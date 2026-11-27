/*
 * copy.c - --from: copy all files of an image or a folder
 *
 *   image  -> new image       copy, laid out anew (auto), also into another format
 *   image  -> existing image  files are added (dos)
 *   image  -> folder          every file as a PC64 x00 file (P00/S00/U00/R00)
 *   folder -> image           x00 files with their real name and type, other
 *                             files as PRG named after the file
 *
 * The source is only read. Whatever is not reachable through the directory
 * (used blocks of no file, error table, REL side sectors, GEOS info) is not
 * copied; a warning says so.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"
#include <dirent.h>
#ifdef _WIN32
#include <io.h>        /* mkdir */
#endif

#define X00_HEADER_SIZE 26          /* "C64File" 0, name 16 + 0, REL record size */

/* image type from the file size of a source image, -1 = unknown */
static int
type_from_size(long size, bool *error_table)
{
    static const struct {
        image_type type;
    } candidates[] = {
        { IMAGE_D64 }, { IMAGE_D71 }, { IMAGE_D81 }, { IMAGE_D80 }, { IMAGE_D82 }
    };
    for (unsigned int i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        image_type t = candidates[i].type;
        long blocks = image_num_blocks(t);
        if (size == blocks * BLOCKSIZE) {
            *error_table = false;
            return t;
        }
        if (size == blocks * (BLOCKSIZE + 1)) {
            *error_table = true;
            return t;
        }
    }
    return -1;
}

