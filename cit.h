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

/* cit.h: declarations shared by the modules of CIT */

#ifndef CIT_H
#define CIT_H

#define VERSION "0.1" /* CIT, based on cc1541 4.2 */

#define _CRT_SECURE_NO_WARNINGS /* avoid security warnings for MSVC */

#include <ctype.h>
#include <stdarg.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wchar.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#define min(a, b) (((a) < (b)) ? (a) : (b))

#ifdef _WIN32
#include <windows.h>
#define FILESEPARATOR '\\'
#else
#define FILESEPARATOR '/'
#endif

#define DIRENTRIESPERBLOCK     8
#define MAXTRACKS              154  /* most tracks of any image (D82) */
#define MAXSECTORS             192  /* most sectors per track (D90: 6 heads x 32) */
#define DIRTRACK_D41_D71       18
#define MAXNUMFILES            296  /* files per call (D81 directory: 37 sectors x 8) */
#define DIRENTRYSIZE           32
#define BLOCKSIZE              256
#define BLOCKOVERHEAD          2
#define TRACKLINKOFFSET        0
#define SECTORLINKOFFSET       1
#define FILETYPEOFFSET         2
#define FILETYPEDEL            0
#define FILETYPESEQ            1
#define FILETYPEPRG            2
#define FILETYPEUSR            3
#define FILETYPEREL            4
#define FILETYPETRANSWARPMASK  0x100
#define FILETRACKOFFSET        3
#define FILESECTOROFFSET       4
#define FILENAMEOFFSET         5
#define FILENAMEMAXSIZE        16
#define FILENAMEEMPTYCHAR      (' ' | 0x80)
#define BAMMESSAGEOFFSET       0xab
#define BAMMESSAGEMAXSIZE      0x100-BAMMESSAGEOFFSET
#define TRANSWARPSIGNATROFFSLO 21
#define TRANSWARPSIGNATURELO   'T'
#define TRANSWARPSIGNATROFFSHI 22
#define TRANSWARPSIGNATUREHI   'W'
#define DIRDATACHECKSUMOFFSET  23
#define TRANSWARPTRACKOFFSET   24
#define FILECHECKSUMOFFSET     25
#define LOADADDRESSLOOFFSET    26
#define LOADADDRESSHIOFFSET    27
#define ENDADDRESSLOOFFSET     28
#define ENDADDRESSHIOFFSET     29
#define FILEBLOCKSLOOFFSET     30
#define FILEBLOCKSHIOFFSET     31
#define D64NUMBLOCKS           (664 + 19)
#define D64SIZE                (D64NUMBLOCKS * BLOCKSIZE)
#define D64NUMTRACKS           35
#define BAM_OFFSET_SPEED_DOS   0xc0
#define BAM_OFFSET_DOLPHIN_DOS 0xac
#define DIRSLOTEXISTS          0
#define DIRSLOTFOUND           1
#define DIRSLOTNOTFOUND        2
/* for sector chain analysis */
#define UNALLOCATED            0 /* unused as of now */
#define ALLOCATED              1 /* part of a valid sector chain */
#define FILESTART              2 /* analysed to be the start of a sector chain */
#define FILESTART_TRUNCATED    3 /* analysed to be the start of a sector chain, was truncated */
#define POTENTIALLYALLOCATED   4 /* currently being analysed */
/* error codes for sector chain validation */
#define VALID                  0 /* valid chain */
#define ILLEGAL_TRACK          1 /* ends with illegal track pointer */
#define ILLEGAL_SECTOR         2 /* ends with illegal sector pointer */
#define LOOP                   3 /* loop in current chain */
#define COLLISION              4 /* collision with other file */
#define CHAINED                5 /* ends at another file start */
#define CHAINED_TRUNCATED      6 /* ends at start of a truncated file */
#define FIRST_BROKEN           7 /* issue already in first sector */
/* undelete levels */
#define RESTORE_DIR_ONLY        0 /* Only restore all dir entries without touching any t/s links */
#define RESTORE_VALID_FILES     1 /* Fix dir entries for files with valid t/s chains */
#define RESTORE_VALID_CHAINS    2 /* Also add wild sector chains with valid t/s chains */
#define RESTORE_INVALID_FILES   3 /* Also fix dir entries with invalid t/s chains */
#define RESTORE_INVALID_CHAINS  4 /* Also add and fix wild invalid t/s chains */
#define RESTORE_INVALID_SINGLES 5 /* Also include single block files */
/* error codes for directory */
#define DIR_OK                 0
#define DIR_ILLEGAL_TS         1
#define DIR_CYCLIC_TS          2

