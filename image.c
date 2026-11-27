/*******************************************************************************
* Copyright (c) 2008-2024 JackAsser, Krill, Claus, Björn Esser
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*******************************************************************************/

/* image.c: format table, geometry, BAM, directory and validation */

#include "cit.h"

static const char *error_name[] = {
    "no error", "illegal track", "illegal sector", "loop", "collision", "chained", "chained", "first block broken"
};

static const format_t formats[] = {
    [IMAGE_D64] = {
        .name = "D64", .num_tracks = 35, .sides = 1,
        .zone = {{17, 21}, {24, 19}, {30, 18}, {35, 17}},
        .dir_track = 18, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 10,
        .header_offset = 0x90, .id_offset = 0xa2, .dos_type = 'A',
        .default_id = "00#a02a"
    },
    [IMAGE_D64_EXTENDED_SPEED_DOS] = {
        .name = "D64 (40 tracks, SpeedDOS BAM)", .num_tracks = 40, .sides = 1,
        .zone = {{17, 21}, {24, 19}, {30, 18}, {40, 17}},
        .dir_track = 18, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 10,
        .header_offset = 0x90, .id_offset = 0xa2, .dos_type = 0,
        .default_id = "00#a02a"
    },
    [IMAGE_D64_EXTENDED_DOLPHIN_DOS] = {
        .name = "D64 (40 tracks, DolphinDOS BAM)", .num_tracks = 40, .sides = 1,
        .zone = {{17, 21}, {24, 19}, {30, 18}, {40, 17}},
        .dir_track = 18, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 10,
        .header_offset = 0x90, .id_offset = 0xa2, .dos_type = 0,
        .default_id = "00#a02a"
    },
    [IMAGE_D71] = {
        .name = "D71", .num_tracks = 70, .sides = 2,
        .zone = {{17, 21}, {24, 19}, {30, 18}, {35, 17}, {52, 21}, {59, 19}, {65, 18}, {70, 17}},
        .dir_track = 18, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 6,
        .header_offset = 0x90, .id_offset = 0xa2, .dos_type = 'A',
        .second_bam_track = 53, .num_bam_blocks = 1, .bam_block = {{53, 0}},
        .default_id = "00#a02a"
    },
    [IMAGE_D81] = {
        .name = "D81", .num_tracks = 80, .sides = 1,
        .zone = {{80, 40}},
        .dir_track = 40, .first_dir_sector = 3, .dir_interleave = 1, .file_interleave = 1,
        .header_offset = 0x04, .id_offset = 0x16, .dos_type = 'D',
        .num_bam_blocks = 2, .bam_block = {{40, 1}, {40, 2}},
        .default_id = "00#a03d"
    },
    /* 8050 and 8250/SFD-1001 (DOS 2.7): header 39/0, BAM on track 38 (one
       block per 50 tracks), directory from 39/1 with interleave 3 */
    [IMAGE_D80] = {
        .name = "D80", .num_tracks = 77, .sides = 1,
        .zone = {{39, 29}, {53, 27}, {64, 25}, {77, 23}},
        .dir_track = 39, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 5,
        .header_offset = 0x06, .id_offset = 0x18, .dos_type = 'C',
        .num_bam_blocks = 2, .bam_block = {{38, 0}, {38, 3}},
        .default_id = "00#a02c"
    },
    [IMAGE_D82] = {
        .name = "D82", .num_tracks = 154, .sides = 2,
        .zone = {{39, 29}, {53, 27}, {64, 25}, {77, 23}, {116, 29}, {130, 27}, {141, 25}, {154, 23}},
        .dir_track = 39, .first_dir_sector = 1, .dir_interleave = 3, .file_interleave = 5,
        .header_offset = 0x06, .id_offset = 0x18, .dos_type = 'C',
        .num_bam_blocks = 4, .bam_block = {{38, 0}, {38, 3}, {38, 6}, {38, 9}},
        .default_id = "00#a02c"
    },
};


const format_t *
fmt(image_type type)
{
    return &formats[type];
}

/* Returns the number of tracks in an image */
unsigned int
image_num_tracks(image_type type)
{
    return fmt(type)->num_tracks;
}

/* Returns the number of sectors for a given track */
int
num_sectors(image_type type, int track)
{
    const format_t *f = fmt(type);
    for (int z = 0; z < MAXZONES && f->zone[z].last_track; z++) {
        if (track <= f->zone[z].last_track) {
            return f->zone[z].sectors;
        }
    }
    return 0;
}