/* true if both paths name the same existing file */
bool
same_file(const char *a, const char *b)
{
#ifdef _WIN32
    /* no inode numbers on Windows: compare the full paths */
    char fa[_MAX_PATH];
    char fb[_MAX_PATH];
    if (_fullpath(fa, a, sizeof fa) == NULL || _fullpath(fb, b, sizeof fb) == NULL) {
        return false;
    }
    return strcasecmp(fa, fb) == 0;
#else
    struct stat sa;
    struct stat sb;
    return stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

int
load_source_image(const char *path, image_type *type, unsigned char **image)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: Could not open source image %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool error_table;
    int t = type_from_size(size, &error_table);
    if (t < 0) {
        fprintf(stderr, "ERROR: Unknown image size %ld of %s\n", size, path);
        fclose(f);
        return -1;
    }
    *type = (image_type)t;
    *image = malloc(size);
    if (*image == NULL || fread(*image, 1, size, f) != (size_t)size) {
        fprintf(stderr, "ERROR: Could not read source image %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (error_table) {
        fprintf(stderr, "WARNING: The error table of %s is not copied\n", path);
    }
    return 0;
}

/* data of a file by its block chain; returns the size, -1 if the chain is broken */
static int
read_chain(image_type type, const unsigned char *image, int track, int sector, unsigned char **data, char *used)
{
    int size = 0;
    int capacity = 0;
    *data = NULL;
    while (track != 0) {
        if (track > (int)image_num_tracks(type) || sector >= num_sectors(type, track)) {
            return -1;
        }
        int b = linear_sector(type, track, sector);
        if (used[b]) {
            return -1; /* loop or block of another file */
        }
        used[b] = 1;
        const unsigned char *block = image + b * BLOCKSIZE;
        int n = (block[0] == 0) ? block[1] - 1 : BLOCKSIZE - BLOCKOVERHEAD;
        if (n < 0) {
            n = 0;
        }
        if (size + n > capacity) {
            capacity = (capacity + n) * 2;
            *data = realloc(*data, capacity);
            if (*data == NULL) {
                fprintf(stderr, "ERROR: Memory allocation error\n");
                exit(-1);
            }
        }
        memcpy(*data + size, block + 2, n);
        size += n;
        track = block[0];
        sector = block[1];
    }
    return size;
}

/* hex escaped copy of PETSCII bytes up to the first A0 (for -n/-i);
   s must hold 3 * len + 1 characters */
static void
escaped(char *s, const unsigned char *p, int len)
{
    int n = 0;
    for (int i = 0; i < len && p[i] != FILENAMEEMPTYCHAR; i++) {
        n += sprintf(s + n, "#%02x", p[i]);
    }
    s[n] = 0;
}

void
source_header(image_type type, const unsigned char *image, char *header, char *id)
{
    const unsigned char *h = image + linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
    escaped(header, h + fmt(type)->header_offset, FILENAMEMAXSIZE);
    escaped(id, h + fmt(type)->id_offset, 2);
}

/* one directory entry of the source, NULL at the end */
typedef struct {
    unsigned char filetype;
    unsigned char name[FILENAMEMAXSIZE];
    unsigned char record_length;
    unsigned char *data;
    int size;
} source_file;

/* all files of the source image in directory order; used blocks that belong
   to no copied file are counted for a warning */
static int
read_source_files(image_type type, const unsigned char *image, source_file **out, bool keep_rel)
{
    char *used = calloc(image_num_blocks(type), 1);
    char *blockmap = calloc(image_num_blocks(type), 1);
    source_file *files = calloc(MAXNUMFILES, sizeof(source_file));
    if (used == NULL || blockmap == NULL || files == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int n = 0;
    int dt = dirtrack(type);
    int ds = first_dir_sector(type);
    int offset = 0;
    do {
        const unsigned char *e = image + linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        used[linear_sector(type, dt, ds)] = 1;
        int ft = e[FILETYPEOFFSET];
        if ((ft & 7) == FILETYPEDEL && !(ft & 0x80)) {
            continue; /* empty slot or deleted file */
        }
        char aname[3 * FILENAMEMAXSIZE + 1];
        petscii2ascii((unsigned char *)e + FILENAMEOFFSET, aname, FILENAMEMAXSIZE, false);
        if (!(ft & 0x80)) {
            fprintf(stderr, "WARNING: Unclosed file \"%s\" is not copied\n", aname);
            continue;
        }
        if ((ft & 7) == FILETYPEREL && !keep_rel) {
            fprintf(stderr, "WARNING: REL file \"%s\" is not copied (side sectors are not written yet)\n", aname);
            continue;
        }
        if (e[0x18] != 0) {
            fprintf(stderr, "WARNING: GEOS file \"%s\" is not copied\n", aname);
            continue;
        }
        if (n >= MAXNUMFILES) {
            fprintf(stderr, "ERROR: Too many files in the source image\n");
            exit(-1);
        }
        source_file *f = &files[n];
        f->filetype = ft;
        memcpy(f->name, e + FILENAMEOFFSET, FILENAMEMAXSIZE);
        f->record_length = e[0x15];
        f->size = read_chain(type, image, e[FILETRACKOFFSET], e[FILESECTOROFFSET], &f->data, used);
        if (f->size < 0) {
            fprintf(stderr, "WARNING: File \"%s\" has a broken block chain and is not copied\n", aname);
            free(f->data);
            continue;
        }
        if ((ft & 7) == FILETYPEREL) {
            /* side sectors are rebuilt by the drive, they hold no data */
            int st = e[0x13];
            int ss = e[0x14];
            while (st != 0 && st <= (int)image_num_tracks(type) && ss < num_sectors(type, st)
                    && !used[linear_sector(type, st, ss)]) {
                used[linear_sector(type, st, ss)] = 1;
                const unsigned char *b = image + linear_sector(type, st, ss) * BLOCKSIZE;
                st = b[0];
                ss = b[1];
            }
        }
        n++;
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));

    /* used blocks that are neither header, BAM, directory nor part of a copied file */
    int lost = 0;
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        if ((int)t == fmt(type)->second_bam_track) {
            continue;
        }
        for (int s = 0; s < num_sectors(type, t); s++) {
            if (!is_sector_free(type, image, t, s, 0, 0) && !used[linear_sector(type, t, s)]
                    && !is_system_block(type, t, s)) {
                lost++;
            }
        }
    }
    if (lost > 0) {
        fprintf(stderr, "WARNING: %d used block%s belong%s to no copied file and %s not copied\n",
                lost, lost == 1 ? "" : "s", lost == 1 ? "s" : "", lost == 1 ? "is" : "are");
    }
    free(used);
    free(blockmap);
    *out = files;
    return n;
}

