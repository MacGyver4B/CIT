/*
 * rel.c - REL files as one unit of data and index
 *
 * A REL file has two chains: the data blocks, linked like any other file,
 * and its index. The index is a chain of side sectors, each listing up to
 * 120 data blocks in order; the 8250 and the 1581 put a super side sector
 * in front that lists the first side sector of every group of 6. The
 * directory entry points to the first block of the index ($15/$16) and
 * holds the record length ($17).
 *
 *   super side sector  [0,1] first side sector, [2] $FE,
 *                      [3..] first side sector of group 0, 1, ...
 *   side sector        [0,1] next side sector (last: 0, last used byte),
 *                      [2] number in its group (0-5), [3] record length,
 *                      [4..15] the 6 side sectors of its group,
 *                      [16..255] the data blocks, 120 at most
 *
 * The index only holds block addresses, so it is valid for exactly one
 * layout of the data: it is never copied or unpacked, but built anew
 * whenever the data are written.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

#define REL_SS_TRACK      0x15  /* directory entry: first index block */
#define REL_SS_SECTOR     0x16
#define REL_RECORD_LENGTH 0x17
#define SS_FIRST_DATA     16    /* side sector: first data block pointer */
#define SS_PER_GROUP      6
#define SS_DATA_BLOCKS    120
#define SSS_ID            0xfe

bool
rel_has_super_side_sector(image_type type)
{
    return type == IMAGE_D82 || type == IMAGE_D81 || type == IMAGE_D60 || type == IMAGE_D90;
}

static bool
valid_ts(image_type type, int t, int s)
{
    return t >= 1 && t <= (int)image_num_tracks(type) && s < num_sectors(type, t);
}

/* index blocks of the REL file whose directory entry is at entry (offset 0
   = the 2 link bytes of the directory block for the first entry, like
   everywhere in cit): super side sector first, then the side sectors in
   chain order. Returns their number, -1 if the index is broken. */
int
rel_index_blocks(image_type type, const unsigned char *image, const unsigned char *entry, ts_block *blocks, int max)
{
    int t = entry[REL_SS_TRACK];
    int s = entry[REL_SS_SECTOR];
    int n = 0;
    if (!valid_ts(type, t, s)) {
        return -1;
    }
    const unsigned char *b = image + linear_sector(type, t, s) * BLOCKSIZE;
    if (b[2] == SSS_ID) {
        blocks[n].track = t;
        blocks[n].sector = s;
        n++;
        t = b[0];
        s = b[1];
    }
    while (t != 0) {
        if (!valid_ts(type, t, s) || n >= max) {
            return -1;
        }
        for (int i = 0; i < n; i++) {
            if (blocks[i].track == t && blocks[i].sector == s) {
                return -1; /* loop */
            }
        }
        blocks[n].track = t;
        blocks[n].sector = s;
        n++;
        b = image + linear_sector(type, t, s) * BLOCKSIZE;
        t = b[0];
        s = b[1];
    }
    return n;
}

/* Does the index list exactly the data chain, with the record length of
   the directory entry? Returns NULL if so, else a short reason. */
const char *
rel_check_index(image_type type, const unsigned char *image, const unsigned char *entry)
{
    ts_block index[REL_MAX_INDEX_BLOCKS];
    int n = rel_index_blocks(type, image, entry, index, REL_MAX_INDEX_BLOCKS);
    if (n < 0) {
        return "broken side sector chain";
    }
    int first_ss = 0;
    if (n > 0 && image[linear_sector(type, index[0].track, index[0].sector) * BLOCKSIZE + 2] == SSS_ID) {
        first_ss = 1;
    }
    if (n == first_ss) {
        return "no side sector";
    }
    int t = entry[FILETRACKOFFSET];
    int s = entry[FILESECTOROFFSET];
    for (int i = first_ss; i < n; i++) {
        const unsigned char *ss = image + linear_sector(type, index[i].track, index[i].sector) * BLOCKSIZE;
        if (ss[3] != entry[REL_RECORD_LENGTH]) {
            return "record length in side sector differs";
        }
        if (ss[2] != (i - first_ss) % SS_PER_GROUP) {
            return "side sector number wrong";
        }
        int last = (ss[0] == 0) ? ss[1] : 255;
        for (int p = SS_FIRST_DATA; p + 1 <= last; p += 2) {
            if (ss[p] != t || ss[p + 1] != s) {
                return "side sector does not match the data blocks";
            }
            if (!valid_ts(type, t, s)) {
                return "broken data chain";
            }
            const unsigned char *d = image + linear_sector(type, t, s) * BLOCKSIZE;
            t = d[0];
            s = d[1];
        }
    }
    if (t != 0) {
        return "data blocks missing in the side sectors";
    }
    return NULL;
}