/* Returns the highest number of sectors on any track */
int
max_sectors(image_type type)
{
    int max = 0;
    for (int z = 0; z < MAXZONES && fmt(type)->zone[z].last_track; z++) {
        if (fmt(type)->zone[z].sectors > max) {
            max = fmt(type)->zone[z].sectors;
        }
    }
    return max;
}

/* Returns the number of blocks in an image */
unsigned int
image_num_blocks(image_type type)
{
    int num_blocks = 0;
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        num_blocks += num_sectors(type, t);
    }
    return num_blocks;
}

/* Returns the size of an image in bytes (without error table) */
unsigned int
image_size(image_type type)
{
    return image_num_blocks(type) * BLOCKSIZE;
}

/* Returns the directory track of an image */
int
dirtrack(image_type type)
{
    return fmt(type)->dir_track;
}

/* Returns the number of tracks on one side (D71: 35) */
int
side_tracks(image_type type)
{
    return fmt(type)->num_tracks / fmt(type)->sides;
}

/* Header (dir_track/0) or BAM block: not part of any file */
bool
is_system_block(image_type type, int track, int sector)
{
    if (track == dirtrack(type) && sector == 0) {
        return true;
    }
    for (int i = 0; i < fmt(type)->num_bam_blocks; i++) {
        if (track == fmt(type)->bam_block[i].track && sector == fmt(type)->bam_block[i].sector) {
            return true;
        }
    }
    return false;
}

/* Returns the first directory sector on the directory track */
int
first_dir_sector(image_type type)
{
    return fmt(type)->first_dir_sector;
}

/* Calculates the overall sector index from a given track and sector, returns -1 for invalid track/sector */
int
linear_sector(image_type type, int track, int sector)
{
    if ((track < 1) || (track > (int)image_num_tracks(type))) {
        return -1;
    }

    int numsectors = num_sectors(type, track);
    if ((sector < 0) || (sector >= numsectors)) {
        return -1;
    }

    int linear_sector = 0;
    for (int i = 0; i < track - 1; i++) {
        linear_sector += num_sectors(type, i + 1);
    }
    linear_sector += sector;

    return linear_sector;
}

/*
 * BAM entry of a track: offset of the number of free sectors and offset of
 * the bitmap (bit set = sector free, sector 0 = bit 0 of the first byte).
 *
 *   D64 (1541/2031/4040)  18/0, 4 bytes per track from offset 4
 *   D64 40 tracks         tracks 36-40 in 18/0 from $c0 (SpeedDOS) or $ac (DolphinDOS)
 *   D71 (1571)            side 2: counters in 18/0 from $dd, bitmaps (3 bytes) in 53/0
 *   D81 (1581)            40/1 (tracks 1-40) and 40/2 (41-80), 6 bytes per track from $10
 *   D80/D82 (8050/8250)   track 38, sectors 0, 3, 6, 9 for 50 tracks each,
 *                         5 bytes per track from offset 6
 */
static void
bam_entry(image_type type, int track, int *count, int *bitmap)
{
    int bam;

    switch (type) {
    case IMAGE_D81:
        bam = linear_sector(type, dirtrack(type), (track <= 40) ? 1 : 2) * BLOCKSIZE;
        *count = bam + 0x10 + ((track - 1) % 40) * 6;
        *bitmap = *count + 1;
        return;

    case IMAGE_D80:
    case IMAGE_D82:
        bam = linear_sector(type, 38, 3 * ((track - 1) / 50)) * BLOCKSIZE;
        *count = bam + 6 + ((track - 1) % 50) * 5;
        *bitmap = *count + 1;
        return;

    case IMAGE_D71:
        if (track > side_tracks(type)) {
            bam = linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
            *count = bam + 0xdd + track - side_tracks(type) - 1;
            *bitmap = linear_sector(type, fmt(type)->second_bam_track, 0) * BLOCKSIZE
                      + (track - side_tracks(type) - 1) * 3;
            return;
        }
        break;

    case IMAGE_D64_EXTENDED_SPEED_DOS:
    case IMAGE_D64_EXTENDED_DOLPHIN_DOS:
        if (track > D64NUMTRACKS) {
            bam = linear_sector(type, dirtrack(type), 0) * BLOCKSIZE
                  + ((type == IMAGE_D64_EXTENDED_SPEED_DOS) ? BAM_OFFSET_SPEED_DOS : BAM_OFFSET_DOLPHIN_DOS);
            *count = bam + (track - D64NUMTRACKS - 1) * 4;
            *bitmap = *count + 1;
            return;
        }
        break;

    default:
        break;
    }
    bam = linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
    *count = bam + track * 4;
    *bitmap = *count + 1;
}

