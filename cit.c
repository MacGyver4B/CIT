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

/* cit.c: command line, writing and listing files */

#include "cit.h"

static const char *filetypename_uc[] = {
    "DEL", "SEQ", "PRG", "USR", "REL", "CBM", "???", "???",
    "???", "???", "???", "???", "???", "???", "???", "???"
};

static const char *filetypename_lc[] = {
    "del", "seq", "prg", "usr", "rel", "cbm", "???", "???",
    "???", "???", "???", "???", "???", "???", "???", "???"
};

static const char *dir_error_string[] = {
    "no error", "illegal track or sector in directory sector chain", "cycle in directory sector chain"
};

int quiet           = 0;      /* global quiet flag */
int verbose         = 0;      /* global verbose flag */
int num_files       = 0;      /* number of files to be written provided by the user */
int max_hash_length = 16;     /* number of bytes of the filenames to calculate the hash over */
int unicode         = 0;      /* which unicode mapping to use: 0 = none, 1 = upper case, 2 = lower case */
int modified        = 0;      /* image needs to be written */
int dir_error       = DIR_OK; /* directory has an error */

/* Prints the command line help */
static void
usage(void)
{
    printf("\n*** This is CIT (Commodore Image Tools) version " VERSION " built on " __DATE__ " ***\n");
    printf("*** based on cc1541 4.2 by JackAsser, Krill, Claus and Bjoern Esser ***\n\n");
    printf("Usage: cit [options] image.[d64|d71|d81|d80|d82]\n\n");
    printf("Disk Related:\n");
    printf("============\n");
    printf("-n diskname   Disk name, default='cit'.\n");
    printf("-i id         Disk ID, default='00 2a'.\n");
    printf("-D format     Image format, overrides the file name extension:\n");
    printf("              d40  D64 for 2031/4040 (no Transwarp, 35 tracks)\n");
    printf("              d41  D64 for the 1541 (also d64, default for .d64)\n");
    printf("              d71  1571, d81 1581, d80 8050, d82 8250/SFD-1001\n");
    printf("-4            Use tracks 35-40 with SPEED DOS BAM formatting.\n");
    printf("-5            Use tracks 35-40 with DOLPHIN DOS BAM formatting.\n");
    printf("-d track      Maintain a shadow directory (copy of the actual directory without\n");
    printf("              a valid BAM).\n");
    printf("-g filename   Write additional g64 output file with given name.\n");
    printf("-H message    Hidden BAM message. Only for D64 (up to 85 chars) or SPEED DOS\n");
    printf("              (up to 20 chars).\n");
    printf("-p t,s,o,file Write binary patch from file to track t, sector s, offset o.\n");
    printf("-V            Do not modify image unless it is in valid CBM DOS format.\n\n");
    printf("File Related:\n");
    printf("============\n");
    printf("-w localname  Write local file to disk, if filename is not set then the\n");
    printf("              local name is used. After file written, the filename is unset.\n");
    printf("-f filename   Use filename as name when writing next file, use prefix # to\n");
    printf("              include arbitrary PETSCII characters (e.g. -f \"start#a0,8,1\").\n");
    printf("-T filetype   Filetype for next file, allowed parameters are PRG, SEQ, USR, REL\n");
    printf("              and DEL, or a decimal number between 0 and 255. Default is PRG.\n");
    printf("-P            Set write protect flag for next file.\n");
    printf("-O            Set open flag for next file.\n");
    printf("-B numblocks  Write the given value as file size in blocks to the directory for\n");
    printf("              the next file. Not applicable for Transwarp files.\n");
    printf("-L            Add dir entry without writing file (track and sector will be 0),\n");
    printf("              requires a filename given with -f.\n");
    printf("-l filename   Write loop file (an additional dir entry) to existing file to\n");
    printf("              disk. Wildcards * and ? are supported. Set filename with -f.\n");
    printf("-N            Force creation of a new directory entry, even if a file with the\n");
    printf("              same name exists already.\n");
    printf("-o            Do not overwrite if file with same name exists already.\n");
    printf("-X filename   Extract file from disk image. Wildcards * and ? are supported.\n\n");
    printf("Track/Sector Control:\n");
    printf("====================\n");
    printf("-S value      Default sector interleave, default=10. Not applicable for D81.\n");
    printf("-s value      Next file sector interleave, valid after each file.\n");
    printf("              The interleave value falls back to the default value set by -S\n");
    printf("              after the first sector of the next file. Not applicable for D81\n");
    printf("              or Transwarp files.\n");
    printf("-e            Start next file on an empty track (default start sector is\n");
    printf("              current sector plus interleave).\n");
    printf("-E            Try to fit file on a single track.\n");
    printf("-r track      Restrict next file blocks to the specified track or higher.\n");
    printf("-b sector     Set next file beginning sector to the specified value.\n");
    printf("              Not applicable for D81 or Transwarp files.\n");
    printf("-F value      Next file first sector on a new track (default=0).\n");
    printf("              Any negative value assumes aligned tracks and uses current\n");
    printf("              sector + interleave - value. After each file, the value falls\n");
    printf("              back to the default. Not applicable for D81.\n");
    printf("-t            Use directory track to also store files (makes -x useless)\n");
    printf("              (default no).\n");
    printf("-u numblocks  When using -t, amount of dir blocks to leave free (default=2).\n");
    printf("-x            Don't split files over directory track hole (default split files).\n");
    printf("-c            Save next file cluster-optimized (d71 only).\n\n");
    printf("Fastloader Specific:\n");
    printf("===================\n");
    printf("-M numchars   Hash computation maximum filename length, this must\n");
    printf("              match loader option FILENAME_MAXLENGTH in Krill's loader.\n");
    printf("              Default is 16.\n");
    printf("-m            Ignore filename hash collisions, without this switch a collision\n");
    printf("              results in an error.\n");
    printf("-W localname  Like -w, but encode file in Transwarp format.\n");
    printf("              Provide Transwarp bootfile as last file using\n");
    printf("              \"-f 'transwarp vX.YZ' -w 'transwarp vX.YZ.prg'\"\n");
    printf("-A value      Select allocation strategy for Transwarp files (default 1).\n");
    printf("              0: allocate in the order given on the commandline.\n");
    printf("              1: save disk space by reordering transwarp files.\n");
    printf("-K key        Set an encryption key for Transwarp files, a string of up to 29\n");
    printf("              characters.\n\n");
    printf("Miscellaneous:\n");
    printf("=============\n");
    printf("-h            Print this command line help.\n");
    printf("-v            Be verbose.\n");
    printf("-q            Be quiet.\n");
    printf("-U mapping    Print PETSCII as Unicode (requires Unicode 13.0 font, e.g.\n");
    printf("              UNSCII). Use mapping 0 for ASCII output, 1 for upper case, 2 for\n");
    printf("              lower case, default is 0.\n");
    printf("-R level      Try to restore deleted and formatted files.\n");
    printf("              level 0: Only restore dir entries without touching any t/s links.\n");
    printf("              level 1: Fix dir entries for files with valid t/s chains.\n");
    printf("              level 2: Also add wild sector chains with valid t/s chains.\n");
    printf("              level 3: Also fix dir entries with invalid t/s chains.\n");
    printf("              level 4: Also add and fix wild invalid t/s chains.\n");
    printf("              level 5: Also add reasonable wild single blocks.\n");
    printf("-a            Print command line options that would create the same directory as\n");
    printf("              the one in the given image (for directory art import).\n");
    printf("\n");
    printf("Project page: https://github.com/MacGyver4B/CIT\n");
    printf("\n");

    exit(-1);
}

/* Returns a pointer to the filename in a path */
static const unsigned char*
basename(const unsigned char* path)
{
    const unsigned char* name;
    (name = (unsigned char*)strrchr((char *)path, FILESEPARATOR)) ? ++name : (name = path);
    return name;
}

/* Deletes a file from disk and BAM, but leaves the directory entry */
static void
wipe_file(image_type type, unsigned char* image, imagefile* file)
{
    int b = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;

    if (is_transwarp_file(image, b)) {
        int start_track;
        int end_track;
        int low_track;
        int high_track;
        int filesize = transwarp_stat(type, image, b, &start_track, &end_track, &low_track, &high_track);
        if (filesize <= 0) {
            fprintf(stderr, "ERROR: Cannot overwrite Transwarp file ");
            print_filename(stderr, file->pfilename);
            fprintf(stderr, "\n");

            exit(-1);
        }

        for (int track = low_track; track <= high_track; ++track) {
            for (int sector = 0; sector < num_sectors(type, track); ++sector) {
                int block_offset = linear_sector(type, track, sector) * BLOCKSIZE;
                memset(image + block_offset, 0, BLOCKSIZE);
                mark_sector(type, image, track, sector, 1 /* free */);
            }
        }

        return;
    }

    unsigned int track = image[b + FILETRACKOFFSET];
    unsigned int sector = image[b + FILESECTOROFFSET];

    if (sector >= 0x80) {
        return; /* loop file */
    }

    while (track != 0) {
        int block_offset = linear_sector(type, track, sector) * BLOCKSIZE;
        int next_track = image[block_offset + TRACKLINKOFFSET];
        int next_sector = image[block_offset + SECTORLINKOFFSET];
        memset(image + block_offset, 0, BLOCKSIZE); /* this also fixes any cyclic t/s chain */
        mark_sector(type, image, track, sector, 1 /* free */);
        track = next_track;
        sector = next_sector;
    }
}

