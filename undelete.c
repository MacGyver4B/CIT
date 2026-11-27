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

/* undelete.c: restoring deleted and formatted files */

#include "cit.h"

/* Generates a unique filename, either based on the proposed name, or using track and sector. */
static void
generate_unique_filename(image_type type, unsigned char *image, unsigned char *name, int track, int sector, int start, char marker)
{
    int i, t, s, o;
    if(name[0] == 0xa0 || name[0] == 0) {
        /* no name provided, create one */
        name[0] = a2p('t');
        pputnum2(name+1, track);
        name[3] = a2p('s');
        pputnum2(name+4, sector);
        name[6] = a2p('$');
        pputhex(name+7, start);
        for(int pos = 11; pos < FILENAMEMAXSIZE; pos++) {
            name[pos] = 0xa0;
        }
    }
    int marker_pos = pstrlen(name);
    if(marker_pos > FILENAMEMAXSIZE-1) {
        marker_pos = FILENAMEMAXSIZE-1;
    }
    if(marker) {
        name[marker_pos] = marker;
    } else {
        marker_pos++;
    }
    int appendix = 1;
    int appendix_len = 2;
    int namelen = pstrlen(name);
    while(find_existing_file(type, image, name, &i, &t, &s, &o)) {
        marker_pos = namelen + appendix_len;
        if(marker_pos > FILENAMEMAXSIZE-1) {
            marker_pos = FILENAMEMAXSIZE-1;
            if(!marker) {
                marker_pos++;
            }
        }
        if(marker) {
            name[marker_pos] = marker;
        }
        int appendix_pos = marker_pos - appendix_len;
        name[appendix_pos] = '.';
        pputnum(name + appendix_pos + 1, appendix);
        appendix++;
        if(appendix == 10) {
            appendix_len++;
        }
        if(appendix == 100) {
            appendix_len++;
        }
    }
}

/* Try to undelete a file given the directory entry, returns true if successful */
static bool
undelete_file(image_type type, unsigned char* image, int dt, int ds, int offset, char* atab, int level)
{
    unsigned int last_track;
    int last_sector;
    char marker = 0;
    int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE;
    int track = image[dirblock + offset + FILETRACKOFFSET];
    int sector = image[dirblock + offset + FILESECTOROFFSET];
    int error = validate_sector_chain(type, image, atab, track, sector, &last_track, &last_sector);
    if(error == VALID || ((level == RESTORE_DIR_ONLY || level >= RESTORE_INVALID_FILES) && error != FIRST_BROKEN)) {
        unsigned char name[17];
        if(error != VALID) {
            if(level != RESTORE_DIR_ONLY) {
                /* terminate chain */
                int block_offset = linear_sector(type, last_track, last_sector) * BLOCKSIZE;
                image[block_offset + TRACKLINKOFFSET] = 0;
                marker = '<';
            }
        }
        /* restore dir entry */
        memcpy(&name, &image[dirblock + offset + FILENAMEOFFSET], 16);
        name[16] = 0;
        int b = linear_sector(type, track, sector);
        int address = image[b * BLOCKSIZE + 2] + 256 * image[b * BLOCKSIZE + 3];
        generate_unique_filename(type, image, name, track, sector, address, marker);
        memcpy(&image[dirblock + offset + FILENAMEOFFSET], &name, 16);
        image[dirblock + offset + FILETYPEOFFSET] = 0x82; /* original file type is lost, use closed PRG instead */
        mark_sector_chain(type, image, atab, track, sector, last_track, last_sector, ALLOCATED);
        if(level != RESTORE_DIR_ONLY) {
            int size = count_blocks(type, image, track, sector);
            image[dirblock + offset + FILEBLOCKSHIOFFSET] = size / 256;
            image[dirblock + offset + FILEBLOCKSLOOFFSET] = size % 256;
        }
        return true;
    }
    if(error != FIRST_BROKEN) {
        mark_sector_chain(type, image, atab, track, sector, last_track, last_sector, UNALLOCATED);
    }
    return false;
}