/* Returns the image offset of the bam bitmap for a given track */
int
get_bam_offset(image_type type, unsigned int track)
{
    int count, bitmap;
    bam_entry(type, track, &count, &bitmap);
    return bitmap;
}

/* Returns the image offset of the number of free sectors for a given track */
int
get_bam_count_offset(image_type type, unsigned int track)
{
    int count, bitmap;
    bam_entry(type, track, &count, &bitmap);
    return count;
}

/* Checks if a given sector is marked as free in the BAM and also not used by directory */
int
is_sector_free(image_type type, const unsigned char* image, int track, int sector, int numdirblocks, int dir_sector_interleave)
{
    const unsigned char* bitmap;

    if (sector < 0) {
        fprintf(stderr, "ERROR: Illegal sector %d for track %d\n", sector, track);

        exit(-1);
    }

    bitmap = image + get_bam_offset(type, track);

    int byte = sector >> 3;
    int bit = sector & 7;

    int is_not_dir_block = 1;
    if ((track == dirtrack(type)) && (numdirblocks > 0)) {
        int dirsector = 0;
        int s = 2;
        for (int i = 0; is_not_dir_block && (i < numdirblocks); i++) {
            switch (i) {
            case 0:
                dirsector = 0;
                break;

            case 1:
                dirsector = 1;
                break;

            default:
                dirsector += dir_sector_interleave;
                if (dirsector >= num_sectors(type, track)) {
                    dirsector = s;
                    s++;
                }
                break;
            }
            is_not_dir_block = (sector != dirsector);
        }
    }

    return is_not_dir_block && ((bitmap[byte] & (1 << bit)) != 0);
}

/* Marks given sector in BAM */
void
mark_sector(image_type type, unsigned char* image, int track, int sector, int free)
{
    if (free != is_sector_free(type, image, track, sector, 0, 0)) {
        unsigned char* count = image + get_bam_count_offset(type, track);
        unsigned char* bitmap = image + get_bam_offset(type, track);

        /* update number of free sectors on track */
        if (free) {
            ++*count;
        } else {
            --*count;
        }

        /* update bitmap */
        int byte = sector >> 3;
        int bit = sector & 7;

        if (free) {
            bitmap[byte] |= 1 << bit;
        } else {
            bitmap[byte] &= ~(1 << bit);
        }
    }
}

/* Returns offset for header on directory track */
int
get_header_offset(image_type type)
{
    return fmt(type)->header_offset;
}

/* Returns offset for id on directory track */
int
get_id_offset(image_type type)
{
    return fmt(type)->id_offset;
}