/* Adds the specified new entries to the directory */
static void
create_dir_entries(image_type type, unsigned char* image, imagefile* files, int num_files, int dir_sector_interleave, unsigned int shadowdirtrack, int nooverwrite)
{
    /* this does not check for uniqueness of filenames */

    int num_overwritten_files = 0;

    if (verbose && num_files > 0) {
        printf("\nCreating dir entries:\n");
    }

    for (int i = 0; i < num_files; i++) {
        /* find or create slot */
        imagefile *file = files + i;

        if (verbose) {
            printf("  ");
            print_dirfilename(file->pfilename);
        }

        if(file->force_new) {
            new_dir_slot(type, image, dir_sector_interleave, shadowdirtrack, &file->direntryindex, &file->direntrysector, &file->direntryoffset, files);
        } else if (find_dir_slot(type, image, file->pfilename, dir_sector_interleave, shadowdirtrack, &file->direntryindex, &file->direntrysector, &file->direntryoffset, files)) {
            if (nooverwrite) {
                fprintf(stderr, "ERROR: Filename exists on disk image already and -o was set\n");
                exit(-1);
            }

            wipe_file(type, image, file);
            num_overwritten_files++;
        }

        int file_entry_offset = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;
        image[file_entry_offset + FILETYPEOFFSET] = file->filetype & 0xff;
        if (verbose && (file->filetype & FILETYPETRANSWARPMASK)) {
            printf(" [Transwarp]");
        }
        memcpy(image + file_entry_offset + FILENAMEOFFSET, file->pfilename, FILENAMEMAXSIZE);

        if (is_transwarp_bootfile(image, file_entry_offset)) {
            if (file->filetype & FILETYPETRANSWARPMASK) {
                if (verbose) {
                    printf("\n");
                }

                fprintf(stderr, "ERROR: Attempt to write Transwarp bootfile as Transwarp file\n");

                exit(-1);
            }

            file->transwarp_boot = true;
            if (verbose) {
                printf(" [Transwarp bootfile]");
            }

            if (i != 0) {
                // allocate Transwarp bootfile first
                if (files[0].transwarp_boot) {
                    if (verbose) {
                        printf("\n");
                    }

                    fprintf(stderr, "ERROR: Multiple Transwarp bootfiles\n");
                    exit(-1);
                }

                imagefile transwarp_bootfile = *file;
                for (int j = i; j > 0; --j) {
                    files[j] = files[j - 1];
                }
                files[0] = transwarp_bootfile;
                file = files;
            }
        }

        if (shadowdirtrack > 0) {
            file_entry_offset = linear_sector(type, shadowdirtrack, file->direntrysector) * BLOCKSIZE + file->direntryoffset;
            image[file_entry_offset + FILETYPEOFFSET] = file->filetype;
            memcpy(image + file_entry_offset + FILENAMEOFFSET, file->pfilename, FILENAMEMAXSIZE);
        }

        if (verbose) {
            printf("\n");
        }
    }

    if (!quiet && (num_overwritten_files > 0)) {
        printf("%d out of %d files exist and will be overwritten\n", num_overwritten_files, num_files);
    }
}

/* Prints the allocated tracks and sectors for every file */
static void
print_file_allocation(image_type type, const unsigned char* image, imagefile* files, int num_files)
{
    if (num_files <= 0) {
        imagefile existing_files[144];
        memset(existing_files, 0, sizeof existing_files);

        int t = dirtrack(type);
        int s = first_dir_sector(type);
        int o = 0;
        char *blockmap = calloc(image_num_blocks(type), sizeof(char));
        if(blockmap == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }

        do {
            int b = linear_sector(type, t, s) * BLOCKSIZE + o;
            int filetype = image[b + FILETYPEOFFSET] & 0xf;
            switch (filetype) {
            case FILETYPEPRG: {
                int track = image[b + FILETRACKOFFSET];
                int sector = image[b + FILESECTOROFFSET];
                int numsectors = image[b + FILEBLOCKSLOOFFSET] + 256 * image[b + FILEBLOCKSHIOFFSET];
                const unsigned char *filename = image + b + FILENAMEOFFSET;
                b = linear_sector(type, track, sector);
                if (b >= 0) {
                    memcpy(existing_files[num_files].pfilename, filename, FILENAMEMAXSIZE);
                    existing_files[num_files].track = track;
                    existing_files[num_files].sector = sector;
                    existing_files[num_files].direntryindex = num_files;
                    existing_files[num_files].direntrysector = s;
                    existing_files[num_files].direntryoffset = o;
                    existing_files[num_files].nrSectors = numsectors;

                    if (is_transwarp_file(image, b)) {
                        existing_files[num_files].filetype = filetype | FILETYPETRANSWARPMASK;
                        existing_files[num_files].sectorInterleave = 1;

                        int start_track;
                        int end_track;
                        int low_track;
                        int high_track;
                        int filesize = transwarp_stat(type, image, b, &start_track, &end_track, &low_track, &high_track);

                        existing_files[num_files].size = filesize;

                        existing_files[num_files].track = start_track;
                        existing_files[num_files].last_track = end_track;

                        ++num_files;

                        break;
                    }

                    while (true) {
                        b = linear_sector(type, track, sector);
                        if(b < 0) {
                            break; /* TODO: print info about illegal t/s */
                        }
                        int offset = b * BLOCKSIZE;
                        int next_track = image[offset + 0];
                        int next_sector = image[offset + 1];
                        if ((track == 0) || (next_track == 0)) {
                            break;
                        }
                        if ((track == next_track) && (next_sector > sector)) {
                            existing_files[num_files].sectorInterleave = next_sector - sector;
                            break;
                        }
                        track = next_track;
                        sector = next_sector;
                    }

                    ++num_files;

                    break;
                }
            }

            default:
                break;
            }
        } while (next_dir_entry(type, image, &t, &s, &o, blockmap));
        free(blockmap);
        files = existing_files;
    }

    if(num_files > 0) {
        printf("\nFile allocation:\n");
    }

    for (int i = 0; i < num_files; i++) {
        printf("%02d/%02d %3d (0x%02x 0x%02x:0x%02x) ", files[i].track, files[i].sector, files[i].nrSectors & 0xffff,
               files[i].direntryindex, files[i].direntrysector, files[i].direntryoffset);
        if (files[i].alocalname) {
            printf("\"%s\" => ", files[i].alocalname);
        }
        print_filename(stdout, files[i].pfilename);
        printf(" (SL: %d)", files[i].sectorInterleave);

        if (files[i].loop_file && (files[i].sectorInterleave != 0)) {

            continue;
        }

        if (files[i].filetype & FILETYPETRANSWARPMASK) {
            int transwarp_blocks;
            int nonredundant_blocks;
            int redundant_blocks;
            int nonredundant_blocks_on_last_track;
            int spare_bytes = transwarp_size(type, files[i].track, files[i].last_track, files[i].size, &transwarp_blocks, &nonredundant_blocks, &redundant_blocks, &nonredundant_blocks_on_last_track);

            int num_blocks = 0;
            int filesize = files[i].size + 2;
            while (filesize > 0) {
                ++num_blocks;
                filesize -= 254;
            }

            printf("\n          Transwarp: %d total/%d actual (%d standard) blocks, tracks %d-%d, %d used and %d redundant block%s on last track, 0x%x spare bytes in last block, size 0x%x\n",
                   transwarp_blocks, nonredundant_blocks, num_blocks,
                   (files[i].track < files[i].last_track) ? files[i].track : files[i].last_track,
                   (files[i].track >= files[i].last_track) ? files[i].track : files[i].last_track,
                   nonredundant_blocks_on_last_track, redundant_blocks, (redundant_blocks == 1) ? "" : "s",
                   spare_bytes, files[i].size);

            continue;
        }

        int track = files[i].track;
        int sector = files[i].sector;

        bool firsttrack = true;
        int firstsector = sector;
        bool fileblocks[MAXSECTORS];
        memset(fileblocks, 0, sizeof fileblocks);
        fileblocks[sector] = true;

        int j = 0;
        while (track != 0) {
            if (j == 0) {
                printf("\n          ");
            }
            printf("%02d/%02d", track, sector);
            int b = linear_sector(type, track, sector);
            if(b < 0) {
                break; /* TODO: print info about illegal t/s link? */
            }
            int offset = b * BLOCKSIZE;
            int next_track = image[offset + 0];
            int next_sector = image[offset + 1];
            if ((track != next_track) && (next_track != 0)) {
                /* track change */
                if (next_sector != 0) {
                    /* interleave violation */
                    printf("!-");
                } else {
                    printf(" -");
                }
            } else if ((next_sector < sector) && (next_track != 0)) {
                /* sector wrap */
                int expected_next_sector = ((sector + abs(files[i].sectorInterleave)) % num_sectors(type, track));
                bool on_nonempty_firsttrack = (expected_next_sector < next_sector) && firsttrack && (firstsector != 0);
                if ((expected_next_sector != next_sector) && (!on_nonempty_firsttrack)) {
                    while ((expected_next_sector < next_sector) && fileblocks[expected_next_sector]) {
                        ++expected_next_sector;
                    }
                    if (expected_next_sector != next_sector) {
                        /* interleave violation */
                        printf("!.");
                    } else {
                        printf(" .");
                    }
                } else {
                    printf(" .");
                }
            } else if (((next_sector - sector) != abs(files[i].sectorInterleave)) && (next_track != 0)) {
                /* interleave violation */
                printf(" !");
            } else {
                printf("  ");
            }

            if (track != next_track) {
                memset(fileblocks, 0, sizeof fileblocks);
                firsttrack = false;
            }
            track = next_track;
            sector = next_sector;
            if (next_track != 0) {
                fileblocks[sector] = true;
            }

            j++;
            if (j == 10) {
                j = 0;
            }
        }
        printf("\n");
    }
    printf("\n");
}