/* number of index blocks (side sectors and super side sector) for n data blocks */
int
rel_index_count(image_type type, int data_blocks)
{
    int ss = (data_blocks + SS_DATA_BLOCKS - 1) / SS_DATA_BLOCKS;
    if (ss == 0) {
        ss = 1;
    }
    return ss + (rel_has_super_side_sector(type) ? 1 : 0);
}

/*
 * Order of the blocks of a REL file as the drive writes it when the file is
 * expanded in one go (position to the last record and write), read from
 * images written by the 8050 and 8250 DOS in VICE:
 *   data 1 as the first block of any file; side sector 0 and then the super
 *   side sector as the next blocks after data 1; every further data block
 *   the next block after the previous one; side sector j (j >= 1) after data
 *   120 * j + 2 is taken, as the next block after data 120 * j + 1.
 * The blocks are marked as used in the BAM. Returns false if the disk is full.
 */
static bool
rel_allocate_dos(image_type type, unsigned char *image, int data_blocks, ts_block *data, ts_block *ss, ts_block *sss)
{
    int inc = fmt(type)->file_interleave;
    int t;
    int s;
    if (!dos_first_block(type, image, &t, &s)) {
        return false;
    }
    data[0].track = t;
    data[0].sector = s;
    mark_sector(type, image, t, s, 0);

    int nss = rel_index_count(type, data_blocks) - (rel_has_super_side_sector(type) ? 1 : 0);
    int ss_done = 0;
    int nt = t;
    int ns = s;
    if (!dos_next_block(type, image, &nt, &ns, inc)) {
        return false;
    }
    ss[ss_done].track = nt;
    ss[ss_done].sector = ns;
    ss_done++;
    mark_sector(type, image, nt, ns, 0);
    if (rel_has_super_side_sector(type)) {
        nt = t;
        ns = s;
        if (!dos_next_block(type, image, &nt, &ns, inc)) {
            return false;
        }
        sss->track = nt;
        sss->sector = ns;
        mark_sector(type, image, nt, ns, 0);
    }
    for (int k = 1; k < data_blocks; k++) {
        nt = data[k - 1].track;
        ns = data[k - 1].sector;
        if (!dos_next_block(type, image, &nt, &ns, inc)) {
            return false;
        }
        data[k].track = nt;
        data[k].sector = ns;
        mark_sector(type, image, nt, ns, 0);
        /* side sector j once data 120 * j + 2 is taken (k is 0-based) */
        if (k >= 2 && (k - 1) % SS_DATA_BLOCKS == 0 && ss_done < nss) {
            nt = data[k - 1].track;
            ns = data[k - 1].sector;
            if (!dos_next_block(type, image, &nt, &ns, inc)) {
                return false;
            }
            ss[ss_done].track = nt;
            ss[ss_done].sector = ns;
            ss_done++;
            mark_sector(type, image, nt, ns, 0);
        }
    }
    /* a file that ends with data 120 * j + 1 still needs side sector j */
    while (ss_done < nss) {
        nt = data[data_blocks - 1].track;
        ns = data[data_blocks - 1].sector;
        if (!dos_next_block(type, image, &nt, &ns, inc)) {
            return false;
        }
        ss[ss_done].track = nt;
        ss[ss_done].sector = ns;
        ss_done++;
        mark_sector(type, image, nt, ns, 0);
    }
    return true;
}

/* auto: the planned blocks are one stream:
   [super side sector] side sector 0, data 1-120, side sector 1, data 121-240 ... */
static void
rel_split_plan(image_type type, const ts_block *plan, int data_blocks, ts_block *data, ts_block *ss, ts_block *sss)
{
    int p = 0;
    if (rel_has_super_side_sector(type)) {
        *sss = plan[p++];
    }
    int d = 0;
    int j = 0;
    while (d < data_blocks || j == 0) {
        ss[j++] = plan[p++];
        for (int k = 0; k < SS_DATA_BLOCKS && d < data_blocks; k++) {
            data[d++] = plan[p++];
        }
    }
}

/*
 * Writes a REL file: data blocks and a new index. plan: blocks planned by
 * auto (data and index blocks, see rel_split_plan), NULL for the DOS order.
 * Fills in the directory entry at entry. Returns the number of blocks.
 */