/* Updates the directory with the given header, id and BAM message */
void
update_directory(image_type type, unsigned char* image, unsigned char* header, unsigned char* id, unsigned char *bam_message, int shadowdirtrack)
{
    unsigned int bam = linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;

    if (type != IMAGE_D81) {
        image[bam + 0x03] = (type == IMAGE_D71) ? 0x80 : 0x00;
    }

    /* Set header and ID */
    unsigned char pheader[FILENAMEMAXSIZE];
    unsigned char pid[5];
    evalhexescape(header, pheader, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
    evalhexescape(id, pid, 5, FILENAMEEMPTYCHAR);
    memcpy(image + bam + get_header_offset(type), pheader, FILENAMEMAXSIZE);
    memcpy(image + bam + get_id_offset(type), pid, 5);

    /* Set BAM message */
    if(bam_message != NULL) {
        unsigned char pbam_message[BAMMESSAGEMAXSIZE];
        int bam_message_len = BAMMESSAGEMAXSIZE;
        if(type == IMAGE_D64_EXTENDED_SPEED_DOS) {
            bam_message_len = 0xbf - BAMMESSAGEOFFSET; /* avoid conflict with extended BAM and allow for a 0 at the end*/
        }
        evalhexescape(bam_message, pbam_message, bam_message_len, 0);
        memcpy(image + bam + BAMMESSAGEOFFSET, pbam_message, bam_message_len);
    }

    if (type == IMAGE_D81) {
        unsigned int bam = linear_sector(type, dirtrack(type), 1 /* sector */) * BLOCKSIZE;
        image[bam + 0x04] = id[0];
        image[bam + 0x05] = id[1];

        bam = linear_sector(type, dirtrack(type), 2 /* sector */) * BLOCKSIZE;
        image[bam + 0x04] = id[0];
        image[bam + 0x05] = id[1];
    }

    if (shadowdirtrack > 0) {
        unsigned int shadowbam = linear_sector(type, shadowdirtrack, 0 /* sector */) * BLOCKSIZE;
        memcpy(image + shadowbam, image + bam, BLOCKSIZE);

        image[shadowbam + 0x00] = shadowdirtrack;
    }
}

/* Writes an empty directory and BAM */
void
initialize_directory(image_type type, unsigned char* image, unsigned char* header, unsigned char* id, unsigned char * bam_message, int shadowdirtrack, int usedirtrack)
{
    unsigned int dir = linear_sector(type, dirtrack(type), 0 /* sector */) * BLOCKSIZE;

    /* Clear image */
    memset(image, 0, image_size(type));

    /* Write initial BAM */
    if (type == IMAGE_D81) {
        image[dir + 0x00] = dirtrack(type);
        image[dir + 0x01] = 3;
        image[dir + 0x02] = 0x44;

        image[dir + 0x14] = FILENAMEEMPTYCHAR;
        image[dir + 0x15] = FILENAMEEMPTYCHAR;

        image[dir + 0x1b] = FILENAMEEMPTYCHAR;
        image[dir + 0x1c] = FILENAMEEMPTYCHAR;

        unsigned int bam = linear_sector(type, dirtrack(type), 1 /* sector */) * BLOCKSIZE;
        image[bam + 0x00] = dirtrack(type);
        image[bam + 0x01] = 2;
        image[bam + 0x02] = 0x44;
        image[bam + 0x03] = 0xbb;
        image[bam + 0x06] = 0xc0;

        bam = linear_sector(type, dirtrack(type), 2 /* sector */) * BLOCKSIZE;
        image[bam + 0x00] = 0;
        image[bam + 0x01] = 255;
        image[bam + 0x02] = 0x44;
        image[bam + 0x03] = 0xbb;
        image[bam + 0x06] = 0xc0;
    } else if (type == IMAGE_D80 || type == IMAGE_D82) {
        /* header 39/0 links to the first BAM block */
        image[dir + 0x00] = fmt(type)->bam_block[0].track;
        image[dir + 0x01] = fmt(type)->bam_block[0].sector;
        image[dir + 0x02] = fmt(type)->dos_type;
        image[dir + 0x16] = FILENAMEEMPTYCHAR;
        image[dir + 0x17] = FILENAMEEMPTYCHAR;
        for (int i = 0x1d; i <= 0x20; i++) {
            image[dir + i] = FILENAMEEMPTYCHAR;
        }
        /* BAM blocks: link, 'C', 0, first track, last track + 1;
           the last one links to the first directory block */
        int n = fmt(type)->num_bam_blocks;
        for (int i = 0; i < n; i++) {
            unsigned int bam = linear_sector(type, fmt(type)->bam_block[i].track, fmt(type)->bam_block[i].sector) * BLOCKSIZE;
            image[bam + 0x00] = (i + 1 < n) ? fmt(type)->bam_block[i + 1].track : dirtrack(type);
            image[bam + 0x01] = (i + 1 < n) ? fmt(type)->bam_block[i + 1].sector : first_dir_sector(type);
            image[bam + 0x02] = fmt(type)->dos_type;
            image[bam + 0x04] = i * 50 + 1;
            image[bam + 0x05] = min((i + 1) * 50, (int)image_num_tracks(type)) + 1;
        }
    } else {
        image[dir + 0x00] = dirtrack(type);
        image[dir + 0x01] = 1;
        image[dir + 0x02] = 0x41;
        image[dir + 0x03] = (type == IMAGE_D71) ? 0x80 : 0x00;

        image[dir + 0xa0] = FILENAMEEMPTYCHAR;
        image[dir + 0xa1] = FILENAMEEMPTYCHAR;

        image[dir + 0xa7] = FILENAMEEMPTYCHAR;
        image[dir + 0xa8] = FILENAMEEMPTYCHAR;
        image[dir + 0xa9] = FILENAMEEMPTYCHAR;
        image[dir + 0xaa] = FILENAMEEMPTYCHAR;
    }

    /* Mark all sectors unused */
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        for (int s = 0; s < num_sectors(type, t); s++) {
            mark_sector(type, image, t, s, 1 /* free */);
        }
    }

    /* Reserve space for BAM */
    mark_sector(type, image, dirtrack(type), 0 /* sector */, 0 /* not free */);
    if (type == IMAGE_D71) {
        mark_sector(type, image, fmt(type)->second_bam_track, 0, 0 /* not free */);
        if(!usedirtrack) {
            for (int s = 1; s < num_sectors(type, fmt(type)->second_bam_track); s++) {
                mark_sector(type, image, fmt(type)->second_bam_track, s, 0 /* not free */);
            }
        }
    } else {
        for (int i = 0; i < fmt(type)->num_bam_blocks; i++) {
            mark_sector(type, image, fmt(type)->bam_block[i].track, fmt(type)->bam_block[i].sector, 0 /* not free */);
        }
    }

    /* first dir block */
    unsigned int dirblock = linear_sector(type, dirtrack(type), first_dir_sector(type)) * BLOCKSIZE;
    image[dirblock + SECTORLINKOFFSET] = 255;
    mark_sector(type, image, dirtrack(type), first_dir_sector(type), 0 /* not free */);

    if (shadowdirtrack > 0) {
        dirblock = linear_sector(type, shadowdirtrack, first_dir_sector(type)) * BLOCKSIZE;
        image[dirblock + SECTORLINKOFFSET] = 255;

        mark_sector(type, image, shadowdirtrack, 0 /* sector */, 0 /* not free */);
        mark_sector(type, image, shadowdirtrack, first_dir_sector(type), 0 /* not free */);
    }

    update_directory(type, image, header, id, bam_message, shadowdirtrack);
}