static void
assign_blocktags(image_type type, const unsigned char *image, int(*blocktags)[MAXSECTORS])
{
    char c = '@';

    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET] & 0xf;
        if (filetype != 0) {
            int filetrack = image[dirblock + FILETRACKOFFSET];
            int filesector = image[dirblock + FILESECTOROFFSET];

            if (is_transwarp_file(image, dirblock)) {
                int start_track;
                int end_track;
                int low_track;
                int high_track;
                int filesize = transwarp_stat(type, image, dirblock, &start_track, &end_track, &low_track, &high_track);
                if (filesize <= 0) {
                    continue;
                }

                int transwarp_blocks;
                int nonredundant_blocks;
                int redundant_blocks;
                int nonredundant_blocks_on_last_track;
                transwarp_size(type, start_track, end_track, filesize, &transwarp_blocks, &nonredundant_blocks, &redundant_blocks, &nonredundant_blocks_on_last_track);

                for (int track = low_track; track <= high_track; ++track) {
                    for (int sector = 0; sector < MAXSECTORS; ++sector) {
                        blocktags[track][sector] = c + (((track == end_track) && (sector >= nonredundant_blocks_on_last_track)) ? 256 : 0);
                    }
                }
            } else {
                bool new_track = true;
                while (filetrack != 0) {
                    int b = linear_sector(type, filetrack, filesector);
                    if(b < 0) {
                        break;
                    }
                    int block_offset = b * BLOCKSIZE;
                    int next_track = image[block_offset + TRACKLINKOFFSET];
                    int next_sector = image[block_offset + SECTORLINKOFFSET];

                    blocktags[filetrack][filesector] = c + (new_track ? 256 : 0);
                    new_track = (filetrack != next_track);

                    filetrack = next_track;
                    filesector = next_sector;
                }
            }

            switch (c) {
            default:
                ++c;
                break;
            case 'Z':
                c = '0';
                break;
            case '9':
                c = 'a';
                break;
            case 'z':
                c = '@';
                break;
            }
        }

    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
}

/* Returns true if the file starting on the given filetrack and filesector uses the given track */
static bool
fileontrack(image_type type, const unsigned char *image, int track, int filetrack, int filesector)
{
    while (filetrack != 0) {
        if (filetrack == track) {
            return true;
        }

        int b = linear_sector(type, filetrack, filesector);
        if(b < 0) {
            return false;
        }
        int block_offset = b * BLOCKSIZE;
        int next_track = image[block_offset + TRACKLINKOFFSET];
        int next_sector = image[block_offset + SECTORLINKOFFSET];
        filetrack = next_track;
        filesector = next_sector;
    }

    return false;
}

/* Prints all filenames of files that use the given track */
static void
print_track_usage(image_type type, const unsigned char *image, int(*blocktags)[MAXSECTORS], int track)
{

    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET] & 0xf;
        if (filetype != 0) {
            int filetrack = image[dirblock + FILETRACKOFFSET];
            int filesector = image[dirblock + FILESECTOROFFSET];
            bool ontrack = (type == IMAGE_D71) ? fileontrack(type, image, track, (filetrack > side_tracks(type)) ? filetrack - side_tracks(type) : filetrack + side_tracks(type), filesector) : false;

            if (is_transwarp_file(image, dirblock)) {
                int start_track;
                int end_track;
                int low_track;
                int high_track;
                int filesize = transwarp_stat(type, image, dirblock, &start_track, &end_track, &low_track, &high_track);
                if (filesize <= 0) {
                    continue;
                }

                ontrack = (low_track <= track) && (track <= high_track);
                if (ontrack == false) {
                    continue;
                }

                filetrack = start_track;
            }

            if (ontrack || fileontrack(type, image, track, filetrack, filesector)) {
                unsigned char *filename = (unsigned char *) image + dirblock + FILENAMEOFFSET;
                if (track == filetrack) {
                    reverse_print_on();
                }
                printf("%c", blocktags[filetrack][filesector]);
                if (track == filetrack) {
                    reverse_print_off();
                }
                printf(": ");
                print_filename(stdout, filename);
                printf(" ");
            }
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
}

/* Prints the BAM allocation map and returns the number of free blocks */
int
check_bam(image_type type, const unsigned char* image)
{
    int sectorsFree = 0;
    int sectorsFreeOnDirTrack = 0;
    int sectorsOccupied = 0;
    int sectorsOccupiedOnDirTrack = 0;

    static int blocktags[MAXTRACKS][MAXSECTORS]; /* 118 KB, not on the stack */

    if (verbose) {
        memset(blocktags, 0, sizeof blocktags);
        assign_blocktags(type, image, blocktags);

        printf("Block allocation:\n");
    }

    int max_track = image_num_tracks(type) / fmt(type)->sides; /* D71: both sides per line */
    for (int t = 1; t <= max_track; t++) {

        if (verbose) {
            printf("  %2d: ", t);
        }
        for (int s = 0; s < num_sectors(type, t); s++) {
            if (is_sector_free(type, image, t, s, 0, 0)) {
                if (verbose) {
                    printf(".");
                }
                if (t != dirtrack(type)) {
                    sectorsFree++;
                } else {
                    sectorsFreeOnDirTrack++;
                }
            } else {
                if (verbose) {
                    int blocktag = blocktags[t][s];
                    if (blocktag == 0) {
                        blocktag = '#';
                    }
                    if (blocktag >= 256) {
                        reverse_print_on();
                    }
                    printf("%c", blocktag);
                    if (blocktag >= 256) {
                        reverse_print_off();
                    }
                }
                if (t != dirtrack(type)) {
                    sectorsOccupied++;
                } else {
                    sectorsOccupiedOnDirTrack++;
                }
            }
        }

        if (fmt(type)->sides == 2) {
            for (int i = num_sectors(type, t); i < max_sectors(type) + 2; i++) {
                if (verbose) {
                    printf(" ");
                }
            }
            int t2 = t + side_tracks(type);

            if (verbose) {
                printf("%2d: ", t2);
            }
            for (int s = 0; s < num_sectors(type, t2); s++) {
                if (is_sector_free(type, image, t2, s, 0, 0)) {
                    if (verbose) {
                        printf(".");
                    }
                    if (t2 != dirtrack(type)) {
                        sectorsFree++;
                    } else {
                        /* track 53 is usually empty except the extra BAM block */
                        sectorsFreeOnDirTrack++;
                    }
                } else {
                    if (verbose) {
                        printf("#");
                    }
                    sectorsOccupied++;
                }
            }
        }

        for (int i = max_sectors(type) + 2 - num_sectors(type, t); i > 0; --i) {
            if (verbose) {
                printf(" ");
            }
        }
        if (verbose) {
            print_track_usage(type, image, blocktags, t);
            printf("\n");
        }
    }
    if (verbose) {
        printf("%3d/%3d blocks free (%d/%d including dir track)\n", sectorsFree, sectorsFree + sectorsOccupied,
               sectorsFree + sectorsFreeOnDirTrack, sectorsFree + sectorsFreeOnDirTrack + sectorsOccupied + sectorsOccupiedOnDirTrack);
    }

    return sectorsFree;
}

/* Prints the filetype like the C64 when listing the directory */
static void
print_filetype(int filetype)
{
    if ((filetype & 0x80) == 0) {
        printf("*");
    } else {
        printf(" ");
    }
    if(unicode == 1) {
        printf("%s", filetypename_uc[filetype & 0xf]);
    } else {
        printf("%s", filetypename_lc[filetype & 0xf]);
    }
    if ((filetype & 0x40) != 0) {
        printf("<");
    } else {
        printf(" ");
    }
}