/* search for scratched directory entries and restore them */
static int
undelete(image_type type, unsigned char* image, char* atab, int level)
{
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int nsectors = num_sectors(type, dt);
    int offset = 0;
    int num_undeleted = 0;
    int final_dt, final_ds; /* last linked directory sector and track */
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    bool* searched = calloc(nsectors, sizeof(bool));
    if(searched == NULL) {
        fprintf(stderr, "ERROR: error allocating memory");
        exit(-1);
    }

    /* go through directory sector chain */
    do {
        if(dt == dirtrack(type)) {
            searched[ds] = true;
        }
        final_dt = dt;
        final_ds = ds;
        int db = linear_sector(type, dt, ds);
        mark_sector(type, image, dt, ds, 0); /* needs to be updated in BAM, so that dir allocation works correctly later */
        atab[db] = ALLOCATED;
        int dirblock = db * BLOCKSIZE;
        int filetype = image[dirblock + offset + FILETYPEOFFSET];
        if(filetype == FILETYPEDEL && image[dirblock + offset + FILENAMEOFFSET] != 0 && undelete_file(type, image, dt, ds, offset, atab, level)) { /* filename starting with 0 means that likely there was no file */
            num_undeleted++;
        }
    } while(next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);

    /* search for unlinked dir sectors */
    dt = dirtrack(type);
    for(ds = first_dir_sector(type); ds < nsectors; ds++) {
        if(!searched[ds]) {
            int found = 1;
            for(offset = 0; offset < DIRENTRIESPERBLOCK*DIRENTRYSIZE; offset += DIRENTRYSIZE) {
                if(!undelete_file(type, image, dt, ds, offset, atab, level)) {
                    found = 0;
                    break;
                }
            }
            if(found) {
                num_undeleted += 8;
                if(!quiet) {
                    printf("Relinking directory sector %d\n", ds);
                }
                /* link found dir sector */
                int block_offset = linear_sector(type, final_dt, final_ds) * BLOCKSIZE;
                image[block_offset + TRACKLINKOFFSET] = dt;
                image[block_offset + SECTORLINKOFFSET] = ds;
                /* terminate found dir sector */
                int db = linear_sector(type, dt, ds);
                block_offset = db * BLOCKSIZE;
                image[block_offset + TRACKLINKOFFSET] = 0;
                final_dt = dt;
                final_ds = ds;
                mark_sector(type, image, dt, ds, 0); /* needs to be updated in BAM, so that dir allocation works correctly later */
                atab[db] = ALLOCATED;
            }
        }
    }
    free(searched);
    write_atab(type, image, atab);
    return num_undeleted;
}

/* add new DIR entries for wild chains */
static void
add_wild_to_dir(image_type type, unsigned char* image, char* atab, imagefile files[])
{
    /* create a DIR entry for each FILESTART */
    for(unsigned int t = 1; t <= image_num_tracks(type); t++) {
        for(int s = 0; s < num_sectors(type, t); s++) {
            int b = linear_sector(type, t, s);
            if(atab[b] == FILESTART || atab[b] == FILESTART_TRUNCATED) {
                char marker = atab[b] == FILESTART ? 0 : '<';
                unsigned char name[17];
                name[0] = 0xa0;
                int dir_index, dir_sector, dir_offset;
                atab[b] = ALLOCATED;
                new_dir_slot(type, image, fmt(type)->dir_interleave, 0, &dir_index, &dir_sector, &dir_offset, files); /* TODO: handle full directory more gracefully */
                int db = linear_sector(type, dirtrack(type), dir_sector);
                atab[db] = ALLOCATED; /* make sure that potentially new dir block is marked as used */
                int offset = db * BLOCKSIZE + dir_offset;
                image[offset + FILETYPEOFFSET] = 0x82; /* closed PRG */
                image[offset + FILETRACKOFFSET] = t;
                image[offset + FILESECTOROFFSET] = s;
                image[offset + FILENAMEOFFSET] = 0xa0; /* no proposed filename */
                int address = image[b * BLOCKSIZE + 2] + 256 * image[b * BLOCKSIZE + 3];
                generate_unique_filename(type, image, name, t, s, address, marker);
                memcpy(&image[offset + FILENAMEOFFSET], name, 16);
                int size = count_blocks(type, image, t, s);
                image[offset + FILEBLOCKSHIOFFSET] = size / 256;
                image[offset + FILEBLOCKSLOOFFSET] = size % 256;
            }
        }
    }
}

/* search for wild valid chains of unallocated sectors */
static int
undelete_wild(image_type type, unsigned char* image, char* atab, int level, imagefile files[])
{
    int num_undeleted = 0;
    int max_bam_sector = fmt(type)->last_bam_sector;
    unsigned int dt = dirtrack(type);
    /* search for well terminated sector chains */
    for(unsigned int t = 1; t <= image_num_tracks(type); t++) {
        for(int s = 0; s < num_sectors(type, t); s++) {
            int b = linear_sector(type, t, s);
            if(atab[b] == UNALLOCATED && (t != dt || s > max_bam_sector)) { /* ignore bam sectors */
                unsigned int last_track;
                int last_sector;
                int error = validate_sector_chain(type, image, atab, t, s, &last_track, &last_sector);
                int last_block = linear_sector(type, last_track, last_sector);
                if(error == CHAINED || error == CHAINED_TRUNCATED) {
                    mark_sector_chain(type, image, atab, t, s, last_track, last_sector, ALLOCATED);
                    atab[b] = (error == CHAINED) ? FILESTART : CHAINED_TRUNCATED;
                    int chained_track = image[last_block * BLOCKSIZE + TRACKLINKOFFSET];
                    int chained_sector = image[last_block * BLOCKSIZE + SECTORLINKOFFSET];
                    int chained_block = linear_sector(type, chained_track, chained_sector);
                    atab[chained_block] = ALLOCATED; /* overwrite FILESTART */
                } else if(error == VALID) {
                    if(last_track == t && last_sector == s) {
                        if(level == RESTORE_INVALID_SINGLES) {
                            /* single block files should have all 0 after file end */
                            bool valid = false;
                            int last_byte = image[last_block * BLOCKSIZE + SECTORLINKOFFSET];
                            if(image[last_block * BLOCKSIZE + TRACKLINKOFFSET] == 0 && last_byte >= 2) {
                                valid = true;
                                for(int i = last_byte + 1; i <= 255; i++) {
                                    if(image[last_block * BLOCKSIZE + i] != 0) {
                                        valid = false;
                                        break;
                                    }
                                }
                            }
                            if(valid) {
                                atab[b] = FILESTART;
                                num_undeleted++;
                            } else {
                                atab[b] = UNALLOCATED;
                            }
                            atab[b] = valid ? FILESTART : UNALLOCATED;
                        } else {
                            atab[b] = UNALLOCATED;
                        }
                    } else {
                        mark_sector_chain(type, image, atab, t, s, last_track, last_sector, ALLOCATED);
                        atab[b] = FILESTART;
                        image[last_block * BLOCKSIZE + TRACKLINKOFFSET] = 0;
                        num_undeleted++;
                    }
                } else {
                    if(error != FIRST_BROKEN) {
                        mark_sector_chain(type, image, atab, t, s, last_track, last_sector, UNALLOCATED);
                    }
                }
            }
        }
    }
    if(num_undeleted) {
        write_atab(type, image, atab);
        add_wild_to_dir(type, image, atab, files);
    }
    return num_undeleted;
}