/* Sets image offset to the next DIR entry, returns false when the DIR end was reached */
bool
next_dir_entry(image_type type, const unsigned char* image, int *track, int *sector, int *offset, char *blockmap)
{
    int b = linear_sector(type, *track, *sector); /* assuming here that the given t/s is valid */
    blockmap[b] = 1;
    if (*offset % BLOCKSIZE == 7 * DIRENTRYSIZE) {
        /* last entry in sector */
        int next_track = image[b * BLOCKSIZE + TRACKLINKOFFSET];
        if (next_track == 0) {
            /* this was the last DIR sector */
            return false;
        }
        int next_sector = image[b * BLOCKSIZE + SECTORLINKOFFSET];
        b = linear_sector(type, next_track, next_sector);
        if(b < 0) {
            dir_error = DIR_ILLEGAL_TS;
            return false;
        }
        if(blockmap[b]) {
            dir_error = DIR_CYCLIC_TS;
            return false;
        }
        *track = next_track;
        *sector = next_sector;
        *offset = 0;
    } else {
        *offset += DIRENTRYSIZE;
    }
    return true;
}

/* Compare filenames with wildcards */
bool
match_filename(const unsigned char *pattern, const unsigned char *filename)
{
    for(int c = 0; c < FILENAMEMAXSIZE; c++) {
        if(pattern[c] == 42) { /* PETSCII * */
            return true;
        }
        if((pattern[c] != 63) && (pattern[c] != filename[c])) { /* PETSCII ? or same character */
            return false;
        }
    }
    return true;
}

/* Searches for an existing DIR entry with the given name, returns false if it does not exist */
bool
find_existing_file(image_type type, unsigned char* image, unsigned char* filename, int *index, int *track, int *sector, int *offset)
{
    *track = dirtrack(type);
    *sector = first_dir_sector(type);
    *offset = 0;
    *index = 0;
    bool exists = false;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    do {
        int b = linear_sector(type, *track, *sector) * BLOCKSIZE + *offset;
        int filetype = image[b + FILETYPEOFFSET];
        if(filetype != 0 && match_filename(filename, image + b + FILENAMEOFFSET)) {
            exists = true;
            break;
        }
        ++(*index);
    } while (next_dir_entry(type, image, track, sector, offset, blockmap));

    free(blockmap);
    return exists;
}