#define TRANSWARP                "TRANSWARP"
#define TRANSWARPBASEBLOCKSIZE   0xc0
#define TRANSWARPBUFFERBLOCKSIZE 0x1f
#define TRANSWARPBLOCKSIZE       (TRANSWARPBASEBLOCKSIZE + TRANSWARPBUFFERBLOCKSIZE)
#define TRANSWARPKEYSIZE         29 /* 232 bits */
#define TRANSWARPKEYHASHROUNDS   33

/* maximum number of files for transwarp order permutation */
#define MAXPERMUTEDFILES 11
/* maximum number of free sectors on an extended D64 */
#define MAXFREESECTORS 768
/* maximum number of patches */
#define MAXNUMPATCHES 32
/* maximum number of patterns for file extraction */
#define MAXNUMEXTRACTIONS 32

typedef struct {
    const unsigned char* alocalname;                  /* local file name or name of loop file in ASCII */
    unsigned char        plocalname[FILENAMEMAXSIZE]; /* loop file in PETSCII */
    unsigned char        pfilename[FILENAMEMAXSIZE];  /* disk file name in PETSCII */
    int                  direntryindex;
    int                  direntrysector;
    int                  direntryoffset;
    int                  sectorInterleave;
    int                  first_sector_new_track;
    int                  track;
    int                  sector;
    int                  nrSectors;
    int                  nrSectorsShown;
    int                  filetype;
    /* content already in memory (--convert), instead of reading alocalname */
    const unsigned char *data;
    int                  data_size;
    bool                 has_data;
    void                *data_alloc;           /* to free: start of the data buffer */
    void                *name_alloc;           /* to free: alocalname, if allocated */
    int                  record_length;        /* REL files: record length (1-254) */
    int                  loop_target;          /* loop file: index + 1 of the file in files[], 0 = by name */
    bool                 convert_by_name; /* -w without -T: .txt/.csv/.bas/[rsNN].rel converted */
    bool                 name_from_local; /* no -f: the CBM name comes from the local name */
    /* placement of the file (strategy cc1541) and kind of entry */
    int                  start_sector;         /* -b: first sector of the file */
    bool                 start_sector_set;
    int                  min_track;            /* -r: lowest track, 0 = none */
    bool                 save_to_empty_track;  /* -e */
    bool                 fit_on_single_track;  /* -E */
    bool                 cluster_optimized;    /* -c: D71, both sides of a track */
    bool                 loop_file;            /* -l: second entry of an existing file */
    bool                 no_file;              /* -L: directory entry only */
    bool                 transwarp_boot;       /* the Transwarp boot file */
    int                  force_new;
    int                  size;
    int                  last_track;
    bool                 have_key;
    unsigned char        key[TRANSWARPKEYSIZE];
} imagefile;

typedef struct {
    const char* filename;
    int track;
    int sector;
    int offset;
} patch;

typedef enum {
    IMAGE_D64,
    IMAGE_D64_EXTENDED_SPEED_DOS,
    IMAGE_D64_EXTENDED_DOLPHIN_DOS,
    IMAGE_D71,
    IMAGE_D81,
    IMAGE_D80,
    IMAGE_D82,
    IMAGE_D60,                  /* D9060 hard disk (DOS 3.0), 4 heads */
    IMAGE_D90                   /* D9090 hard disk (DOS 3.0), 6 heads */
} image_type;