/* search for wild invalid chains of unallocated sectors and fix them */
static int
undelete_fix_wild(image_type type, unsigned char* image, char* atab, imagefile files[])
{
    int num_undeleted = 0;
    int max_bam_sector = fmt(type)->last_bam_sector;
    unsigned int dt = dirtrack(type);
    /* search for broken sector chains */
    for(unsigned int t = 1; t <= image_num_tracks(type); t++) {
        for(int s = 0; s < num_sectors(type, t); s++) {
            int b = linear_sector(type, t, s);
            if(atab[b] == UNALLOCATED && (t != dt || s > max_bam_sector)) { /* ignore bam sectors */
                unsigned int last_track;
                int last_sector;
                int error = validate_sector_chain(type, image, atab, t, s, &last_track, &last_sector);
                int last_block = linear_sector(type, last_track, last_sector);
                if(error == CHAINED || error == CHAINED_TRUNCATED) {
                    mark_sector_chain(type, image, atab, t, s, last_track, last_sector, ALLOCATED);
                    atab[b] = (error == CHAINED) ? FILESTART : FILESTART_TRUNCATED;
                    int chained_track = image[last_block * BLOCKSIZE + TRACKLINKOFFSET];
                    int chained_sector = image[last_block * BLOCKSIZE + SECTORLINKOFFSET];
                    int chained_block = linear_sector(type, chained_track, chained_sector);
                    atab[chained_block] = ALLOCATED; /* overwrite FILESTART */
                } else if(t != last_track || s != last_sector) {
                    mark_sector_chain(type, image, atab, t, s, last_track, last_sector, ALLOCATED);
                    atab[b] = FILESTART_TRUNCATED;
                    image[last_block * BLOCKSIZE + TRACKLINKOFFSET] = 0;
                    image[last_block * BLOCKSIZE + SECTORLINKOFFSET] = 255;
                    num_undeleted++;
                } else {
                    if(error != FIRST_BROKEN) {
                        mark_sector_chain(type, image, atab, t, s, last_track, last_sector, UNALLOCATED);
                    }
                }
            }
        }
    }
    if(num_undeleted) {
        write_atab(type, image, atab);
        add_wild_to_dir(type, image, atab, files);
    }
    return num_undeleted;
}

/* Tries to restore any deleted or formatted files */
void
restore(image_type type, unsigned char* image, int level, imagefile files[])
{
    int num_undeleted = 0;
    /* create block allocation table */
    char *atab = (char *)calloc(image_num_blocks(type), sizeof(char));
    if (atab == NULL) {
        fprintf(stderr, "ERROR: error allocating memory");
        exit(-1);
    }
    init_atab(type, image, atab);
    if(level == RESTORE_DIR_ONLY) {
        num_undeleted += undelete(type, image, atab, RESTORE_DIR_ONLY);
    } else {
        num_undeleted += undelete(type, image, atab, RESTORE_VALID_FILES);
        if(level >= RESTORE_VALID_CHAINS) {
            num_undeleted += undelete_wild(type, image, atab, RESTORE_VALID_CHAINS, files);
        }
        if(level >= RESTORE_INVALID_FILES) {
            num_undeleted += undelete(type, image, atab, RESTORE_INVALID_FILES);
        }
        if(level >= RESTORE_INVALID_CHAINS) {
            num_undeleted += undelete_fix_wild(type, image, atab, files);
        }
        if(level >= RESTORE_INVALID_SINGLES) {
            num_undeleted += undelete_wild(type, image, atab, RESTORE_INVALID_SINGLES, files);
        }
    }
    free(atab);
    if(num_undeleted) {
        modified = 1;
    }
    if(!quiet) {
        printf("%d files undeleted", num_undeleted);
        if(num_undeleted) {
            printf(", '<' at filename end marks truncated files");
        }
        printf("\n");
    }
}