/* Returns an empty DIR slot, allocates a new DIR sector if required */
void
new_dir_slot(image_type type, unsigned char* image, int dir_sector_interleave, int shadowdirtrack, int *index, int *dirsector,  int *entry_offset, imagefile files[])
{
    int track = dirtrack(type);
    *dirsector = first_dir_sector(type);
    *entry_offset = 0;
    int lindex = 0;

    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    do {
        int b = linear_sector(type, track, *dirsector) * BLOCKSIZE + *entry_offset;
        if (image[b + FILETYPEOFFSET] == FILETYPEDEL) {
            /* Check if the apparently free slot is used by a new filetype 0 file */
            bool used = false;
            for(int f = 0; f < num_files; f++) {
                if(files[f].direntryindex == lindex) {
                    used = true;
                    break;
                }
            }
            if(!used) {
                *index = lindex;
                free(blockmap);
                return; /* found an empty slot */
            }
        }
        ++lindex;
    } while (next_dir_entry(type, image, &track, dirsector, entry_offset, blockmap));
    free(blockmap);

    /* allocate new dir block */
    int last_sector = *dirsector;
    int next_sector = -1;
    if (alloc_mode != ALLOC_CC1541) {
        /* NXDRBK: like the next block of a file, but only on the directory track */
        int n = num_sectors(type, dirtrack(type));
        int s = last_sector + fmt(type)->dir_interleave;
        if (s > n) {
            s -= n;
            if (s > 0) {
                s--;
            }
        }
        next_sector = dos_getsec(type, image, dirtrack(type), s);
    }
    for (int s = 1; next_sector < 0 && s < num_sectors(type, dirtrack(type)); s++) {
        int sector = (last_sector + s * dir_sector_interleave) % num_sectors(type, dirtrack(type));
        if (is_sector_free(type, image, dirtrack(type), sector, 0, 0)) {
            next_sector = sector;
            break;
        }
    }
    if (next_sector == -1) {
        fprintf(stderr, "ERROR: Dir track full\n");
        exit(-1);
    }
    int b = linear_sector(type, dirtrack(type), last_sector) * BLOCKSIZE;
    image[b + TRACKLINKOFFSET] = dirtrack(type);
    image[b + SECTORLINKOFFSET] = next_sector;

    mark_sector(type, image, dirtrack(type), next_sector, 0 /* not free */);
    b = linear_sector(type, dirtrack(type), next_sector) * BLOCKSIZE;
    memset(image + b, 0, BLOCKSIZE);
    image[b + SECTORLINKOFFSET] = 255;
    *dirsector = next_sector;
    *entry_offset = 0;

    if (shadowdirtrack > 0) {
        b = linear_sector(type, shadowdirtrack, last_sector) * BLOCKSIZE;
        image[b + TRACKLINKOFFSET] = shadowdirtrack;
        image[b + SECTORLINKOFFSET] = next_sector;
        mark_sector(type, image, shadowdirtrack, next_sector, 0 /* not free */);

        b = linear_sector(type, shadowdirtrack, next_sector) * BLOCKSIZE;
        memset(image + b, 0, BLOCKSIZE);
        image[b + SECTORLINKOFFSET] = 255;
    }
    *index = lindex;
}

/* Returns suitable index and offset for given filename (either existing slot when overwriting, first free slot or slot in newly allocated segment) */
bool
find_dir_slot(image_type type, unsigned char* image, unsigned char* filename, int dir_sector_interleave, int shadowdirtrack, int *index, int *dirsector,  int *entry_offset, imagefile files[])
{
    int track;
    if(find_existing_file(type, image, filename, index, &track, dirsector, entry_offset)) {
        return true;
    }
    new_dir_slot(type, image, dir_sector_interleave, shadowdirtrack, index, dirsector, entry_offset, files);
    return false;
}

/* Count number of blocks in file, assumes valid t/s chain */
int
count_blocks(image_type type, unsigned char *image, unsigned int track, int sector)
{
    int num = 0;
    while(track != 0) {
        int b = linear_sector(type, track, sector);
        if(b < 0) {
            break;
        }
        int block_offset = b * BLOCKSIZE;
        track = image[block_offset + TRACKLINKOFFSET];
        sector = image[block_offset + SECTORLINKOFFSET];
        num++;
    };
    return num;
}

/* Overwrites all blocks that are marked as potentially allocated with the given value */
void
mark_sector_chain(image_type type, unsigned char *image, char* atab, unsigned int track, int sector, unsigned int last_track, int last_sector, int mark)
{
    if(track <= 0 || track > image_num_tracks(type) || sector < 0 || sector > num_sectors(type, track)) {
        return;
    }
    while(1) {
        int b = linear_sector(type, track, sector);
        atab[b] = mark;
        if(track == last_track && sector == last_sector) {
            break;
        }
        int block_offset = b * BLOCKSIZE;
        track = image[block_offset + TRACKLINKOFFSET];
        sector = image[block_offset + SECTORLINKOFFSET];
    };
}