/*
 * Format table: everything that describes the geometry and the layout of an
 * image type lives here. Code that needs such a value asks the table instead
 * of testing the image type.
 *
 * zone[]: sectors per track, valid up to and including last_track. Tracks
 * count from 1. The zones of a double sided image cover both sides.
 */
#define MAXZONES 8

typedef struct {
    const char *name;           /* image type as shown to the user */
    int num_tracks;             /* tracks of the whole image (both sides) */
    int sides;                  /* 2: second side starts at num_tracks/2+1 */
    struct {
        int last_track;
        int sectors;
    } zone[MAXZONES];
    int dir_track;              /* track with header, BAM and directory */
    int first_dir_sector;       /* first directory sector on dir_track */
    int dir_interleave;         /* default interleave of directory sectors */
    int header_offset;          /* disk name in the header sector */
    int id_offset;              /* disk id (5 bytes incl. dos type) */
    unsigned char dos_type;     /* format byte at offset 2, 0 = not checked */
    int second_bam_track;       /* 1571: BAM of the second side (track 53), 0 = none */
    int file_interleave;        /* DOS sector increment for files (SECINC) */
    int num_bam_blocks;         /* BAM blocks besides the header (dir_track/0) */
    struct {
        int track;
        int sector;
    } bam_block[4];
    const char *default_id;     /* disk id incl. DOS version, hex escapes allowed */
    /* hard disks (DOS 3.0): track 0 is part of the image (0/0 points to
       directory, header and BAM, 0/1 lists the bad sectors); a sector number
       is head * 32 + sector of the head, the BAM has an entry per head */
    bool track0;
    int heads;                  /* 0 = floppy */
    int header_sector;          /* header on dir_track/header_sector */
} format_t;

/* cit.c: global settings */
extern int quiet;
extern int verbose;
extern int num_files;
extern int max_hash_length;
extern int unicode;
unsigned int petscii_unicode(unsigned char p, bool lower);
char *utf8_encode(int c, char *out);
extern int modified;
extern int dir_error;

/* petscii.c: PETSCII and console output */
unsigned char a2p(unsigned char a);
char* petscii2ascii(const unsigned char* petscii, char* ascii, int len, bool slash);
void ascii2petscii(const unsigned char* ascii, unsigned char* petscii, int len);
void print_filename_with_escapes(const unsigned char* petscii, int len);
int pstrlen(const unsigned char* string);
void pputnum(unsigned char* string, unsigned int num);
void pputnum2(unsigned char* string, unsigned int num);
void pputhex(unsigned char* string, unsigned int num);
void evalhexescape(const unsigned char* ascii, unsigned char* petscii, int len, unsigned char emptychar);
void reverse_print_on(void);
void reverse_print_off(void);
void putp(unsigned char petscii, FILE *file);
void print_petscii(unsigned char *petscii, int len);
void print_dirfilename(unsigned char* pfilename);
void print_filename(FILE *file, unsigned char* pfilename);

/* image.c: format table, geometry, BAM, directory and validation */
const format_t * fmt(image_type type);
unsigned int image_num_tracks(image_type type);
int num_sectors(image_type type, int track);
int max_sectors(image_type type);
unsigned int image_num_blocks(image_type type);
unsigned int image_size(image_type type);
int dirtrack(image_type type);
int first_dir_sector(image_type type);
int side_tracks(image_type type);

