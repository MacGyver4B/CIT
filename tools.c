/*
 * tools.c - looking into images (for tests and repairs)
 *
 *   --sums      CRC32 and size of every file, in directory order
 *
 * These only read the image; when nothing else is asked for, cit stops
 * after them without listing or writing the image.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

/* CRC-32 as zip, PNG and the crc32 tools use it (polynomial $EDB88320) */
unsigned long
crc32_of(const unsigned char *data, int size)
{
    unsigned long crc = 0xffffffffUL;
    for (int i = 0; i < size; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1)));
        }
    }
    return crc ^ 0xffffffffUL;
}

static const char *
type_name(int filetype)
{
    static const char *names[] = { "del", "seq", "prg", "usr", "rel", "???", "???", "???" };
    return names[filetype & 7];
}

/* name as ASCII in quotes, padded for a column */
static void
print_name(const unsigned char *name)
{
    char a[3 * FILENAMEMAXSIZE + 1];
    petscii2ascii((unsigned char *)name, a, FILENAMEMAXSIZE, false);
    char q[3 * FILENAMEMAXSIZE + 3];
    snprintf(q, sizeof q, "\"%s\"", a);
    printf("%-18s", q);
}

/* --sums: number, name, type, blocks shown, bytes, CRC32 of the data
   (REL: the data blocks, not the index); entries without blocks show "-",
   entries that share the blocks of another one name it */
void
tool_sums(image_type type, const unsigned char *image)
{
    source_file *f;
    int n = read_source_files(type, image, &f, false);
    printf("  #  name               type blocks    bytes  crc32\n");
    for (int i = 0; i < n; i++) {
        printf("%3d  ", i + 1);
        print_name(f[i].name);
        printf(" %c%s%c %6d", (f[i].filetype & 0x80) ? ' ' : '*', type_name(f[i].filetype),
               (f[i].filetype & 0x40) ? '<' : ' ', f[i].shown_blocks);
        const source_file *d = (f[i].kind == ENTRY_LINK) ? &f[f[i].link] : &f[i];
        if (f[i].kind == ENTRY_DUMMY) {
            printf(" %8s  %-8s  no blocks", "-", "-");
        } else if (d->broken) {
            printf(" %8s  %-8s  broken block chain", "-", "-");
        } else {
            printf(" %8d  %08lx", d->size, crc32_of(d->data, d->size));
            if (f[i].kind == ENTRY_LINK) {
                printf("  same blocks as %d", f[i].link + 1);
            }
            if ((f[i].filetype & 7) == FILETYPEREL) {
                printf("  record length %d", f[i].record_length);
            }
        }
        printf("\n");
    }
    for (int i = 0; i < n; i++) {
        free(f[i].data);
    }
    free(f);
}

/* prints a list of blocks, 12 per line */
static void
print_blocks(const char *label, const ts_block *b, int n)
{
    printf("  %-6s", label);
    for (int i = 0; i < n; i++) {
        if (i > 0 && i % 12 == 0) {
            printf("\n  %-6s", "");
        }
        printf(" %3d/%-3d", b[i].track, b[i].sector);
    }
    printf("  (%d)\n", n);
}

/* --layout pattern: the blocks of every matching file in chain order; REL
   files also with their index (super side sector, side sectors) */