/* Validates sector chain starting at track/sector, returns how and in which t/s the chain ends */
int
validate_sector_chain(image_type type, unsigned char* image, char* atab, unsigned int track, int sector, unsigned int *last_track, int *last_sector)
{
    *last_track = track;
    *last_sector = sector;
    if(track == 0 || track > image_num_tracks(type)) {
        return FIRST_BROKEN;
    }
    if(sector >= num_sectors(type, track)) {
        return FIRST_BROKEN;
    }
    if(atab[linear_sector(type, track, sector)] != UNALLOCATED) {
        return FIRST_BROKEN;
    }
    while (1) {
        atab[linear_sector(type, track, sector)] = POTENTIALLYALLOCATED;
        int block_offset = linear_sector(type, track, sector) * BLOCKSIZE;
        unsigned int next_track = image[block_offset + TRACKLINKOFFSET];
        int next_sector = image[block_offset + SECTORLINKOFFSET];
        if (next_track == 0) {
            return VALID; /* end of valid chain */
        }
        if (next_track > image_num_tracks(type)) {
            return ILLEGAL_TRACK;
        }
        if (next_sector >= num_sectors(type, next_track)) {
            return ILLEGAL_SECTOR;
        }
        switch(atab[linear_sector(type, next_track, next_sector)]) {
        case POTENTIALLYALLOCATED:
            return LOOP;
        case ALLOCATED:
            return COLLISION;
        case FILESTART:
            return CHAINED;
        case FILESTART_TRUNCATED:
            return CHAINED_TRUNCATED;
        }
        track = next_track;
        sector = next_sector;
        *last_track = track;
        *last_sector = sector;
    }
}