/* adds the files of a source image to the files to write */
int
add_files_from_image(image_type type, const unsigned char *image, imagefile *files, int *num_files,
                     int sector_interleave, int first_sector_new_track)
{
    source_file *src;
    int n = read_source_files(type, image, &src, false);
    for (int i = 0; i < n; i++) {
        if (*num_files >= MAXNUMFILES) {
            fprintf(stderr, "ERROR: Too many files\n");
            exit(-1);
        }
        imagefile *f = &files[*num_files];
        char *aname = malloc(3 * FILENAMEMAXSIZE + 1);
        if (aname == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        petscii2ascii(src[i].name, aname, FILENAMEMAXSIZE, false);
        f->alocalname = (unsigned char *)aname;
        memcpy(f->pfilename, src[i].name, FILENAMEMAXSIZE);
        f->filetype = src[i].filetype;
        f->sectorInterleave = sector_interleave;
        f->first_sector_new_track = first_sector_new_track;
        f->nrSectorsShown = -1;
        f->direntryindex = -1;
        f->data = src[i].data;
        f->data_size = src[i].size;
        f->has_data = true;
        f->data_alloc = src[i].data;
        f->name_alloc = aname;
        (*num_files)++;
    }
    free(src);
    return n;
}

/* host name for an x00 file: number (keeps the directory order), name, extension */
static void
x00_name(char *out, size_t len, int number, const unsigned char *name, int filetype)
{
    static const char letter[] = { 'd', 's', 'p', 'u', 'r' };
    char aname[3 * FILENAMEMAXSIZE + 1];
    petscii2ascii((unsigned char *)name, aname, FILENAMEMAXSIZE, false);
    for (char *c = aname; *c; c++) {
        if (strchr("<>:\"/\\|?*", *c) || (unsigned char)*c < 32) {
            *c = '_';
        }
    }
    int t = filetype & 7;
    snprintf(out, len, "%03d_%s.%c00", number, aname, (t <= 4) ? letter[t] : 'u');
}

int
unpack_to_folder(image_type type, const unsigned char *image, const char *folder)
{
    struct stat st;
    if (stat(folder, &st) != 0) {
#ifdef _WIN32
        if (mkdir(folder) != 0) {
#else
        if (mkdir(folder, 0777) != 0) {
#endif
            fprintf(stderr, "ERROR: Could not create folder %s\n", folder);
            return -1;
        }
    }
    source_file *src;
    int n = read_source_files(type, image, &src, true);
    for (int i = 0; i < n; i++) {
        char name[3 * FILENAMEMAXSIZE + 16];
        x00_name(name, sizeof name, i + 1, src[i].name, src[i].filetype);
        size_t plen = strlen(folder) + strlen(name) + 2;
        char *path = malloc(plen);
        if (path == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        snprintf(path, plen, "%s/%s", folder, name);
        FILE *f = fopen(path, "wb");
        if (f == NULL) {
            fprintf(stderr, "ERROR: Could not write %s\n", path);
            return -1;
        }
        unsigned char header[X00_HEADER_SIZE] = { 'C', '6', '4', 'F', 'i', 'l', 'e', 0 };
        for (int k = 0; k < FILENAMEMAXSIZE; k++) {
            /* x00 names are padded with 0 instead of A0 */
            header[8 + k] = (src[i].name[k] == FILENAMEEMPTYCHAR) ? 0 : src[i].name[k];
        }
        header[25] = ((src[i].filetype & 7) == FILETYPEREL) ? src[i].record_length : 0;
        if (fwrite(header, 1, X00_HEADER_SIZE, f) != X00_HEADER_SIZE
                || (src[i].size > 0 && fwrite(src[i].data, 1, src[i].size, f) != (size_t)src[i].size)) {
            fprintf(stderr, "ERROR: Could not write %s\n", path);
            return -1;
        }
        fclose(f);
        if (!quiet) {
            printf("%s\n", path);
        }
        free(path);
        free(src[i].data);
    }
    free(src);
    return n;
}

static int
compare_names(const void *a, const void *b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

/* adds the files of a folder, sorted by file name: x00 files with the name
   and type of their header, all other files as PRG named after the file */
int
add_files_from_folder(const char *folder, imagefile *files, int *num_files,
                      int sector_interleave, int first_sector_new_track)
{
    DIR *d = opendir(folder);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Could not read folder %s\n", folder);
        return -1;
    }
    char **names = NULL;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') {
            continue;
        }
        names = realloc(names, sizeof(char *) * (count + 1));
        if (names != NULL) {
            names[count] = malloc(strlen(de->d_name) + 1);
            if (names[count] != NULL) {
                strcpy(names[count], de->d_name);
            }
        }
        if (names == NULL || names[count] == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        count++;
    }
    closedir(d);
    qsort(names, count, sizeof(char *), compare_names);

    int added = 0;
    for (int i = 0; i < count; i++) {
        size_t plen = strlen(folder) + strlen(names[i]) + 2;
        char *path = malloc(plen);
        if (path == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        snprintf(path, plen, "%s/%s", folder, names[i]);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(path);
            continue;
        }
        FILE *f = fopen(path, "rb");
        unsigned char *data = malloc(st.st_size + 1);
        if (f == NULL || data == NULL || fread(data, 1, st.st_size, f) != (size_t)st.st_size) {
            fprintf(stderr, "ERROR: Could not read %s\n", path);
            exit(-1);
        }
        fclose(f);
        if (*num_files >= MAXNUMFILES) {
            fprintf(stderr, "ERROR: Too many files\n");
            exit(-1);
        }
        imagefile *fi = &files[*num_files];
        const char *ext = strrchr(names[i], '.');
        bool x00 = ext != NULL && strlen(ext) == 4 && strchr("pPsSuUrR", ext[1])
                   && isdigit((unsigned char)ext[2]) && isdigit((unsigned char)ext[3])
                   && st.st_size >= X00_HEADER_SIZE && memcmp(data, "C64File", 8) == 0;
        if (x00) {
            int t = strchr("sS", ext[1]) ? FILETYPESEQ : strchr("uU", ext[1]) ? FILETYPEUSR
                    : strchr("rR", ext[1]) ? FILETYPEREL : FILETYPEPRG;
            if (t == FILETYPEREL) {
                fprintf(stderr, "WARNING: REL file %s is not copied (side sectors are not written yet)\n", names[i]);
                free(data);
                free(path);
                continue;
            }
            for (int k = 0; k < FILENAMEMAXSIZE; k++) {
                unsigned char c = data[8 + k];
                fi->pfilename[k] = (c == 0) ? FILENAMEEMPTYCHAR : c;
            }
            /* stop padding at the first 0 of the x00 name */
            for (int k = 0; k < FILENAMEMAXSIZE; k++) {
                if (data[8 + k] == 0) {
                    for (; k < FILENAMEMAXSIZE; k++) {
                        fi->pfilename[k] = FILENAMEEMPTYCHAR;
                    }
                }
            }
            fi->filetype = 0x80 | t;
            fi->data = data + X00_HEADER_SIZE;
            fi->data_size = (int)st.st_size - X00_HEADER_SIZE;
        } else {
            ascii2petscii((unsigned char *)names[i], fi->pfilename, FILENAMEMAXSIZE);
            fi->filetype = 0x82;
            fi->data = data;
            fi->data_size = (int)st.st_size;
        }
        fi->alocalname = (unsigned char *)path;
        fi->has_data = true;
        fi->data_alloc = data;
        fi->name_alloc = path;
        fi->sectorInterleave = sector_interleave;
        fi->first_sector_new_track = first_sector_new_track;
        fi->nrSectorsShown = -1;
        fi->direntryindex = -1;
        (*num_files)++;
        added++;
        free(names[i]);
    }
    free(names);
    return added;
}