/* block allocation strategy (--alloc) */
typedef enum {
    ALLOC_CC1541,               /* as cc1541: from track 1 upwards */
    ALLOC_DOS,                  /* as the drive's DOS: from the directory track outwards */
    ALLOC_AUTO                  /* planned, see alloc.c */
} alloc_mode_t;
extern alloc_mode_t alloc_mode;
typedef struct {
    unsigned char track;
    unsigned char sector;
} ts_block;
int dos_free_blocks(image_type type, const unsigned char *image, int track);
int dos_getsec(image_type type, const unsigned char *image, int track, int start);
/* rel.c: REL files (data + side sector index) */
#define REL_MAX_INDEX_BLOCKS (1 + 126 * 6)   /* super side sector + side sectors */
bool rel_has_super_side_sector(image_type type);
int rel_index_blocks(image_type type, const unsigned char *image, const unsigned char *entry, ts_block *blocks, int max);
const char *rel_check_index(image_type type, const unsigned char *image, const unsigned char *entry);
int rel_index_count(image_type type, int data_blocks);
int rel_write(image_type type, unsigned char *image, unsigned char *entry, const unsigned char *data, int size,
              int record_length, const ts_block *plan);

/* config.c: cit.ini */
typedef struct {
    const char *petcat;         /* path of petcat, NULL = search PATH */
    const char *sevenzip;       /* path of 7-Zip (7z, 7za, 7zz), NULL = search PATH;
                                   for reading and writing .zip/.7z (planned) */
    const char *osfmount;       /* path of OSFMount.com, NULL = default, see
                                   osfmount_path(); for --sd-mount */
    char sd_letter;             /* drive letter for --sd-mount, 0 = S */
    int text_eol;               /* -1 = of the system, else EOL_* */
    int text_upper;             /* -1/0 = lowercase/uppercase set, 1 = uppercase/graphics */
    char csv_sep;               /* 0 = ';' */
    int seq_text;               /* -1 = 90: from this share a SEQ is text */
    int seq_mixed;              /* -1 = 75: from this share text with a note */
    const char *seq00;          /* text for $00 in SEQ, "eol", NULL = {0x00} */
    const char *rel00;          /* text for $00 in REL, "sep", NULL = {0x00} */
    int alloc_new;              /* strategy for a new image, -1 = built-in */
    int alloc_existing;         /* strategy when adding files, -1 = built-in */
    const char *disk_name;      /* disk name without -n, NULL = "cit" */
    const char *path;           /* the file that was read, NULL = none */
} cit_config;
extern cit_config config;
void load_config(const char *program, const char *explicit_path);
const char *osfmount_path(void);
int eol_from_name(const char *s);
char sep_from_name(const char *s);

/* textconv.c: PETSCII files as text (-XA) */
enum { EOL_CRLF, EOL_LF, EOL_CR };
enum { SEQ_AUTO, SEQ_RAW, SEQ_ASCII, SEQ_UTF8 };
typedef struct {
    int eol;                    /* EOL_* */
    bool upper;                 /* uppercase/graphics character set */
    int seq_mode;               /* SEQ_* */
    char csv_sep;
    int seq_text;               /* percent */
    int seq_mixed;              /* percent */
    const char *seq00;          /* NULL, "eol" or a text */
    const char *rel00;          /* NULL, "sep" or a text */
} text_options;
extern text_options textopt;
void text_options_init(void);
int check_zero_text(const char *rep, const char *option);
int text_share(const unsigned char *data, int size);
char *seq_to_text(const unsigned char *data, int size, bool utf8, size_t *len);
int text_to_seq(const char *text, size_t len, const char *what, unsigned char **data, int *size);
char *rel_to_csv(const unsigned char *data, int size, int record_length, size_t *len);
int csv_to_rel(const char *text, size_t len, const char *what, int *record_length, unsigned char **data, int *size);

int convert_written_files(imagefile *files, int num_files);
void extract_files(image_type type, unsigned char *image, char *patterns[], const bool *converted, int num_patterns);
int convert_local_file(const char *fname, const char *path, unsigned char *data, int size, char *cbmname,
                       size_t cbmsize, int *filetype, int *record_length, unsigned char **out, int *out_size);

/* util.c */
int read_whole_file(const char *path, unsigned char **data, long *size);
int run_program(const char *prog, const char *const args[], bool quiet);
int temp_file_name(char *name, size_t size);