/* Allocates and initialises a table to track block allocation */
void
init_atab(image_type type, unsigned char* image, char* atab)
{
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int offset = 0;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    do {
        int db = linear_sector(type, dt, ds);
        atab[db] = ALLOCATED;
        int filetype = image[db * BLOCKSIZE + offset + FILETYPEOFFSET] & 0xf;
        if(filetype != FILETYPEDEL) {
            int track = image[db * BLOCKSIZE + offset + FILETRACKOFFSET];
            int sector = image[db * BLOCKSIZE + offset + FILESECTOROFFSET];
            unsigned int last_track;
            int last_sector;
            int error = validate_sector_chain(type, image, atab, track, sector, &last_track, &last_sector);
            if(error != VALID) {
                printf("WARNING: file ");
                print_filename(stdout, &image[db * BLOCKSIZE + offset + FILENAMEOFFSET]);
                printf(" seems corrupt (%s)\n", error_name[error]);
            }
            mark_sector_chain(type, image, atab, track, sector, last_track, last_sector, ALLOCATED);
        }
    } while(next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
}

/* Write atab into BAM */
void
write_atab(image_type type, unsigned char* image, char* atab)
{
    for(unsigned int t = 1; t <= image_num_tracks(type); t++) {
        for(int s = 0; s < num_sectors(type, t); s++) {
            int b = linear_sector(type, t, s);
            int free = (atab[b] == UNALLOCATED);
            if(!free) {
                /* keep allocated BAM entries, only add new ones */
                mark_sector(type, image, t, s, 0);
            }
        }
    }
}

/* Performs strict CBM DOS validation on the image */
void
validate(image_type type, unsigned char* image)
{
    /* create block allocation table */
    char *atab = (char *)calloc(image_num_blocks(type), sizeof(int));
    if (atab == NULL) {
        fprintf(stderr, "ERROR: error allocating memory");
        exit(-1);
    }
    /* check format specifier */
    int format = image[linear_sector(type, dirtrack(type), 0) * BLOCKSIZE + 2];
    if (fmt(type)->dos_type && format != fmt(type)->dos_type) {
        fprintf(stderr, "ERROR: validation failed, format specifier in directory (0x%02x) does not match %s (0x%02x)\n", format, fmt(type)->name, fmt(type)->dos_type);
        exit(-1);
    }
    /* check double sided flag */
    int doublesided = image[linear_sector(type, dirtrack(type), 0) * BLOCKSIZE + 3];
    if (type == IMAGE_D64 && doublesided != 0) {
        fprintf(stderr, "ERROR: validation failed, double sided flag in directory not zero (0x%02x) for D64\n", doublesided);
        exit(-1);
    }
    if (type == IMAGE_D71 && doublesided != 0x80) {
        fprintf(stderr, "ERROR: validation failed, double sided flag in directory not 0x80 (0x%02x) for D71\n", doublesided);
        exit(-1);
    }
    /* check each directory entry and set block allocation table */
    /* set BAM sector as allocated */
    atab[linear_sector(type, dirtrack(type), 0)] = ALLOCATED;
    for (int i = 0; i < fmt(type)->num_bam_blocks; i++) {
        atab[linear_sector(type, fmt(type)->bam_block[i].track, fmt(type)->bam_block[i].sector)] = ALLOCATED;
    }
    if (fmt(type)->second_bam_track) {
        /* 1571: the whole track of the second BAM is allocated */
        for (int s = 0; s < num_sectors(type, fmt(type)->second_bam_track); s++) {
            atab[linear_sector(type, fmt(type)->second_bam_track, s)] = ALLOCATED;
        }
    }
    unsigned int dt = dirtrack(type);
    int dirsector = first_dir_sector(type);
    unsigned int start_track = 1;
    while (start_track != 0) {
        atab[linear_sector(type, dt, dirsector)] = ALLOCATED;
        int dirblock = linear_sector(type, dt, dirsector) * BLOCKSIZE;
        for (int direntry = 0; direntry < DIRENTRIESPERBLOCK; direntry++) {
            int entryOffset = direntry * DIRENTRYSIZE;
            int filetype = image[dirblock + entryOffset + FILETYPEOFFSET] & 0xf;
            if (filetype > 4) {
                fprintf(stderr, "ERROR: validation failed, illegal file type (0x%02x) in directory\n", filetype);
                exit(-1);
            }
            if (filetype != 0) { /* skip deleted entries */
                start_track = image[dirblock + entryOffset + FILETRACKOFFSET];
                int start_sector = image[dirblock + entryOffset + FILESECTOROFFSET];
                if (start_track == 0 || start_track > image_num_tracks(type)) {
                    fprintf(stderr, "ERROR: validation failed, illegal track reference (%u) in directory\n", start_track);
                    exit(-1);
                }
                if (start_sector >= num_sectors(type, start_track)) {
                    fprintf(stderr, "ERROR: validation failed, illegal sector reference (track %u, sector %d) in directory\n", start_track, start_sector);
                    exit(-1);
                }
                if (atab[linear_sector(type, start_track, start_sector)] == ALLOCATED) {
                    fprintf(stderr, "ERROR: validation failed, file starts in the middle of another file (track %u, sector %d)\n", start_track, start_sector);
                    exit(-1);
                }
                if (atab[linear_sector(type, start_track, start_sector)] != FILESTART) { /* loop files are allowed */
                    unsigned int error_track;
                    int error_sector;
                    int result = validate_sector_chain(type, image, atab, start_track, start_sector, &error_track, &error_sector);
                    switch(result) {
                    case ILLEGAL_TRACK:
                        fprintf(stderr, "ERROR: validation failed, illegal track reference in file sector chain at track %d, sector %d\n", error_track, error_sector);
                        exit(-1);
                    case ILLEGAL_SECTOR:
                        fprintf(stderr, "ERROR: validation failed, illegal sector reference in file sector chain at track %d, sector %d\n", error_track, error_sector);
                        exit(-1);
                    case LOOP:
                        fprintf(stderr, "ERROR: validation failed, loop in file sector chain at track %d, sector %d\n", error_track, error_sector);
                        exit(-1);
                    case COLLISION:
                    case CHAINED:
                        fprintf(stderr, "ERROR: validation failed, collision with existing file in file sector chain at track %d, sector %d\n", error_track, error_sector);
                        exit(-1);
                    }
                    atab[linear_sector(type, start_track, start_sector)] = FILESTART;
                    mark_sector_chain(type, image, atab, start_track, start_sector, error_track, error_sector, ALLOCATED);
                }
            }
        }
        dt = image[dirblock + TRACKLINKOFFSET];
        dirsector = image[dirblock + SECTORLINKOFFSET];
        if (dt == 0) {
            break;
        }
    }
    /* check BAM for consistency with block allocation table */
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        unsigned char* bitmap = image + get_bam_offset(type, t);
        int num_free = 0;
        int exp_free;
        for (int s = 0; s < num_sectors(type, t); s++) {
            int atab_used = (atab[linear_sector(type, t, s)] != UNALLOCATED);
            int bam_used = ((bitmap[s >> 3] & (1 << (s & 7))) == 0);
            num_free += (1 - bam_used);
            if (bam_used != atab_used) {
                fprintf(stderr, "ERROR: validation failed, BAM (%s) is not consistent with files (%s) for track %u sector %d\n", bam_used ? "used" : "free", atab_used ? "used" : "free", t, s);
                exit(-1);
            }
        }
        exp_free = image[get_bam_count_offset(type, t)];
        if (exp_free != num_free) {
            fprintf(stderr, "ERROR: validation failed, BAM number of free blocks (%d) is not consistent with bitmap (%#02x%#02x%#02x) for track %u\n", exp_free, *bitmap, *(bitmap + 1), *(bitmap + 2), t);
            exit(-1);
        }
    }
    free(atab);
    if(!quiet) {
        fprintf(stderr, "CBM DOS validation passed\n");
    }
}