int
rel_write(image_type type, unsigned char *image, unsigned char *entry, const unsigned char *data, int size,
          int record_length, const ts_block *plan)
{
    int data_blocks = (size + BLOCKSIZE - BLOCKOVERHEAD - 1) / (BLOCKSIZE - BLOCKOVERHEAD);
    if (data_blocks == 0) {
        data_blocks = 1;
    }
    int nss = rel_index_count(type, data_blocks) - (rel_has_super_side_sector(type) ? 1 : 0);
    if (nss > 126 * SS_PER_GROUP || (!rel_has_super_side_sector(type) && nss > SS_PER_GROUP)) {
        fprintf(stderr, "ERROR: REL file too large for this format (%d blocks)\n", data_blocks);
        exit(-1);
    }
    ts_block *db = malloc(sizeof(ts_block) * data_blocks);
    ts_block *ss = malloc(sizeof(ts_block) * nss);
    ts_block sss = { 0, 0 };
    if (db == NULL || ss == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    if (plan != NULL) {
        rel_split_plan(type, plan, data_blocks, db, ss, &sss);
        for (int i = 0; i < data_blocks; i++) {
            mark_sector(type, image, db[i].track, db[i].sector, 0);
        }
        for (int i = 0; i < nss; i++) {
            mark_sector(type, image, ss[i].track, ss[i].sector, 0);
        }
        if (rel_has_super_side_sector(type)) {
            mark_sector(type, image, sss.track, sss.sector, 0);
        }
    } else if (!rel_allocate_dos(type, image, data_blocks, db, ss, &sss)) {
        fprintf(stderr, "ERROR: Disk full, REL file (%d blocks)\n", data_blocks + rel_index_count(type, data_blocks));
        exit(-1);
    }

    /* data blocks */
    for (int i = 0; i < data_blocks; i++) {
        unsigned char *b = image + linear_sector(type, db[i].track, db[i].sector) * BLOCKSIZE;
        int n = size - i * (BLOCKSIZE - BLOCKOVERHEAD);
        if (n > BLOCKSIZE - BLOCKOVERHEAD) {
            n = BLOCKSIZE - BLOCKOVERHEAD;
        }
        if (n < 0) {
            n = 0;
        }
        memset(b, 0, BLOCKSIZE);
        memcpy(b + 2, data + i * (BLOCKSIZE - BLOCKOVERHEAD), n);
        if (i + 1 < data_blocks) {
            b[0] = db[i + 1].track;
            b[1] = db[i + 1].sector;
        } else {
            b[0] = 0;
            b[1] = n + 1;
            /* the drive starts the next (empty) record behind the last one
               with $FF, seen with every DOS from 1541 to 8250 */
            if (n + 2 < BLOCKSIZE) {
                b[n + 2] = 0xff;
            }
        }
    }

    /* side sectors */
    for (int j = 0; j < nss; j++) {
        unsigned char *b = image + linear_sector(type, ss[j].track, ss[j].sector) * BLOCKSIZE;
        memset(b, 0, BLOCKSIZE);
        int first = j * SS_DATA_BLOCKS;
        int count = data_blocks - first;
        if (count > SS_DATA_BLOCKS) {
            count = SS_DATA_BLOCKS;
        }
        if (j + 1 < nss) {
            b[0] = ss[j + 1].track;
            b[1] = ss[j + 1].sector;
        } else {
            b[0] = 0;
            b[1] = SS_FIRST_DATA + 2 * count - 1;
        }
        b[2] = j % SS_PER_GROUP;
        b[3] = record_length;
        int group = j / SS_PER_GROUP;
        for (int g = 0; g < SS_PER_GROUP && group * SS_PER_GROUP + g < nss; g++) {
            b[4 + 2 * g] = ss[group * SS_PER_GROUP + g].track;
            b[5 + 2 * g] = ss[group * SS_PER_GROUP + g].sector;
        }
        for (int k = 0; k < count; k++) {
            b[SS_FIRST_DATA + 2 * k] = db[first + k].track;
            b[SS_FIRST_DATA + 2 * k + 1] = db[first + k].sector;
        }
    }

    /* super side sector */
    if (rel_has_super_side_sector(type)) {
        unsigned char *b = image + linear_sector(type, sss.track, sss.sector) * BLOCKSIZE;
        memset(b, 0, BLOCKSIZE);
        b[0] = ss[0].track;
        b[1] = ss[0].sector;
        b[2] = SSS_ID;
        for (int g = 0; g * SS_PER_GROUP < nss; g++) {
            b[3 + 2 * g] = ss[g * SS_PER_GROUP].track;
            b[4 + 2 * g] = ss[g * SS_PER_GROUP].sector;
        }
    }

    /* directory entry */
    entry[FILETRACKOFFSET] = db[0].track;
    entry[FILESECTOROFFSET] = db[0].sector;
    if (rel_has_super_side_sector(type)) {
        entry[REL_SS_TRACK] = sss.track;
        entry[REL_SS_SECTOR] = sss.sector;
    } else {
        entry[REL_SS_TRACK] = ss[0].track;
        entry[REL_SS_SECTOR] = ss[0].sector;
    }
    entry[REL_RECORD_LENGTH] = record_length;
    int blocks = data_blocks + rel_index_count(type, data_blocks);
    free(db);
    free(ss);
    return blocks;
}