/* basic.c: BASIC programs as listings through petcat */
int basic_to_listing(const unsigned char *data, int size, const char *cbmname, char **text, size_t *len,
                     char *tags, size_t tags_size);
int listing_to_basic(const char *text, size_t len, unsigned int load_address, const char *version,
                     const char *what, unsigned char **data, int *size);

/* tools.c: looking into images */
unsigned long crc32_of(const unsigned char *data, int size);
int type_from_size(long size, bool *error_table);
/* all checksums of one file, see tools.c */
typedef struct {
    unsigned long crc32;
    unsigned int crc16_xmodem;
    unsigned int crc16_ccitt;   /* CRC-16/CCITT-FALSE, start $FFFF */
    unsigned int crc16_arc;
    unsigned char xor8;         /* CBM tape and disk block checksum */
    unsigned char cbm;          /* CBM ROM checksum: add with carry */
    unsigned long sum32;        /* plain sum, 32, 16 and 8 bit */
    unsigned int sum16;
    unsigned char sum8;
    unsigned char md5[16];
    unsigned char sha1[20];
} file_sums;
void sums_of(const unsigned char *data, long size, file_sums *s);
int tool_sums_files(int count, char **paths);
int sd_main(int argc, char **argv);
void tool_sums(image_type type, const unsigned char *image);
void tool_layout(image_type type, const unsigned char *image, const char *pattern);
bool tool_block(image_type type, const unsigned char *image, const char *arg);
bool tool_poke(image_type type, unsigned char *image, const char *arg);
int tool_diff(image_type type, const unsigned char *image, const char *other_path);
int tool_check(image_type type, const unsigned char *image);
void tool_json(image_type type, const unsigned char *image);

/* copy.c (--convert): the directory entries of an image with their data */
typedef enum {
    ENTRY_FILE,                 /* an entry with its own blocks */
    ENTRY_DUMMY,                /* no blocks (directory art, separators) */
    ENTRY_LINK                  /* points to the blocks of an earlier entry */
} entry_kind;

typedef struct {
    entry_kind kind;
    unsigned char filetype;     /* as in the entry: DEL, locked, unclosed ... */
    unsigned char name[FILENAMEMAXSIZE];
    unsigned char record_length;
    int shown_blocks;           /* block count as shown in the directory */
    int link;                   /* ENTRY_LINK: index of the entry it points to */
    int start_track;
    int start_sector;
    bool broken;                /* ENTRY_FILE whose block chain is broken (no data) */
    unsigned char *data;
    int size;
} source_file;
int read_source_files(image_type type, const unsigned char *image, source_file **out, bool warn);

bool same_file(const char *a, const char *b);
int load_source_image(const char *path, image_type *type, unsigned char **image);
void source_header(image_type type, const unsigned char *image, char *header, char *id); /* 3 * 16 + 1, 3 * 2 + 1 */
int add_files_from_image(image_type type, const unsigned char *image, imagefile *files, int *num_files,
                         int sector_interleave, int first_sector_new_track, bool new_image);
int add_files_from_folder(const char *folder, imagefile *files, int *num_files,
                          int sector_interleave, int first_sector_new_track);