void
tool_layout(image_type type, const unsigned char *image, const char *pattern)
{
    unsigned char ppattern[FILENAMEMAXSIZE];
    evalhexescape((unsigned char *)pattern, ppattern, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
    char *blockmap = calloc(image_num_blocks(type), 1);
    ts_block *chain = malloc(sizeof(ts_block) * image_num_blocks(type));
    char *seen = calloc(image_num_blocks(type), 1);
    if (blockmap == NULL || chain == NULL || seen == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int offset = 0;
    int number = 0;
    do {
        const unsigned char *e = image + linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        if (e[FILETYPEOFFSET] == 0) {
            continue;
        }
        number++;
        if (!match_filename(ppattern, (unsigned char *)e + FILENAMEOFFSET)) {
            continue;
        }
        printf("%3d ", number);
        print_name(e + FILENAMEOFFSET);
        printf(" %s, %d blocks shown, entry in %d/%d\n", type_name(e[FILETYPEOFFSET]),
               e[FILEBLOCKSLOOFFSET] | (e[FILEBLOCKSHIOFFSET] << 8), dt, ds);
        if (is_transwarp_file(image, linear_sector(type, dt, ds) * BLOCKSIZE + offset)) {
            printf("  Transwarp file: the entry points to the loader, data from track %d on\n",
                   e[TRANSWARPTRACKOFFSET]);
        }
        int t = e[FILETRACKOFFSET];
        int s = e[FILESECTOROFFSET];
        if (t == 0) {
            printf("  no blocks\n");
            continue;
        }
        /* data chain, stops at an invalid block or a loop */
        int n = 0;
        const char *problem = NULL;
        memset(seen, 0, image_num_blocks(type));
        while (t != 0) {
            int b = linear_sector(type, t, s);
            if (b < 0) {
                problem = "invalid track/sector";
                break;
            }
            if (seen[b]) {
                problem = "loop";
                break;
            }
            seen[b] = 1;
            chain[n].track = t;
            chain[n].sector = s;
            n++;
            t = image[b * BLOCKSIZE];
            s = image[b * BLOCKSIZE + 1];
        }
        print_blocks("data", chain, n);
        if (problem != NULL) {
            printf("  chain broken after %d blocks: %s (%d/%d)\n", n, problem, t, s);
        } else if (n > 0) {
            const unsigned char *last = image + linear_sector(type, chain[n - 1].track, chain[n - 1].sector) * BLOCKSIZE;
            printf("  last block holds %d bytes\n", last[1] - 1);
        }
        if ((e[FILETYPEOFFSET] & 7) == FILETYPEREL) {
            ts_block index[REL_MAX_INDEX_BLOCKS];
            int ni = rel_index_blocks(type, image, e, index, REL_MAX_INDEX_BLOCKS);
            if (ni < 0) {
                printf("  index broken\n");
            } else {
                int first = 0;
                if (ni > 0 && image[linear_sector(type, index[0].track, index[0].sector) * BLOCKSIZE + 2] == 0xfe) {
                    printf("  super side sector %d/%d\n", index[0].track, index[0].sector);
                    first = 1;
                }
                print_blocks("side", index + first, ni - first);
                const char *r = rel_check_index(type, image, e);
                printf("  record length %d, index %s\n", e[0x17], r ? r : "matches the data blocks");
            }
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(seen);
    free(chain);
    free(blockmap);
}

/* a number: decimal, or hex with $ or 0x in front; returns the rest of the
   string, NULL on error */
static const char *
parse_number(const char *s, int *value)
{
    int base = 10;
    if (*s == '$') {
        base = 16;
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    char *end;
    long v = strtol(s, &end, base);
    if (end == s) {
        return NULL;
    }
    *value = (int)v;
    return end;
}

/* "track/sector" of an image block; returns the rest, NULL on error */
static const char *
parse_block(image_type type, const char *s, int *track, int *sector)
{
    s = parse_number(s, track);
    if (s == NULL || *s != '/') {
        return NULL;
    }
    s = parse_number(s + 1, sector);
    if (s == NULL || linear_sector(type, *track, *sector) < 0) {
        return NULL;
    }
    return s;
}

/* --block t/s: hex dump of one block */
bool
tool_block(image_type type, const unsigned char *image, const char *arg)
{
    int t, s;
    const char *rest = parse_block(type, arg, &t, &s);
    if (rest == NULL || *rest != 0) {
        fprintf(stderr, "ERROR: --block: no block %s in this image (track/sector)\n", arg);
        return false;
    }
    const unsigned char *b = image + linear_sector(type, t, s) * BLOCKSIZE;
    printf("block %d/%d (offset $%06x in the image)\n", t, s, linear_sector(type, t, s) * BLOCKSIZE);
    for (int row = 0; row < BLOCKSIZE; row += 16) {
        printf("%02x:", row);
        for (int k = 0; k < 16; k++) {
            printf("%s%02x", (k == 8) ? "  " : " ", b[row + k]);
        }
        printf("  ");
        for (int k = 0; k < 16; k++) {
            unsigned char c = b[row + k] & 0x7f;
            putchar((c >= 0x20 && c < 0x7f) ? c : '.');
        }
        printf("\n");
    }
    return true;
}

/* --poke t/s:offset=value[,value...]: sets bytes of a block (for test cases
   and patches); the offset may not leave the block */
bool
tool_poke(image_type type, unsigned char *image, const char *arg)
{
    int t, s, offset;
    const char *p = parse_block(type, arg, &t, &s);
    if (p == NULL || *p != ':' || (p = parse_number(p + 1, &offset)) == NULL || *p != '=') {
        fprintf(stderr, "ERROR: --poke: expected track/sector:offset=value[,value...], got %s\n", arg);
        return false;
    }
    unsigned char *b = image + linear_sector(type, t, s) * BLOCKSIZE;
    do {
        int v;
        p = parse_number(p + 1, &v);
        if (p == NULL || v < 0 || v > 255 || offset < 0 || offset >= BLOCKSIZE) {
            fprintf(stderr, "ERROR: --poke: value or offset out of range in %s\n", arg);
            return false;
        }
        b[offset++] = (unsigned char)v;
    } while (*p == ',');
    if (*p != 0) {
        fprintf(stderr, "ERROR: --poke: unexpected \"%s\" in %s\n", p, arg);
        return false;
    }
    return true;
}

/* block chain from t/s, -1 if it is broken (invalid block or loop) */
static int
get_chain(image_type type, const unsigned char *image, int t, int s, ts_block *chain, int max, char *seen)
{
    int n = 0;
    memset(seen, 0, image_num_blocks(type));
    while (t != 0) {
        int b = linear_sector(type, t, s);
        if (b < 0 || seen[b] || n >= max) {
            return -1;
        }
        seen[b] = 1;
        chain[n].track = t;
        chain[n].sector = s;
        n++;
        t = image[b * BLOCKSIZE];
        s = image[b * BLOCKSIZE + 1];
    }
    return n;
}

/* for every block: how many bytes count (0 = free/unused, 256 = all);
   the last block of a file counts up to its end marker */
static void
used_bytes(image_type type, const unsigned char *image, int *count, char *seen, ts_block *chain)
{
    int nb = image_num_blocks(type);
    for (int i = 0; i < nb; i++) {
        count[i] = 0;
    }
    for (unsigned int t = fmt(type)->track0 ? 0 : 1; t <= image_num_tracks(type); t++) {
        for (int s = 0; s < num_sectors(type, t); s++) {
            if (t == 0 || !is_sector_free(type, image, t, s, 0, 0) || is_system_block(type, t, s)) {
                count[linear_sector(type, t, s)] = BLOCKSIZE;
            }
        }
    }
    char *blockmap = calloc(nb, 1);
    if (blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int offset = 0;
    do {
        const unsigned char *e = image + linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        if (e[FILETYPEOFFSET] == 0 || e[FILETRACKOFFSET] == 0) {
            continue;
        }
        int n = get_chain(type, image, e[FILETRACKOFFSET], e[FILESECTOROFFSET], chain, nb, seen);
        if (n > 0) {
            int last = linear_sector(type, chain[n - 1].track, chain[n - 1].sector);
            count[last] = image[last * BLOCKSIZE + 1] + 1; /* bytes 0 .. end marker */
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
}

/* --diff other: compares the image with another one, part by part. Returns
   the number of differences (0 = the same, apart from buffer rests behind
   file ends and the contents of free blocks). */
int
tool_diff(image_type type, const unsigned char *image, const char *other_path)
{
    image_type otype;
    unsigned char *other;
    if (load_source_image(other_path, &otype, &other) < 0) {
        return -1;
    }
    int diffs = 0;
    bool same_format = (otype == type);
    if (!same_format) {
        printf("format: %s here, %s in %s - only directory and contents are compared\n",
               fmt(type)->name, fmt(otype)->name, other_path);
    }

    /* header */
    const unsigned char *h1 = image + linear_sector(type, dirtrack(type), header_sector(type)) * BLOCKSIZE;
    const unsigned char *h2 = other + linear_sector(otype, dirtrack(otype), header_sector(otype)) * BLOCKSIZE;
    if (memcmp(h1 + fmt(type)->header_offset, h2 + fmt(otype)->header_offset, FILENAMEMAXSIZE) != 0) {
        printf("header: disk names differ\n");
        diffs++;
    }
    if (memcmp(h1 + fmt(type)->id_offset, h2 + fmt(otype)->id_offset, 5) != 0) {
        printf("header: disk ids differ\n");
        diffs++;
    }

    /* directory and files, entry by entry */
    source_file *f1;
    source_file *f2;
    int n1 = read_source_files(type, image, &f1, false);
    int n2 = read_source_files(otype, other, &f2, false);
    if (n1 != n2) {
        printf("directory: %d entries here, %d in the other image\n", n1, n2);
        diffs++;
    }
    int nb = image_num_blocks(type) > image_num_blocks(otype) ? image_num_blocks(type) : image_num_blocks(otype);
    ts_block *c1 = malloc(sizeof(ts_block) * nb);
    ts_block *c2 = malloc(sizeof(ts_block) * nb);
    char *seen = calloc(nb, 1);
    if (c1 == NULL || c2 == NULL || seen == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    for (int i = 0; i < n1 && i < n2; i++) {
        const source_file *a = &f1[i];
        const source_file *b = &f2[i];
        char name[3 * FILENAMEMAXSIZE + 1];
        petscii2ascii((unsigned char *)a->name, name, FILENAMEMAXSIZE, false);
        bool head = false;
#define ENTRY_HEAD() do { if (!head) { printf("entry %d \"%s\":\n", i + 1, name); head = true; } diffs++; } while (0)
        if (memcmp(a->name, b->name, FILENAMEMAXSIZE) != 0) {
            char other_name[3 * FILENAMEMAXSIZE + 1];
            petscii2ascii((unsigned char *)b->name, other_name, FILENAMEMAXSIZE, false);
            ENTRY_HEAD();
            printf("  name \"%s\" in the other image\n", other_name);
        }
        if (a->filetype != b->filetype) {
            ENTRY_HEAD();
            printf("  type $%02x here, $%02x in the other image\n", a->filetype, b->filetype);
        }
        if (a->shown_blocks != b->shown_blocks) {
            ENTRY_HEAD();
            printf("  %d blocks shown here, %d in the other image\n", a->shown_blocks, b->shown_blocks);
        }
        const source_file *da = (a->kind == ENTRY_LINK) ? &f1[a->link] : a;
        const source_file *db = (b->kind == ENTRY_LINK) ? &f2[b->link] : b;
        if (a->kind != b->kind) {
            ENTRY_HEAD();
            printf("  kind of entry differs (file, no blocks, same blocks as another)\n");
        } else if (da->broken != db->broken) {
            ENTRY_HEAD();
            printf("  block chain broken %s\n", da->broken ? "here" : "in the other image");
        } else if (a->kind != ENTRY_DUMMY) {
            if (da->size != db->size || (da->size > 0 && memcmp(da->data, db->data, da->size) != 0)) {
                ENTRY_HEAD();
                printf("  contents differ: %d bytes crc32 %08lx here, %d bytes crc32 %08lx there\n",
                       da->size, crc32_of(da->data, da->size), db->size, crc32_of(db->data, db->size));
            }
            if ((a->filetype & 7) == FILETYPEREL && a->record_length != b->record_length) {
                ENTRY_HEAD();
                printf("  record length %d here, %d there\n", a->record_length, b->record_length);
            }
            if (same_format) {
                int l1 = get_chain(type, image, a->start_track, a->start_sector, c1, nb, seen);
                int l2 = get_chain(otype, other, b->start_track, b->start_sector, c2, nb, seen);
                int k = 0;
                while (k < l1 && k < l2 && c1[k].track == c2[k].track && c1[k].sector == c2[k].sector) {
                    k++;
                }
                if (k < l1 || k < l2) {
                    ENTRY_HEAD();
                    if (k < l1 && k < l2) {
                        printf("  layout differs from block %d on: %d/%d here, %d/%d there\n",
                               k + 1, c1[k].track, c1[k].sector, c2[k].track, c2[k].sector);
                    } else {
                        printf("  chains of %d and %d blocks\n", l1, l2);
                    }
                }
            }
        }
#undef ENTRY_HEAD
    }
    for (int i = 0; i < n1; i++) {
        free(f1[i].data);
    }
    for (int i = 0; i < n2; i++) {
        free(f2[i].data);
    }
    free(f1);
    free(f2);

    if (same_format) {
        /* BAM, track by track */
        int bam_tracks = 0;
        for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
            int first = -1;
            int count = 0;
            for (int s = 0; s < num_sectors(type, t); s++) {
                if (is_sector_free(type, image, t, s, 0, 0) != is_sector_free(type, other, t, s, 0, 0)) {
                    if (first < 0) {
                        first = s;
                    }
                    count++;
                }
            }
            if (count > 0) {
                if (bam_tracks++ < 10) {
                    printf("BAM track %u: %d sector%s differ, first %u/%d (%s here)\n", t, count, count == 1 ? "" : "s",
                           t, first, is_sector_free(type, image, t, first, 0, 0) ? "free" : "used");
                }
                diffs++;
            }
        }
        if (bam_tracks > 10) {
            printf("BAM: %d more tracks differ\n", bam_tracks - 10);
        }

        /* used blocks with other contents (free blocks and buffer rests ignored) */
        int *u1 = malloc(sizeof(int) * nb);
        int *u2 = malloc(sizeof(int) * nb);
        if (u1 == NULL || u2 == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        used_bytes(type, image, u1, seen, c1);
        used_bytes(type, other, u2, seen, c1);
        int blocks = 0;
        for (unsigned int t = fmt(type)->track0 ? 0 : 1; t <= image_num_tracks(type); t++) {
            for (int s = 0; s < num_sectors(type, t); s++) {
                int b = linear_sector(type, t, s);
                int len = u1[b] > u2[b] ? u1[b] : u2[b];
                if (len > 0 && memcmp(image + b * BLOCKSIZE, other + b * BLOCKSIZE, len) != 0) {
                    if (blocks++ < 10) {
                        int k = 0;
                        while (image[b * BLOCKSIZE + k] == other[b * BLOCKSIZE + k]) {
                            k++;
                        }
                        printf("block %u/%d differs from byte %d on\n", t, s, k);
                    }
                }
            }
        }
        if (blocks > 10) {
            printf("%d more blocks differ\n", blocks - 10);
        }
        if (blocks > 0) {
            printf("used blocks with other contents: %d\n", blocks);
            diffs++;
        }
        free(u1);
        free(u2);
    }
    free(c1);
    free(c2);
    free(seen);
    free(other);
    printf("%s\n", diffs ? "images differ" : "images are the same (free blocks and buffer rests ignored)");
    return diffs;
}

/* --check: everything that is wrong with an image, sorted into problems
   (data at risk or lost), repairable things and notes. Returns 0 if the
   image is clean, 1 if there is only something repairable, 2 if there are
   problems. */
#define MAXFINDINGS 200
typedef struct {
    char text[160];
} finding;

static int
add_finding(finding *list, int n, const char *fmtstr, ...)
{
    if (n < MAXFINDINGS) {
        va_list ap;
        va_start(ap, fmtstr);
        vsnprintf(list[n].text, sizeof list[n].text, fmtstr, ap);
        va_end(ap);
    }
    return n + 1;
}

static void
print_findings(const char *title, const finding *list, int n)
{
    if (n == 0) {
        return;
    }
    printf("%s:\n", title);
    for (int i = 0; i < n && i < MAXFINDINGS; i++) {
        printf("  %s\n", list[i].text);
    }
    if (n > MAXFINDINGS) {
        printf("  ... %d more\n", n - MAXFINDINGS);
    }
}

int
tool_check(image_type type, const unsigned char *image)
{
    static finding problems[MAXFINDINGS];
    static finding repairable[MAXFINDINGS];
    static finding notes[MAXFINDINGS];
    int np = 0;
    int nr = 0;
    int nn = 0;
    int nb = image_num_blocks(type);
    int *owner = malloc(sizeof(int) * nb);        /* entry number (1..), 0 = none, -1 = system */
    ts_block *chain = malloc(sizeof(ts_block) * nb);
    char *seen = calloc(nb, 1);
    if (owner == NULL || chain == NULL || seen == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    for (int i = 0; i < nb; i++) {
        owner[i] = 0;
    }
    for (unsigned int t = fmt(type)->track0 ? 0 : 1; t <= image_num_tracks(type); t++) {
        for (int s = 0; s < num_sectors(type, t); s++) {
            if (is_system_block(type, t, s)) {
                owner[linear_sector(type, t, s)] = -1;
            }
        }
    }
    if (fmt(type)->second_bam_track) {
        for (int s = 0; s < num_sectors(type, fmt(type)->second_bam_track); s++) {
            owner[linear_sector(type, fmt(type)->second_bam_track, s)] = -1;
        }
    }

    /* directory chain */
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int dirblocks = 0;
    memset(seen, 0, nb);
    while (dt != 0) {
        int b = linear_sector(type, dt, ds);
        if (b < 0) {
            np = add_finding(problems, np, "directory chain points to the invalid block %d/%d", dt, ds);
            break;
        }
        if (seen[b]) {
            np = add_finding(problems, np, "directory chain runs in a loop at %d/%d", dt, ds);
            break;
        }
        seen[b] = 1;
        owner[b] = -1;
        dirblocks++;
        dt = image[b * BLOCKSIZE];
        ds = image[b * BLOCKSIZE + 1];
    }

    /* the entries */
    bool transwarp = false;
    char *blockmap = calloc(nb, 1);
    if (blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    dt = dirtrack(type);
    ds = first_dir_sector(type);
    int offset = 0;
    int number = 0;
    do {
        int eoff = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        const unsigned char *e = image + eoff;
        if (e[FILETYPEOFFSET] == 0) {
            continue;
        }
        number++;
        char name[3 * FILENAMEMAXSIZE + 1];
        petscii2ascii((unsigned char *)e + FILENAMEOFFSET, name, FILENAMEMAXSIZE, false);
        int ft = e[FILETYPEOFFSET];
        int t = e[FILETRACKOFFSET];
        int s = e[FILESECTOROFFSET];
        if ((ft & 7) > FILETYPEREL) {
            np = add_finding(problems, np, "entry %d \"%s\": illegal file type $%02x", number, name, ft);
            continue;
        }
        if (is_transwarp_file(image, eoff)) {
            transwarp = true;
            nn = add_finding(notes, nn, "entry %d \"%s\": Transwarp file (points to the loader)", number, name);
            continue;
        }
        if (t == 0) {
            continue; /* no blocks: directory art */
        }
        if (!(ft & 0x80)) {
            nr = add_finding(repairable, nr, "entry %d \"%s\": unclosed file (a VALIDATE deletes it and frees its blocks)", number, name);
        }
        int first = linear_sector(type, t, s);
        if (first >= 0 && owner[first] > 0) {
            nn = add_finding(notes, nn, "entry %d \"%s\" shares the blocks of entry %d (directory art)", number, name, owner[first]);
            continue;
        }
        int n = get_chain(type, image, t, s, chain, nb, seen);
        if (n < 0) {
            /* find where: walk again until the bad block */
            int k = 0;
            int ct = t;
            int cs = s;
            memset(seen, 0, nb);
            while (ct != 0) {
                int b = linear_sector(type, ct, cs);
                if (b < 0) {
                    np = add_finding(problems, np, "entry %d \"%s\": chain broken after %d blocks, invalid block %d/%d", number, name, k, ct, cs);
                    break;
                }
                if (seen[b]) {
                    np = add_finding(problems, np, "entry %d \"%s\": chain runs in a loop after %d blocks at %d/%d", number, name, k, ct, cs);
                    break;
                }
                seen[b] = 1;
                if (owner[b] == 0) {
                    owner[b] = number;
                }
                k++;
                ct = image[b * BLOCKSIZE];
                cs = image[b * BLOCKSIZE + 1];
            }
            continue;
        }
        for (int k = 0; k < n; k++) {
            int b = linear_sector(type, chain[k].track, chain[k].sector);
            if (owner[b] != 0) {
                if (owner[b] < 0) {
                    np = add_finding(problems, np, "entry %d \"%s\": block %d (%d/%d) is a directory, header or BAM block", number, name, k + 1, chain[k].track, chain[k].sector);
                } else {
                    np = add_finding(problems, np, "entry %d \"%s\": block %d (%d/%d) also belongs to entry %d", number, name, k + 1, chain[k].track, chain[k].sector, owner[b]);
                }
                break;
            }
            owner[b] = number;
        }
        if ((ft & 7) == FILETYPEREL) {
            const char *r = rel_check_index(type, image, e);
            if (r != NULL) {
                np = add_finding(problems, np, "entry %d \"%s\": REL index: %s", number, name, r);
            }
            ts_block index[REL_MAX_INDEX_BLOCKS];
            int ni = rel_index_blocks(type, image, e, index, REL_MAX_INDEX_BLOCKS);
            for (int k = 0; k < ni; k++) {
                int b = linear_sector(type, index[k].track, index[k].sector);
                if (owner[b] == 0) {
                    owner[b] = number;
                }
            }
        }
        int shown = e[FILEBLOCKSLOOFFSET] | (e[FILEBLOCKSHIOFFSET] << 8);
        int real = n + (((ft & 7) == FILETYPEREL) ? rel_index_count(type, n) : 0);
        if (shown != real) {
            nn = add_finding(notes, nn, "entry %d \"%s\": %d blocks shown, %d used", number, name, shown, real);
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);

    /* BAM against the blocks in use */
    int lost = 0;
    char lost_first[16] = "";
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        int bits_free = 0;
        for (int s = 0; s < num_sectors(type, t); s++) {
            int b = linear_sector(type, t, s);
            bool free_bam = is_sector_free(type, image, t, s, 0, 0);
            bits_free += free_bam ? 1 : 0;
            if (free_bam && owner[b] != 0) {
                nr = add_finding(repairable, nr, "block %u/%d is in use but free in the BAM (data could be overwritten)", t, s);
            }
            if (!free_bam && owner[b] == 0 && !(fmt(type)->second_bam_track && (int)t == fmt(type)->second_bam_track)) {
                if (lost++ == 0) {
                    snprintf(lost_first, sizeof lost_first, "%u/%d", t, s);
                }
            }
        }
        int counter = bam_track_free(type, image, t);
        if (counter != bits_free) {
            nr = add_finding(repairable, nr, "track %u: BAM counter says %d free, the bitmap %d", t, counter, bits_free);
        }
    }
    if (lost > 0) {
        if (transwarp) {
            nn = add_finding(notes, nn, "%d blocks used in the BAM belong to no chain (first %s): the Transwarp data; a VALIDATE would destroy them", lost, lost_first);
        } else {
            nr = add_finding(repairable, nr, "%d block%s used in the BAM but in no file (first %s): a VALIDATE frees them", lost, lost == 1 ? "" : "s", lost_first);
        }
    }

    print_findings("problems (data at risk or lost)", problems, np);
    print_findings("repairable", repairable, nr);
    print_findings("notes", notes, nn);
    printf("%d problem%s, %d repairable, %d note%s\n", np, np == 1 ? "" : "s", nr, nn, nn == 1 ? "" : "s");
    free(owner);
    free(chain);
    free(seen);
    return np ? 2 : nr ? 1 : 0;
}

/* JSON string of PETSCII bytes as ASCII (cit's usual mapping) */
static void
json_name(const unsigned char *p, int len)
{
    char a[3 * FILENAMEMAXSIZE + 1];
    petscii2ascii((unsigned char *)p, a, len, false);
    putchar('"');
    for (char *c = a; *c; c++) {
        if (*c == '"' || *c == '\\') {
            printf("\\%c", *c);
        } else if ((unsigned char)*c < 0x20) {
            printf("\\u%04x", (unsigned char)*c);
        } else {
            putchar(*c);
        }
    }
    putchar('"');
}

static void
json_hex(const unsigned char *p, int len)
{
    putchar('"');
    for (int i = 0; i < len; i++) {
        printf("%02x", p[i]);
    }
    putchar('"');
}

/* --json: the image for scripts and other programs */
void
tool_json(image_type type, const unsigned char *image)
{
    static const char *types[] = { "del", "seq", "prg", "usr", "rel", "?", "?", "?" };
    static const char *kinds[] = { "file", "no blocks", "link" };
    const unsigned char *h = image + linear_sector(type, dirtrack(type), header_sector(type)) * BLOCKSIZE;
    int nb = image_num_blocks(type);
    ts_block *chain = malloc(sizeof(ts_block) * nb);
    char *seen = calloc(nb, 1);
    if (chain == NULL || seen == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int free_blocks = 0;
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        if ((int)t != dirtrack(type) || fmt(type)->heads) {
            free_blocks += bam_track_free(type, image, t);
        }
    }
    printf("{\n  \"format\": \"%s\",\n  \"tracks\": %u,\n", fmt(type)->name, image_num_tracks(type));
    printf("  \"name\": ");
    json_name(h + fmt(type)->header_offset, FILENAMEMAXSIZE);
    printf(",\n  \"name_hex\": ");
    json_hex(h + fmt(type)->header_offset, FILENAMEMAXSIZE);
    printf(",\n  \"id\": ");
    json_name(h + fmt(type)->id_offset, 5);
    printf(",\n  \"blocks_free\": %d,\n  \"entries\": [", free_blocks);

    source_file *f;
    int n = read_source_files(type, image, &f, false);
    for (int i = 0; i < n; i++) {
        const source_file *d = (f[i].kind == ENTRY_LINK) ? &f[f[i].link] : &f[i];
        printf("%s\n    {\"number\": %d, \"name\": ", i ? "," : "", i + 1);
        json_name(f[i].name, FILENAMEMAXSIZE);
        printf(", \"name_hex\": ");
        json_hex(f[i].name, FILENAMEMAXSIZE);
        printf(", \"type\": \"%s\", \"closed\": %s, \"locked\": %s, \"blocks_shown\": %d, \"kind\": \"%s\"",
               types[f[i].filetype & 7], (f[i].filetype & 0x80) ? "true" : "false",
               (f[i].filetype & 0x40) ? "true" : "false", f[i].shown_blocks, kinds[f[i].kind]);
        if (f[i].kind == ENTRY_LINK) {
            printf(", \"link\": %d", f[i].link + 1);
        }
        if (d->broken) {
            printf(", \"broken\": true");
        } else if (f[i].kind != ENTRY_DUMMY) {
            printf(", \"bytes\": %d, \"crc32\": \"%08lx\"", d->size, crc32_of(d->data, d->size));
            if ((f[i].filetype & 7) == FILETYPEREL) {
                printf(", \"record_length\": %d", f[i].record_length);
            }
            int len = get_chain(type, image, f[i].start_track, f[i].start_sector, chain, nb, seen);
            printf(", \"blocks\": [");
            for (int k = 0; k < len; k++) {
                printf("%s\"%d/%d\"", k ? ", " : "", chain[k].track, chain[k].sector);
            }
            printf("]");
        }
        printf("}");
    }
    printf("\n  ],\n  \"free_per_track\": [");
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        printf("%s%d", t > 1 ? ", " : "", bam_track_free(type, image, t));
    }
    printf("]\n}\n");
    for (int i = 0; i < n; i++) {
        free(f[i].data);
    }
    free(f);
    free(chain);
    free(seen);
}