/* Prints the directory like the C64 when listing the directory */
static void
print_directory(image_type type, unsigned char* image, int blocks_free)
{
    unsigned char* bam = image + linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    printf("\n0 ");
    reverse_print_on();
    printf("\"");
    print_petscii(bam + get_header_offset(type), 16);
    printf("\" ");
    print_petscii(bam + get_id_offset(type), 5);
    reverse_print_off();

    if (verbose) {
        printf("   fn hash  ASCII");
    }
    printf("\n");

    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET];
        int blocks = image[dirblock + FILEBLOCKSLOOFFSET] + 256 * image[dirblock + FILEBLOCKSHIOFFSET];

        if (filetype != FILETYPEDEL) {
            unsigned char* filename = (unsigned char*)image + dirblock + FILENAMEOFFSET;
            printf("%-3d  ", blocks);
            print_dirfilename(filename);
            print_filetype(filetype);
            if (verbose) {
                printf(" [$%04x]  \"", filenamehash(filename));
                print_filename_with_escapes(filename, FILENAMEMAXSIZE);
                printf("\"");
            }
            printf("\n");
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
    if(unicode == 1) {
        printf("%d BLOCKS FREE.\n", blocks_free);
    } else {
        printf("%d blocks free.\n", blocks_free);
    }

    /* detect and print bam message */
    if((type == IMAGE_D64 || type == IMAGE_D64_EXTENDED_SPEED_DOS) && bam[BAMMESSAGEOFFSET] != 0) {
        printf("\nBAM message: \"");
        /* only checking the first byte and hoping for the best... */
        int b = BAMMESSAGEOFFSET;
        unsigned char c;
        while(b < 256 && ((c = bam[b]) != 0)) {
            putp(c, stdout);
            b++;
        }
        printf("\"\n");
    }
}
/* Write files to disk */
static void
write_files(image_type type, unsigned char *image, imagefile *files, int num_files, int usedirtrack, int dirtracksplit, int shadowdirtrack, int numdirblocks, int dir_sector_interleave, int allocation_strategy)
{
    unsigned char sector = 0;
    int bytes_to_write = 0;
    int lastTrack = 1;
    int lastSector = sector;
    int lastOffset = linear_sector(type, lastTrack, lastSector) * BLOCKSIZE;
    int lastMinTrack = 1;
    bool transwarp_bootfile_fits_on_dir_track = false;
    int *allocation_order = NULL;

    if (num_files > 0) {
        /* make sure the first file already takes first sector per track into account */
        sector = (type == IMAGE_D81) ? 0 : files[0].first_sector_new_track;
        /* indirection to allow arbitrary disk allocation order */
        allocation_order = calloc(num_files, sizeof(int));
        if(allocation_order == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        for(int i = 0; i < num_files; i++) {
            allocation_order[i] = i;
        }
        if(allocation_strategy != 0) {
            optimize_transwarp_allocation(type, image, files, num_files, allocation_order);
        }
    }

    int transwarp_version = 100;

    for (int i = 0; i < num_files; i++) {
        imagefile *file = files + allocation_order[i];
        if (file->transwarp_boot) {
            int fileSize = 0;

            struct stat st;
            if (stat((char*)file->alocalname, &st) == 0) {
                fileSize = (int)st.st_size;
            }

            int version_major;
            int version_minor;
            if (sscanf((char *) basename(file->alocalname), "transwarp v%d.%d", &version_major, &version_minor) == 2) {
                transwarp_version = (version_major * 100) + version_minor;
            }

            int num_blocks = (fileSize / 254) + ((fileSize % 254 == 0) ? 0 : 1);
            transwarp_bootfile_fits_on_dir_track = (num_blocks <= 17);

            break;
        }
    }

    for (int i = 0; i < num_files; i++) {
        imagefile *file = files + allocation_order[i];
        unsigned char track = 1; /* reset start search track for every file, as Transwarp and -r might leave free blocks below */

        if (type == IMAGE_D81) {
            file->sectorInterleave = 1; /* caught in command line parsing anyway, but does not hurt */
        }

        int file_usedirtrack = usedirtrack;
        int file_numdirblocks = numdirblocks;

        if (file->no_file) {
            if (file->nrSectorsShown == -1) {
                file->nrSectorsShown = file->nrSectors;
            }
            file->track = 0;
            file->sector = 0;
            int entryOffset = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;
            image[entryOffset + FILETRACKOFFSET] = file->track;
            image[entryOffset + FILESECTOROFFSET] = file->sector;
            image[entryOffset + FILEBLOCKSLOOFFSET] = file->nrSectorsShown & 255;
            image[entryOffset + FILEBLOCKSHIOFFSET] = file->nrSectorsShown >> 8;
            if (shadowdirtrack > 0) {
                entryOffset = linear_sector(type, shadowdirtrack, file->direntrysector) * BLOCKSIZE + file->direntryoffset;
                image[entryOffset + FILETRACKOFFSET] = file->track;
                image[entryOffset + FILESECTOROFFSET] = file->sector;
                image[entryOffset + FILEBLOCKSLOOFFSET] = file->nrSectors & 255;
                image[entryOffset + FILEBLOCKSHIOFFSET] = file->nrSectors >> 8;
            }
        } else if (!file->loop_file) { /* loop files are handled later */

            int fileSize = 0;

            struct stat st;
            if (stat((char*)file->alocalname, &st) == 0) {
                fileSize = (int)st.st_size;
            }

            if (file->transwarp_boot) {
                file_usedirtrack = true;
                file_numdirblocks = transwarp_bootfile_fits_on_dir_track ? 2 : 4;
                file->sectorInterleave = -4;
                file->start_sector = 10;
                file->start_sector_set = true;
                file->first_sector_new_track = 10;
                track = DIRTRACK_D41_D71;
            }

            unsigned char* filedata = (unsigned char*)calloc(fileSize + ((file->filetype & FILETYPETRANSWARPMASK) ? (21 * TRANSWARPBLOCKSIZE) : 0), sizeof(unsigned char));
            if (filedata == NULL) {
                fprintf(stderr, "ERROR: Memory allocation error\n");

                exit(-1);
            }
            FILE* f = fopen((char*)file->alocalname, "rb");
            if (f == NULL) {
                fprintf(stderr, "ERROR: Could not open file \"%s\" for reading\n", file->alocalname);

                exit(-1);
            }
            if (fread(filedata, fileSize, 1, f) != 1) {
                fprintf(stderr, "ERROR: Unexpected filesize when reading %s\n", file->alocalname);
                exit(-1);
            }
            fclose(f);

            if ((!(file->filetype & FILETYPETRANSWARPMASK))
                    && (file->min_track > 0)) {
                int minTrack = file->min_track;
                if (lastMinTrack != minTrack) {
                    lastMinTrack = minTrack;
                    track = minTrack;
                    /* note that track may be smaller than lastTrack now */
                    if (track > image_num_tracks(type)) {
                        fprintf(stderr, "ERROR: Invalid minimum track %u for file %s (", track, file->alocalname);
                        print_filename(stderr, file->pfilename);
                        fprintf(stderr, ") specified\n");

                        exit(-1);
                    }
                    while ((!file_usedirtrack)
                            && ((track == dirtrack(type))
                                || (track == shadowdirtrack)
                                || (track == fmt(type)->second_bam_track))) { /* .d71 track 53 is usually empty except the extra BAM block */
                        ++track; /* skip dir track */
                    }
                    if (abs(((int) track) - lastTrack) > 1) {
                        /* previous file's last track and this file's beginning track have tracks in between */
                        sector = (type == IMAGE_D81) ? 0 : file->first_sector_new_track;
                    }
                }
            } else {
                lastMinTrack = 0;
            }

            if (file->start_sector_set) {
                sector = file->start_sector;
            }

            if ((!(file->filetype & FILETYPETRANSWARPMASK))
                    && (file->save_to_empty_track || file->fit_on_single_track)) {

                /* find first empty track */
                int found = 0;
                while (!found) {
                    for (int s = 0; s < num_sectors(type, track); s++) {
                        if (is_sector_free(type, image, track, s, file_usedirtrack ? file_numdirblocks : 0, dir_sector_interleave)) {
                            if (s == (num_sectors(type, track) - 1)) {
                                found = 1;
                                /* In first pass, use sector as left by previous file (or as set by -b) to reach first file block quickly. */
                                /* Claus: according to Krill, on real HW tracks are not aligned anyway, so it does not make a difference. */
                                /* Emulators tend to reset the disk angle on track changes, so this should rather be 3. */
                                if (sector >= num_sectors(type, track)) {
                                    if (file->start_sector_set) {
                                        fprintf(stderr, "ERROR: Invalid beginning sector %u on track %u for file %s (", sector, track, file->alocalname);
                                        print_filename(stderr, file->pfilename);
                                        fprintf(stderr, ") specified\n");

                                        exit(-1);
                                    }

                                    sector %= num_sectors(type, track);
                                }
                            }
                        } else {
                            int prev_track = track;
                            if (file->cluster_optimized) {
                                if (track > side_tracks(type)) {
                                    int next_track = track - side_tracks(type) + 1; /* to next track on first side */
                                    if (next_track < side_tracks(type)) {
                                        track = next_track;
                                    } else {
                                        ++track; /* disk full */
                                    }
                                } else {
                                    track += side_tracks(type); /* to same track on second side */
                                }
                            } else {
                                ++track;
                            }
                            while ((!file_usedirtrack)
                                    && ((track == dirtrack(type))
                                        || (track == shadowdirtrack)
                                        || (track == fmt(type)->second_bam_track))) { /* .d71 track 53 is usually empty except the extra BAM block */
                                ++track; /* skip dir track */
                            }
                            if (file->fit_on_single_track) {
                                int file_size = fileSize;
                                int first_sector = -1;
                                for (int s = 0; s < num_sectors(type, prev_track); s++) {
                                    if (is_sector_free(type, image, prev_track, s, file_usedirtrack ? file_numdirblocks : 0, dir_sector_interleave)) {
                                        if (first_sector < 0) {
                                            first_sector = s;
                                        }
                                        file_size -= BLOCKSIZE + BLOCKOVERHEAD;
                                        if (file_size <= 0) {
                                            found = 1;
                                            track = prev_track;
                                            sector = first_sector;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (track > image_num_tracks(type)) {
                                fprintf(stderr, "ERROR: Disk full, file %s (", file->alocalname);
                                print_filename(stderr, file->pfilename);
                                fprintf(stderr, ")\n");

                                exit(-1);
                            }
                            break;
                        }
                    } /* for each sector on track */

                    if ((track == (lastTrack + 2))
                            && !file->start_sector_set) {
                        /* previous file's last track and this file's beginning track have tracks in between now */
                        sector = 0;
                    }
                } /* while not found */
            }

            if (file->start_sector_set) {
                if (sector != file->start_sector) {
                    fprintf(stderr, "ERROR: Specified beginning sector of file %s (", file->alocalname);
                    print_filename(stderr, file->pfilename);
                    fprintf(stderr, ") not free on track %u\n", track);

                    exit(-1);
                }
            }

            /* found start track, now save file */
            if (type == IMAGE_D81) {
                sector = 0;
            }

            int byteOffset = 0;
            int bytesLeft = fileSize;

            unsigned long long key0 = 0;
            if (file->filetype & FILETYPETRANSWARPMASK) {
                key0 = write_transwarp_file(type, image, file, filedata, &fileSize, transwarp_version, transwarp_bootfile_fits_on_dir_track);

                bytesLeft = 0;
            }

            while (bytesLeft > 0) {
                /* Find free track & sector, starting from current T/S forward one revolution, then the next track etc... skip dirtrack (unless -t is active) */
                /* If the file didn't fit before dirtrack then restart on dirtrack + 1 and try again (unless -t is active). */
                /* If the file didn't fit before track 36/41/71 then the disk is full. */

                int blockfound = 0;
                int findSector = 0;

                while (!blockfound) {
                    /* find spare block on the current track */
                    for (int s = sector; s < sector + num_sectors(type, track); s++) {
                        findSector = s % num_sectors(type, track);

                        if (is_sector_free(type, image, track, findSector, file_usedirtrack ? file_numdirblocks : 0, dir_sector_interleave)) {
                            blockfound = 1;
                            break;
                        }
                    }

                    if (!blockfound) {
                        /* find next track, use some magic to make up for track seek delay */
                        int seek_delay = 1;
                        if (file->cluster_optimized) {
                            if (track > side_tracks(type)) {
                                track = track - side_tracks(type) + 1;
                            } else {
                                track += side_tracks(type);
                                seek_delay = 0; /* switching to the other side, no head movement */
                            }
                        } else {
                            ++track;
                        }
                        if (type == IMAGE_D81) {
                            sector = 0;
                        } else if (file->first_sector_new_track < 0) {
                            sector -= file->first_sector_new_track;
                        } else if ((file->sectorInterleave < 0)
                                   && !file->transwarp_boot) {
                            sector += seek_delay;
                        } else {
                            sector = file->first_sector_new_track;
                        }
                        if (track <= image_num_tracks(type)) { /* else: disk full, reported below */
                            sector %= num_sectors(type, track);
                        }
                        findSector = sector;

                        while ((!file_usedirtrack)
                                && ((track == dirtrack(type))
                                    || (track == shadowdirtrack)
                                    || (track == fmt(type)->second_bam_track))) { /* .d71 track 53 is usually empty except the extra BAM block */
                            /* Delete old fragments and restart file */
                            if (!dirtracksplit) {
                                if (file->nrSectors > 0) {
                                    int deltrack = file->track;
                                    int delsector = file->sector;
                                    while (deltrack != 0) {
                                        int b = linear_sector(type, deltrack, delsector);
                                        if(b < 0) {
                                            break;
                                        }
                                        int offset = b * BLOCKSIZE;
                                        mark_sector(type, image, deltrack, delsector, 1 /* free */);
                                        deltrack = image[offset + 0];
                                        delsector = image[offset + 1];
                                        memset(image + offset, 0, BLOCKSIZE);
                                    }
                                }

                                bytesLeft = fileSize;
                                byteOffset = 0;
                                file->nrSectors = 0;
                            }
                            ++track;
                        }

                        if (track > image_num_tracks(type)) {
                            if (verbose) {
                                print_file_allocation(type, image, files, num_files);
                                check_bam(type, image);
                            }

                            fprintf(stderr, "ERROR: Disk full, file %s (", file->alocalname);
                            print_filename(stderr, file->pfilename);
                            fprintf(stderr, ")\n");
                            free(filedata);

                            exit(-1);
                        }
                    }
                } /* while not block found */

                sector = findSector;
                int offset = linear_sector(type, track, sector) * BLOCKSIZE;

                if (bytesLeft == fileSize) {
                    file->track = track;
                    file->sector = sector;
                    lastTrack = track;
                    lastSector = sector;
                    lastOffset = offset;
                } else {
                    image[lastOffset + 0] = track;
                    image[lastOffset + 1] = sector;
                }

                /* write sector */
                bytes_to_write = min(BLOCKSIZE - BLOCKOVERHEAD, bytesLeft);
                memset(image + offset + 2, 0, 254);
                memcpy(image + offset + 2, filedata + byteOffset, bytes_to_write);

                bytesLeft -= bytes_to_write;
                byteOffset += bytes_to_write;

                lastTrack = track;
                lastSector = sector;
                lastOffset = offset;

                mark_sector(type, image, track, sector, 0 /* not free */);

                if (num_sectors(type, track) <= abs(file->sectorInterleave)) {
                    fprintf(stderr, "ERROR: Invalid interleave %d on track %u (%d sectors), file %s (", file->sectorInterleave, track, num_sectors(type, track), file->alocalname);
                    print_filename(stderr, file->pfilename);
                    fprintf(stderr, ")\n");

                    exit(-1);
                }

                sector += abs(file->sectorInterleave);
                sector %= num_sectors(type, track);

                file->nrSectors++;
            } /* while bytes left */

            if (!(file->filetype & FILETYPETRANSWARPMASK)) {
                image[lastOffset + 0] = 0x00;
                image[lastOffset + 1] = bytes_to_write + 1;
            }

            /* update directory entry */
            int entryOffset = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;
            image[entryOffset + FILETRACKOFFSET] = file->track;
            image[entryOffset + FILESECTOROFFSET] = file->sector;

            if (file->filetype & FILETYPETRANSWARPMASK) {
                image[entryOffset + FILETRACKOFFSET] = 0;
                image[entryOffset + FILESECTOROFFSET] = 0;

                image[entryOffset + TRANSWARPSIGNATROFFSLO] = TRANSWARPSIGNATURELO;
                image[entryOffset + TRANSWARPSIGNATROFFSHI] = TRANSWARPSIGNATUREHI;

                image[entryOffset + TRANSWARPTRACKOFFSET] = file->track;

                image[entryOffset + FILEBLOCKSLOOFFSET] = file->nrSectors;
                image[entryOffset + FILEBLOCKSHIOFFSET] = (file->nrSectors >> 8);
                if (image[entryOffset + FILEBLOCKSHIOFFSET] > 0) {
                    fprintf(stderr, "ERROR: Transwarp file \"%s\" is %d > 255 blocks big\n", file->alocalname, file->nrSectors);

                    exit(-8);
                }

                int loadaddress = (filedata[1] << 8) | filedata[0];
                image[entryOffset + LOADADDRESSLOOFFSET] = loadaddress;
                image[entryOffset + LOADADDRESSHIOFFSET] = (loadaddress >> 8);
                int endaddress = loadaddress + fileSize - 2;
                image[entryOffset + ENDADDRESSLOOFFSET] = endaddress;
                image[entryOffset + ENDADDRESSHIOFFSET] = (endaddress >> 8);

                unsigned char file_checksum = 0xff;
                for (int i = 2; i < fileSize; ++i) {
                    file_checksum ^= filedata[i];
                    file_checksum = crc8(file_checksum);
                }
                image[entryOffset + FILECHECKSUMOFFSET] = file_checksum;

                image[entryOffset + DIRDATACHECKSUMOFFSET] = 0;
                image[entryOffset + DIRDATACHECKSUMOFFSET] = (0x0100 - transwarp_dirdata_checksum(image, entryOffset));
                unsigned char dirdata_checksum = transwarp_dirdata_checksum(image, entryOffset);
                if (dirdata_checksum != 0) {
                    if (dirdata_checksum == 1) {
                        --image[entryOffset + DIRDATACHECKSUMOFFSET];
                    }
                    dirdata_checksum = transwarp_dirdata_checksum(image, entryOffset);
                    if (dirdata_checksum != 0) {
                        fprintf(stderr, "ERROR: Encoding error with \"%s\", 0x%x\n", file->alocalname, dirdata_checksum);

                        exit(-9);
                    }
                }

                if (file->have_key != 0) {
                    for (int offset = DIRDATACHECKSUMOFFSET; offset <= FILEBLOCKSLOOFFSET; ++offset) {
                        image[entryOffset + offset] ^= key0;
                        key0 >>= 8;
                    }

                    file->nrSectors = image[entryOffset + FILEBLOCKSLOOFFSET];
                }
            }

            if (file->nrSectorsShown < 0) {
                file->nrSectorsShown = file->nrSectors;
            }
            image[entryOffset + FILEBLOCKSLOOFFSET] = file->nrSectorsShown & 255;
            image[entryOffset + FILEBLOCKSHIOFFSET] = file->nrSectorsShown >> 8;

            if (shadowdirtrack > 0) {
                entryOffset = linear_sector(type, shadowdirtrack, file->direntrysector) * BLOCKSIZE + file->direntryoffset;
                image[entryOffset + FILETRACKOFFSET] = file->track;
                image[entryOffset + FILESECTOROFFSET] = file->sector;

                image[entryOffset + FILEBLOCKSLOOFFSET] = file->nrSectors & 255;
                image[entryOffset + FILEBLOCKSHIOFFSET] = file->nrSectors >> 8;
            }

            free(filedata);
        }
    } /* for each file */

    /* Set track/sector of Transwarp file entries to Transwarp bootfile */
    int transwarp_boot_track = 0;
    int transwarp_boot_sector = 0;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    for (int i = 0; i < num_files; i++) {
        imagefile *file = files + allocation_order[i];
        if (file->filetype & FILETYPETRANSWARPMASK) {
            if (transwarp_boot_track == 0) {
                /* find Transwarp bootfile */

                int t = dirtrack(type);
                int s = first_dir_sector(type);
                int o = 0;

                do {
                    int b = linear_sector(type, t, s) * BLOCKSIZE + o;
                    int filetype = image[b + FILETYPEOFFSET] & 0xf;
                    if (filetype == FILETYPEDEL) {
                        continue;
                    }

                    if (is_transwarp_file(image, b)) {
                        continue;
                    }

                    if (!is_transwarp_bootfile(image, b)) {
                        continue;
                    }

                    int filetrack = image[b + FILETRACKOFFSET];
                    int filesector = image[b + FILESECTOROFFSET];
                    if (filetrack == 0) {
                        continue;
                    }

                    if (verbose) {
                        printf("\nTranswarp bootfile at T%d/S%d\n", filetrack, filesector);
                    }

                    transwarp_boot_track = filetrack;
                    transwarp_boot_sector = filesector;

                    break;
                } while (next_dir_entry(type, image, &t, &s, &o, blockmap));
            }

            if (transwarp_boot_track == 0) {
                fprintf(stderr, "ERROR: No Transwarp bootfile provided\n");

                exit(-10);
            }

            int b = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;
            image[b + FILETRACKOFFSET] = transwarp_boot_track;
            image[b + FILESECTOROFFSET] = transwarp_boot_sector;
        }
    }
    free(blockmap);

    /* update loop files */
    for (int i = 0; i < num_files; i++) {
        imagefile *file = files + allocation_order[i];
        if (((file->filetype & 0xf) != FILETYPEDEL) && file->loop_file) {
            int track, sector, offset;
            int index;
            if (find_existing_file(type, image, file->plocalname, &index, &track, &sector, &offset)) {
                /* read track/sector and nrSectors from disk image */
                int b = linear_sector(type, track, sector) * BLOCKSIZE + offset;
                file->track = image[b + FILETRACKOFFSET];
                file->sector = image[b + FILESECTOROFFSET];
                file->nrSectors = image[b + FILEBLOCKSLOOFFSET] + (image[b + FILEBLOCKSHIOFFSET] << 8);

                /* update directory entry */
                b = linear_sector(type, dirtrack(type), file->direntrysector) * BLOCKSIZE + file->direntryoffset;
                image[b + FILETRACKOFFSET] = file->track;
                image[b + FILESECTOROFFSET] = file->sector;

                if (file->nrSectorsShown == -1) {
                    file->nrSectorsShown = file->nrSectors;
                }
                image[b + FILEBLOCKSLOOFFSET] = file->nrSectorsShown & 255;
                image[b + FILEBLOCKSHIOFFSET] = file->nrSectorsShown >> 8;

                if (shadowdirtrack > 0) {
                    b = linear_sector(type, shadowdirtrack, file->direntrysector) * BLOCKSIZE + file->direntryoffset;
                    image[b + FILETRACKOFFSET] = file->track;
                    image[b + FILESECTOROFFSET] = file->sector;

                    image[b + FILEBLOCKSLOOFFSET] = file->nrSectors & 255;
                    image[b + FILEBLOCKSHIOFFSET] = file->nrSectors >> 8;
                }

                for (int j = 0; j < num_files; j++) {
                    imagefile *other_file = files + allocation_order[j];
                    if ((i != j)
                            && (file->track == other_file->track)
                            && (file->sector == other_file->sector)) {
                        file->sectorInterleave = other_file->sectorInterleave;

                        break;
                    }
                }

                continue;
            } else {
                fprintf(stderr, "ERROR: Loop source file '%s' (%d) not found\n", file->alocalname, i + 1);
                exit(-1);
            }
        }
    }
    free(allocation_order);
}

/* Prints a command line to create dir art like the given image */
static void
convert_to_commandline(image_type type, unsigned char* image)
{
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    printf("\nCommandline to create directory art: -n \"");
    unsigned int bam = linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
    print_filename_with_escapes(image + bam + get_header_offset(type), FILENAMEMAXSIZE);
    printf("\" -i \"");
    print_filename_with_escapes(image + bam + get_id_offset(type), 5);
    printf("\" ");

    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET];
        if (filetype) {
            if(filetype != 0x82) {
                printf("-T %d ", filetype);
            }
            int size = image[dirblock + FILEBLOCKSLOOFFSET] + 256 * image[dirblock + FILEBLOCKSHIOFFSET];
            if(size != 0) {
                printf("-B %d ", size);
            }
            unsigned char *filename = (unsigned char *) image + dirblock + FILENAMEOFFSET;
            printf("-N -f \"");
            print_filename_with_escapes(filename, FILENAMEMAXSIZE);
            printf("\" -L ");
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
    printf("\n\n");
}

/* Apply list of binary patches to image */
static void
apply_patches(image_type type, unsigned char* image, patch patches[], int num_patches)
{
    for(int p = 0; p < num_patches; p++) {
        unsigned int t = patches[p].track;
        int s = patches[p].sector;
        int o = patches[p].offset;
        const char *n = patches[p].filename;
        if(t < 1 || t >= image_num_tracks(type)) {
            fprintf(stderr, "ERROR: Illegal track for -p\n");
            exit(-1);
        }
        if(s < 0 || s >= num_sectors(type, t)) {
            fprintf(stderr, "ERROR: Illegal sector for -p\n");
            exit(-1);
        }
        if(o < 0 || o > 255) {
            fprintf(stderr, "ERROR: Illegal offset for -p\n");
            exit(-1);
        }
        int pos = linear_sector(type, t, s) * BLOCKSIZE + o;
        int maxread = image_size(type) - pos;
        FILE* f = fopen(n, "rb");
        if (f == NULL) {
            fprintf(stderr, "ERROR: Could not open patch file \"%s\" for reading\n", n);
            exit(-1);
        }
        int numread = fread(image + pos, 1, maxread, f);
        if(!feof(f)) {
            fprintf(stderr, "ERROR: Patch file \"%s\" overflows disk\n", n);
            exit(-1);
        }
        fclose(f);
        while(numread + o > 0)  {
            mark_sector(type, image, t, s++, 0);
            if(s >= num_sectors(type, t)) {
                s = 0;
                t++;
            }
            numread -= BLOCKSIZE;
        }
    }
}

/* Extract files according to patterns */
static void
extract_files(image_type type, unsigned char* image, char *patterns[], int num_patterns)
{
    bool found = false;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET];

        if (filetype != FILETYPEDEL) {
            unsigned char *filename = (unsigned char *) image + dirblock + FILENAMEOFFSET;
            for(int p = 0; p < num_patterns; p++) {
                unsigned char petscii[FILENAMEMAXSIZE];
                evalhexescape((unsigned char*)patterns[p], petscii, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
                if(match_filename(petscii, filename)) {
                    char afilename[3 * FILENAMEMAXSIZE + 4 + 1]; /* space for hexevals, extension and terminating 0 */
                    found = true;
                    petscii2ascii(filename, afilename, FILENAMEMAXSIZE, false);
                    int pext = strlen(afilename);
                    filetype &= 7;
                    if(filetype <= 5) {
                        afilename[pext] = '.';
                        strcpy(afilename + pext + 1, filetypename_lc[filetype]);
                    }
                    unsigned int track = *(image + dirblock + FILETRACKOFFSET);
                    int sector = *(image + dirblock + FILESECTOROFFSET);
                    unsigned int last_track;
                    int last_sector;
                    int result = validate_sector_chain(type, image, blockmap, track, sector, &last_track, &last_sector);
                    printf("\nExtracting %s", afilename);
                    if(result == FIRST_BROKEN) {
                        printf(", ERROR: directory entry invalid");
                    } else {
                        if(result != VALID) {
                            printf(", WARNING: file corrupt");
                        }
                        FILE *file = fopen(afilename, "wb");
                        if(file == NULL) {
                            fprintf(stderr, "\nERROR: Could not open output file %s\n", afilename);
                            exit(-1);
                        }
                        while(track != last_track || sector != last_sector) {
                            unsigned char *block = image + linear_sector(type, track, sector) * BLOCKSIZE;
                            fwrite(block + 2, 1, 254, file);
                            track = *block;
                            sector = *(block + 1);
                        }
                        unsigned char *block = image + linear_sector(type, track, sector) * BLOCKSIZE;
                        fwrite(block + 2, 1, *(block + 1) - 1, file);
                        fclose(file);
                    }
                    break;
                }
            }
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    if(found) {
        printf("\n");
    }
    free(blockmap);
}

int
main(int argc, char* argv[])
{
    imagefile files[MAXNUMFILES];
    memset(files, 0, sizeof files);
    patch patches[MAXNUMPATCHES];
    memset(patches, 0, sizeof patches);
    int num_patches = 0;
    char *extractions[MAXNUMEXTRACTIONS];
    memset(extractions, 0, sizeof extractions);
    int num_extractions = 0;
    image_type type = IMAGE_D64;
    char* imagepath = NULL;
    char* filename_g64 = NULL;
    unsigned char* header  = (unsigned char*)"cit";
    unsigned char* id      = (unsigned char*)"00#a02a";
    unsigned char* bam_message = NULL;
    int dirtracksplit = 1;
    int usedirtrack = 0;
    unsigned int shadowdirtrack = 0;

    int default_first_sector_new_track = 0;
    int first_sector_new_track = 0;
    int defaultSectorInterleave = 10;
    int sectorInterleave = 0;
    int dir_sector_interleave = 3;
    int numdirblocks = 2;
    int nrSectorsShown = -1;
    unsigned char* filename = NULL;
    int set_header = 0;
    int nooverwrite = 0;
    int dovalidate = 0;
    int restore_level = -1;
    int ignore_collision = 0;
    int filetype = 0x82; /* default is closed PRG */
    bool filetype_set = false;
    bool print_art_commandline = false;
    int allocation_strategy = 1;

    /* flags to detect illegal settings */
    int transwarp_set = 0;
    int format_choice = -1;           /* -D: image type, -1 = from the file name */
    bool d40 = false;                 /* -D d40: only what DOS 2.x (2031/4040) can read */
    bool id_set = false;
    int sector_interleave_set = 0;
    int default_sector_interleave_set = 0;
    int file_start_sector_set = 0;
    int new_track_start_sector_set = 0;
    int cluster_optimized = 0;

    int retval = 0;

    int i, j;

    if (argc == 1 || strcmp(argv[argc-1], "-h") == 0) {
        usage();
    }

    /* read environment variable for unicode printout */
    char *unicode_env = getenv("CC1541UNICODE");
    if(unicode_env != NULL) {
        unicode = atoi(unicode_env);
        if(unicode < 0 || unicode > 2) {
            unicode = 0;
        }
    }

    for (j = 1; j < argc - 1; j++) {
        if (strcmp(argv[j], "-n") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -n\n");
                return -1;
            }
            header = (unsigned char*)argv[++j];
            set_header = 1;
            modified = 1;
        } else if (strcmp(argv[j], "-i") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -i\n");
                return -1;
            }
            id = (unsigned char*)argv[++j];
            id_set = true;
            set_header = 1;
            modified = 1;
        } else if (strcmp(argv[j], "-H") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -H\n");
                return -1;
            }
            bam_message = (unsigned char*)argv[++j];
            set_header = 1;
            modified = 1;
        } else if (strcmp(argv[j], "-M") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &max_hash_length)) {
                fprintf(stderr, "ERROR: Error parsing argument for -M\n");
                return -1;
            }
            if ((max_hash_length < 1) || (max_hash_length > FILENAMEMAXSIZE)) {
                fprintf(stderr, "ERROR: Hash computation maximum filename length %d specified\n", max_hash_length);
                return -1;
            }
        } else if (strcmp(argv[j], "-m") == 0) {
            ignore_collision = 1;
        } else if (strcmp(argv[j], "-F") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &first_sector_new_track)) {
                fprintf(stderr, "ERROR: Error parsing argument for -F\n");
                return -1;
            }
            new_track_start_sector_set = 1;
        } else if (strcmp(argv[j], "-S") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &defaultSectorInterleave)) {
                fprintf(stderr, "ERROR: Error parsing argument for -S\n");
                return -1;
            }
            if(defaultSectorInterleave < 1 || defaultSectorInterleave > 21) {
                fprintf(stderr, "ERROR: Illegal value for -S\n");
                return -1;
            }
            default_sector_interleave_set = 1;
        } else if (strcmp(argv[j], "-s") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &sectorInterleave)) {
                fprintf(stderr, "ERROR: Error parsing argument for -s\n");
                return -1;
            }
            if(sectorInterleave < 1 || sectorInterleave > 21) {
                fprintf(stderr, "ERROR: Illegal value for -s\n");
                return -1;
            }
            sector_interleave_set = 1;
        } else if (strcmp(argv[j], "-f") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -f\n");
                return -1;
            }
            filename = (unsigned char*)argv[++j];
        } else if (strcmp(argv[j], "-A") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &allocation_strategy)) {
                fprintf(stderr, "ERROR: Error parsing argument for -A\n");
                return -1;
            }
            if(allocation_strategy < 0 || allocation_strategy > 1) {
                fprintf(stderr, "ERROR: Illegal value for -A\n");
                return -1;
            }
        } else if (strcmp(argv[j], "-e") == 0) {
            files[num_files].save_to_empty_track = true;
        } else if (strcmp(argv[j], "-E") == 0) {
            files[num_files].fit_on_single_track = true;
        } else if (strcmp(argv[j], "-r") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &i)) {
                fprintf(stderr, "ERROR: Error parsing argument for -r\n");
                return -1;
            }
            if ((i < 1) || (i > MAXTRACKS)) {
                fprintf(stderr, "ERROR: Invalid minimum track %d specified\n",  i);
                return -1;
            }
            files[num_files].min_track = i;
        } else if (strcmp(argv[j], "-b") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &i)) {
                fprintf(stderr, "ERROR: Error parsing argument for -b\n");
                return -1;
            }
            if ((i < 0) || (i >= MAXSECTORS)) { /* checked against the image type later */
                fprintf(stderr, "ERROR: Invalid beginning sector %d specified\n", i);
                return -1;
            }
            files[num_files].start_sector = i;
            files[num_files].start_sector_set = true;
            file_start_sector_set = 1;
        } else if (strcmp(argv[j], "-c") == 0) {
            files[num_files].cluster_optimized = true;
            cluster_optimized = 1;
        } else if (strcmp(argv[j], "-o") == 0) {
            nooverwrite = 1;
        } else if (strcmp(argv[j], "-V") == 0) {
            dovalidate = 1;
        } else if (strcmp(argv[j], "-T") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -T\n");
                return -1;
            }
            if (strcmp(argv[j + 1], "DEL") == 0) {
                filetype = (filetype & 0xf0) | FILETYPEDEL;
            } else if (strcmp(argv[j + 1], "SEQ") == 0) {
                filetype = (filetype & 0xf0) | FILETYPESEQ;
            } else if (strcmp(argv[j + 1], "PRG") == 0) {
                filetype = (filetype & 0xf0) | FILETYPEPRG;
            } else if (strcmp(argv[j + 1], "USR") == 0) {
                filetype = (filetype & 0xf0) | FILETYPEUSR;
            } else if (strcmp(argv[j + 1], "REL") == 0) {
                filetype = (filetype & 0xf0) | FILETYPEREL;
            } else {
                char* dummy;
                filetype = strtol(argv[j + 1], &dummy, 10);
                if(dummy == argv[j + 1] || *dummy != 0 || filetype < 0 || filetype > 255) {
                    fprintf(stderr, "ERROR: Error parsing argument for -T\n");
                    return -1;
                }
            }
            filetype_set = true;
            j++;
        } else if (strcmp(argv[j], "-O") == 0) {
            filetype &= 0x7f;
        } else if (strcmp(argv[j], "-P") == 0) {
            filetype |= 0x40;
        } else if (strcmp(argv[j], "-N") == 0) {
            files[num_files].force_new = 1;
            ignore_collision = 1;
        } else if (strcmp(argv[j], "-K") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -K\n");
                return -1;
            }
            evalhexescape((unsigned char *) argv[++j], files[num_files].key, TRANSWARPKEYSIZE, 0);
            files[num_files].have_key = true;
        } else if ((strcmp(argv[j], "-w") == 0)
                   || (strcmp(argv[j], "-W") == 0)) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for %s\n", argv[j]);
                return -1;
            }
            files[num_files].alocalname = (unsigned char*)argv[j + 1];
            if (filename == NULL) {
                ascii2petscii(basename(files[num_files].alocalname), files[num_files].pfilename, FILENAMEMAXSIZE); /* do not eval escapes when converting the filename, as the local filename could contain the escape char */
            } else {
                evalhexescape(filename, files[num_files].pfilename, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
            }
            files[num_files].sectorInterleave = sectorInterleave ? sectorInterleave : defaultSectorInterleave;
            files[num_files].first_sector_new_track = first_sector_new_track;
            files[num_files].nrSectorsShown = nrSectorsShown;
            files[num_files].filetype = filetype;
            files[num_files].direntryindex = -1;

            if (strcmp(argv[j], "-W") == 0) {
                if(nrSectorsShown != -1) {
                    fprintf(stderr, "ERROR: -B cannot be used for Transwarp files\n");
                    return -1;
                }
                if(file_start_sector_set) {
                    fprintf(stderr, "ERROR: -b cannot be used for Transwarp files\n");
                    return -1;
                }
                if(sector_interleave_set) {
                    fprintf(stderr, "ERROR: -s cannot be used for Transwarp files\n");
                    return -1;
                }
                transwarp_set = true;
                if(!filetype_set && files[num_files].have_key) {
                    files[num_files].filetype = (filetype & 0xf0) | FILETYPEUSR | FILETYPETRANSWARPMASK;
                } else {
                    files[num_files].filetype = filetype | FILETYPETRANSWARPMASK;
                }
                files[num_files].sectorInterleave = 1;
            }

            first_sector_new_track = default_first_sector_new_track;
            filename = NULL;
            sectorInterleave = 0;
            nrSectorsShown = -1;
            filetype = 0x82;
            filetype_set = false;
            num_files++;
            modified = 1;
            j++;
        } else if (strcmp(argv[j], "-l") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -l\n");
                return -1;
            }
            files[num_files].alocalname = (unsigned char*)argv[j + 1];
            evalhexescape(files[num_files].alocalname, files[num_files].plocalname, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
            if (filename == NULL) {
                fprintf(stderr, "ERROR: Loop files require a filename set with -f\n");
                return -1;
            }
            evalhexescape(filename, files[num_files].pfilename, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
            if(memcmp(files[num_files].pfilename, files[num_files].plocalname, FILENAMEMAXSIZE) == 0 && !files[num_files].force_new) {
                fprintf(stderr, "ERROR: Loop file cannot have the same name as the file they refer to, unless with -N\n");
                return -1;
            }
            files[num_files].loop_file = true;
            files[num_files].sectorInterleave = 0;
            files[num_files].first_sector_new_track = first_sector_new_track;
            first_sector_new_track = default_first_sector_new_track;
            files[num_files].nrSectorsShown = nrSectorsShown;
            files[num_files].filetype = filetype;
            files[num_files].direntryindex = -1;
            filename = NULL;
            sectorInterleave = 0;
            nrSectorsShown = -1;
            filetype = 0x82;
            filetype_set = false;
            num_files++;
            modified = 1;
            j++;
        } else if (strcmp(argv[j], "-L") == 0) {
            if (filename == NULL) {
                fprintf(stderr, "ERROR: Writing no file using -L requires disk filename set with -f\n");
                return -1;
            }
            evalhexescape(filename, files[num_files].pfilename, FILENAMEMAXSIZE, FILENAMEEMPTYCHAR);
            files[num_files].nrSectorsShown = nrSectorsShown;
            files[num_files].filetype = filetype;
            files[num_files].direntryindex = -1;
            files[num_files].no_file = true;

            first_sector_new_track = default_first_sector_new_track;
            filename = NULL;
            sectorInterleave = 0;
            nrSectorsShown = -1;
            filetype = 0x82;
            filetype_set = false;
            num_files++;
            modified = 1;
        } else if (strcmp(argv[j], "-x") == 0) {
            dirtracksplit = 0;
        } else if (strcmp(argv[j], "-t") == 0) {
            usedirtrack = 1;
        } else if (strcmp(argv[j], "-d") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%u", &shadowdirtrack)) {
                fprintf(stderr, "ERROR: Error parsing argument for -d\n");
                return -1;
            }
            modified = 1;
        } else if (strcmp(argv[j], "-u") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &numdirblocks)) {
                fprintf(stderr, "ERROR: Error parsing argument for -u\n");
                return -1;
            }
        } else if (strcmp(argv[j], "-B") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &nrSectorsShown)) {
                fprintf(stderr, "ERROR: Error parsing argument for -B\n");
                return -1;
            }
            if (nrSectorsShown < 0 || nrSectorsShown > 65535) {
                fprintf(stderr, "ERROR: Argument must be between 0 and 65535 for -B\n");
                return -1;
            }
        } else if (strcmp(argv[j], "-D") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -D\n");
                return -1;
            }
            const char *f = argv[++j];
            if (strcasecmp(f, "d40") == 0) {
                format_choice = IMAGE_D64;
                d40 = true;
            } else if (strcasecmp(f, "d41") == 0 || strcasecmp(f, "d64") == 0) {
                format_choice = IMAGE_D64;
            } else if (strcasecmp(f, "d71") == 0) {
                format_choice = IMAGE_D71;
            } else if (strcasecmp(f, "d81") == 0) {
                format_choice = IMAGE_D81;
            } else if (strcasecmp(f, "d80") == 0) {
                format_choice = IMAGE_D80;
            } else if (strcasecmp(f, "d82") == 0) {
                format_choice = IMAGE_D82;
            } else {
                fprintf(stderr, "ERROR: Unknown image format %s for -D\n", f);
                return -1;
            }
        } else if (strcmp(argv[j], "-4") == 0) {
            type = IMAGE_D64_EXTENDED_SPEED_DOS;
            modified = 1;
        } else if (strcmp(argv[j], "-R") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &restore_level)) {
                fprintf(stderr, "ERROR: Error parsing argument for -R\n");
                return -1;
            }
            if(restore_level < 0 || restore_level > 5) {
                fprintf(stderr, "ERROR: Argument must be between 0 and 5 for -R\n");
                return -1;
            }
        } else if (strcmp(argv[j], "-5") == 0) {
            type = IMAGE_D64_EXTENDED_DOLPHIN_DOS;
            modified = 1;
        } else if (strcmp(argv[j], "-a") == 0) {
            print_art_commandline = true;
        } else if(strcmp(argv[j], "-g") == 0) {
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -g\n");
                return -1;
            }
            filename_g64 = argv[++j];
        } else if (strcmp(argv[j], "-U") == 0) {
            if ((argc < j + 2) || !sscanf(argv[++j], "%d", &unicode)) {
                fprintf(stderr, "ERROR: Error parsing argument for -U\n");
                return -1;
            }
            if(unicode < 0 || unicode > 2) {
                fprintf(stderr, "ERROR: Argument must be between 0 and 2 for -U\n");
                return -1;
            }
        } else if (strcmp(argv[j], "-p") == 0) {
            if(num_patches >= MAXNUMPATCHES) {
                fprintf(stderr, "ERROR: Too many patches, maximum supported number is %d\n", MAXNUMPATCHES);
                return -1;
            }
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -p\n");
                return -1;
            }
            int numparsed;
            if((sscanf(argv[j + 1], "%d,%d,%d%n", &(patches[num_patches].track), &(patches[num_patches].sector), &(patches[num_patches].offset), &numparsed) != 3)
                    || (argv[j + 1][numparsed] != ',')) {
                fprintf(stderr, "ERROR: Error parsing arguments for -p\n");
                return -1;
            }
            patches[num_patches++].filename = argv[j + 1] + numparsed + 1;
            modified = 1;
            j++;
        } else if (strcmp(argv[j], "-X") == 0) {
            if(num_extractions >= MAXNUMEXTRACTIONS) {
                fprintf(stderr, "ERROR: Too many file extraction patterns, maximum supported number is %d\n", MAXNUMPATCHES);
                return -1;
            }
            if (argc < j + 2) {
                fprintf(stderr, "ERROR: Error parsing argument for -X\n");
                return -1;
            }
            extractions[num_extractions++] = argv[j + 1];
            j++;
        } else if (strcmp(argv[j], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[j], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[j], "-h") == 0) {
            usage();
        } else {
            fprintf(stderr, "ERROR: Error parsing command line at \"%s\"\n", argv[j]);
            printf("Use -h for help.\n");
            return -1;
        }
    }
    if (j >= argc) {
        fprintf(stderr, "ERROR: No image file provided, or misparsed last option\n");
        return -1;
    }
    imagepath = argv[argc-1];

    bool extended = (type == IMAGE_D64_EXTENDED_SPEED_DOS) || (type == IMAGE_D64_EXTENDED_DOLPHIN_DOS);
    if (format_choice >= 0) {
        if (extended && (format_choice != IMAGE_D64 || d40)) {
            fprintf(stderr, "ERROR: -4 and -5 are only supported for D41 (1541) images\n");
            return -1;
        }
        if (!extended) {
            type = format_choice;
        }
    } else if (strlen(imagepath) >= 4) {
        const char *ext = imagepath + strlen(imagepath) - 4;
        int t = -1;
        if (strcasecmp(ext, ".d71") == 0) {
            t = IMAGE_D71;
        } else if (strcasecmp(ext, ".d81") == 0) {
            t = IMAGE_D81;
        } else if (strcasecmp(ext, ".d80") == 0) {
            t = IMAGE_D80;
        } else if (strcasecmp(ext, ".d82") == 0) {
            t = IMAGE_D82;
        }
        if (t >= 0) {
            if (extended) {
                fprintf(stderr, "ERROR: Extended %s images are not supported\n", ext);
                return -1;
            }
            type = t;
        }
    }
    dir_sector_interleave = fmt(type)->dir_interleave;
    /* The 5 byte id field is "id A0 dos-version". Only the C64 D64 (D41,
       also with -4/-5) keeps cc1541's free 5 byte id; for all other formats
       -i sets the 2 id characters and the rest comes from the format. */
    bool free_id = (type == IMAGE_D64 && !d40) || extended;
    static char full_id[64];
    if (!id_set) {
        id = (unsigned char*)fmt(type)->default_id;
    } else if (!free_id) {
        /* take the first 2 id characters (hex escapes count as one), pad with A0 */
        const char *p = (const char *)id;
        int n = 0;
        full_id[0] = 0;
        for (int k = 0; k < 2; k++) {
            int len = 0;
            if (p[n] == '#' && isxdigit((unsigned char)p[n + 1]) && isxdigit((unsigned char)p[n + 2])) {
                len = 3;
            } else if (p[n]) {
                len = 1;
            }
            if (len) {
                strncat(full_id, p + n, len);
                n += len;
            } else {
                strcat(full_id, "#a0");
            }
        }
        strcat(full_id, fmt(type)->default_id + 2);
        id = (unsigned char*)full_id;
    }
    for (int k = 0; k < num_files; k++) {
        if (files[k].start_sector_set && files[k].start_sector >= max_sectors(type)) {
            fprintf(stderr, "ERROR: Invalid beginning sector %d specified\n", files[k].start_sector);
            return -1;
        }
    }
    if (d40 && transwarp_set) {
        fprintf(stderr, "ERROR: Transwarp is not supported for D40 (2031/4040) images\n");
        return -1;
    }

    if(type != IMAGE_D71 && cluster_optimized) {
        fprintf(stderr, "ERROR: -c switch is only supported for D71 images\n");
        return -1;
    }

    if(bam_message != NULL && type != IMAGE_D64 && type != IMAGE_D64_EXTENDED_SPEED_DOS) {
        fprintf(stderr, "ERROR: Bam message only supported for D64 and SPEED DOS images\n");
        return -1;
    }

    if(shadowdirtrack > image_num_tracks(type) || (int)shadowdirtrack == dirtrack(type) || (fmt(type)->second_bam_track && (int)shadowdirtrack == fmt(type)->second_bam_track)) {
        fprintf(stderr, "ERROR: Invalid shadow directory track\n");
        return -1;
    }

    if(!transwarp_set) {
        allocation_strategy = 0;
    }

    if (type != IMAGE_D64) {
        if (transwarp_set
                && (type != IMAGE_D64_EXTENDED_SPEED_DOS)
                && (type != IMAGE_D64_EXTENDED_DOLPHIN_DOS)) {
            fprintf(stderr, "ERROR: Transwarp encoding is not supported for non-D64 images\n");
            return -1;
        }

        if (filename_g64 != NULL) {
            fprintf(stderr, "ERROR: G64 output is only supported for non-extended D64 images\n");
            return -1;
        }
    }

    /* Check for unsupported settings for D81 */
    if (type == IMAGE_D81) {
        if (default_sector_interleave_set) {
            fprintf(stderr, "ERROR: -S is not supported for D81 images\n");
            return -1;
        }
        if (sector_interleave_set) {
            fprintf(stderr, "ERROR: -s is not supported for D81 images\n");
            return -1;
        }
        if (new_track_start_sector_set) {
            fprintf(stderr, "ERROR: -F is not supported for D81 images\n");
            return -1;
        }
        if (file_start_sector_set) {
            fprintf(stderr, "ERROR: -b is not supported for D81 images\n");
            return -1;
        }
    }

    /* Change locale from C to default to allow unicode printouts */
    if(unicode != 0) {
        setlocale(LC_ALL, "");
    }

    /* quiet has precedence over verbose */
    if(quiet) {
        verbose = 0;
    }

    /* open image */
    unsigned int imagesize = image_size(type);
    unsigned char* image = (unsigned char*)calloc(imagesize, sizeof(unsigned char));
    if (image == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        return -1;
    }
    FILE* f = fopen(imagepath, "rb");
    if (f == NULL) {
        modified = 1;
        if (!quiet) {
            printf("Adding %d files to new image %s\n", num_files, basename((unsigned char*)imagepath));
        }
        initialize_directory(type, image, header, id, bam_message, shadowdirtrack, usedirtrack);
    } else {
        if (!quiet) {
            printf("Adding %d files to existing image %s\n", num_files, basename((unsigned char*)imagepath));
        }
        size_t read_size = fread(image, 1, imagesize, f);
        fclose(f);
        if (read_size != imagesize) {
            if (((type == IMAGE_D64_EXTENDED_SPEED_DOS) || (type == IMAGE_D64_EXTENDED_DOLPHIN_DOS)) && (read_size == D64SIZE)) {
                /* Clear extra tracks */
                memset(image + image_size(IMAGE_D64), 0, image_size(type) - image_size(IMAGE_D64));

                /* Mark all extra sectors unused */
                for (unsigned int t = D64NUMTRACKS + 1; t <= image_num_tracks(type); t++) {
                    for (int s = 0; s < num_sectors(type, t); s++) {
                        mark_sector(type, image, t, s, 1 /* free */);
                    }
                }
            } else {
                fprintf(stderr, "ERROR: Wrong filesize: expected to read %u bytes, but read %u bytes\n", imagesize, (unsigned int) read_size);
                return -1;
            }
        }
        if (dovalidate) {
            validate(type, image);
        }
        if (restore_level >= 0) {
            restore(type, image, restore_level, files);
        }
        if (set_header) {
            update_directory(type, image, header, id, bam_message, shadowdirtrack);
        }
    }

    /* Print command line before adding anything to the image */
    if(print_art_commandline) {
        convert_to_commandline(type, image);
    }

    /* Extract files */
    extract_files(type, image, extractions, num_extractions);

    /* Apply patches */
    apply_patches(type, image, patches, num_patches);

    /* Create directory entries */
    create_dir_entries(type, image, files, num_files, dir_sector_interleave, shadowdirtrack, nooverwrite);

    /* Write files and mark sectors in BAM */
    write_files(type, image, files, num_files, usedirtrack, dirtracksplit, shadowdirtrack, numdirblocks, dir_sector_interleave, allocation_strategy);

    /* Print allocation info */
    if (verbose) {
        print_file_allocation(type, image, files, num_files);
    }
    int blocks_free = check_bam(type, image);

    /* Print directory */
    if (!quiet) {
        print_directory(type, image, blocks_free);
    }

    /* Show directory issues if present */
    if(dir_error != DIR_OK) {
        fprintf(stdout, "WARNING: %s\n", dir_error_string[dir_error]);
    }

    /* Save image */
    if(modified) {
        f = fopen(imagepath, "wb");
        if (f == NULL || fwrite(image, imagesize, 1, f) != 1) {
            fprintf(stderr, "ERROR: Failed to write %s\n", imagepath);
            retval = -1;
        }
        if (f != NULL) {
            fclose(f);
        }
    }

    /* Save optional g64 image */
    if (filename_g64 != NULL) {
        /* retval might be set to -1 already.  Thus we need to take its
        previous state and OR it with the following return value. */
        retval |= generate_uniformat_g64(image, filename_g64);
    }

    if (!ignore_collision && check_hashes(type, image)) {
        fprintf(stderr, "\nERROR: Filename hash collision detected; image is not compatible with Krill's loader. Use -m to ignore this error.\n");
        retval = -1;
    }

    free(image);

    return retval;
}