int unpack_to_folder(image_type type, const unsigned char *image, const char *folder);
ts_block **auto_plan(image_type type, const unsigned char *image, imagefile *files, int num_files, const int *num_blocks, int star_file);
bool dos_first_block(image_type type, const unsigned char *image, int *track, int *sector);
bool dos_next_block(image_type type, const unsigned char *image, int *track, int *sector, int increment);
bool is_system_block(image_type type, int track, int sector);
bool swap_dir_entries(image_type type, unsigned char *image, const unsigned char *pname, int position);
int bam_track_free(image_type type, const unsigned char *image, int track);
int header_sector(image_type type);
int hd_bam_blocks(image_type type);
void hd_bam_block(image_type type, int i, int *track, int *sector, int *low, int *high);
int linear_sector(image_type type, int track, int sector);
int is_sector_free(image_type type, const unsigned char* image, int track, int sector, int numdirblocks, int dir_sector_interleave);
void mark_sector(image_type type, unsigned char* image, int track, int sector, int free);
int get_header_offset(image_type type);
int get_id_offset(image_type type);
void update_directory(image_type type, unsigned char* image, unsigned char* header, unsigned char* id, unsigned char *bam_message, int shadowdirtrack);
void initialize_directory(image_type type, unsigned char* image, unsigned char* header, unsigned char* id, unsigned char * bam_message, int shadowdirtrack, int usedirtrack);
bool next_dir_entry(image_type type, const unsigned char* image, int *track, int *sector, int *offset, char *blockmap);
bool match_filename(const unsigned char *pattern, const unsigned char *filename);
bool find_existing_file(image_type type, unsigned char* image, unsigned char* filename, int *index, int *track, int *sector, int *offset);
void new_dir_slot(image_type type, unsigned char* image, int dir_sector_interleave, int shadowdirtrack, int *index, int *dirsector,  int *entry_offset, imagefile files[]);
bool find_dir_slot(image_type type, unsigned char* image, unsigned char* filename, int dir_sector_interleave, int shadowdirtrack, int *index, int *dirsector,  int *entry_offset, imagefile files[]);
int count_blocks(image_type type, unsigned char *image, unsigned int track, int sector);
void mark_sector_chain(image_type type, unsigned char *image, char* atab, unsigned int track, int sector, unsigned int last_track, int last_sector, int mark);
int validate_sector_chain(image_type type, unsigned char* image, char* atab, unsigned int track, int sector, unsigned int *last_track, int *last_sector);
void init_atab(image_type type, unsigned char* image, char* atab);
void write_atab(image_type type, unsigned char* image, char* atab);
void validate(image_type type, unsigned char* image);

/* gcr.c: GCR coding of the 1541 */
extern const unsigned char NIBBLE_TO_GCR[];
void encode_4_bytes_gcr(char* in, char* out);
void generate_gcr_decoding_table(const unsigned char nibble_to_gcr[], int8_t gcr_to_nibble[]);
unsigned char even_bits(unsigned char value);
unsigned char odd_bits(unsigned char value);
bool decode_5_bytes_gcr(const int8_t decoding_map[], const unsigned char *in, unsigned char *out);
int decode_gcr_block(const int8_t decoding_map[], const unsigned char *encoded, unsigned char decoded[]);

/* transwarp.c: Transwarp files and Krill's loader filename hash */
unsigned int filenamehash(const unsigned char *filename);
unsigned char transwarp_dirdata_checksum(const unsigned char *image, int dir_entry_offset);
bool is_transwarp_file(const unsigned char *image, int dir_entry_offset);
bool is_transwarp_bootfile(const unsigned char *image, int dir_entry_offset);
int transwarp_stat(image_type type, const unsigned char *image, int dir_entry_offset, int *start_track, int *end_track, int *low_track, int *high_track);
int transwarp_size(image_type type, int start_track, int end_track, int filesize, int *transwarp_blocks, int *nonredundant_blocks, int *redundant_blocks, int *nonredundant_blocks_on_last_track);
bool check_hashes(image_type type, const unsigned char* image);
unsigned char crc8(unsigned char value);
unsigned long long write_transwarp_file(image_type type, unsigned char *image, imagefile *file, unsigned char *filedata, int *filesize, unsigned int version, bool transwarp_bootfile_fits_on_dir_track);
void optimize_transwarp_allocation(image_type type, unsigned char *image, imagefile *files, int num_files, int order[]);

/* raw.c: track level images (G64 output) */
int generate_uniformat_g64(unsigned char* image, const char *imagepath);

/* undelete.c: restoring deleted and formatted files */
void restore(image_type type, unsigned char* image, int level, imagefile files[]);

/* cit.c: command line, writing and listing files */
int check_bam(image_type type, const unsigned char* image);

#endif /* CIT_H */
