/*
 * copy.c - --convert: copy all files of an image or a folder
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
    unsigned char *data;
    int size;
} source_file;

/* all files of the source image in directory order; used blocks that belong
   to no copied file are counted for a warning */
static int
read_source_files(image_type type, const unsigned char *image, source_file **out)
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
        if (e[0x18] != 0) {
            fprintf(stderr, "WARNING: GEOS file \"%s\" is not copied\n", aname);
            continue;
        }
        if (n >= MAXNUMFILES) {
            fprintf(stderr, "ERROR: Too many files in the source image\n");
            exit(-1);
        }
        /* every entry is taken over as it is: type byte, the raw name and the
           block count shown, so that directory art looks the same */
        source_file *f = &files[n];
        memset(f, 0, sizeof *f);
        f->filetype = ft;
        memcpy(f->name, e + FILENAMEOFFSET, FILENAMEMAXSIZE);
        f->record_length = e[0x17];
        f->shown_blocks = e[FILEBLOCKSLOOFFSET] | (e[FILEBLOCKSHIOFFSET] << 8);
        f->start_track = e[FILETRACKOFFSET];
        f->start_sector = e[FILESECTOROFFSET];
        if (f->start_track == 0 || !(ft & 0x80) || (ft & 7) == FILETYPEDEL) {
            /* no blocks, unclosed or DEL: the entry only */
            if (f->start_track != 0 && !(ft & 0x80)) {
                fprintf(stderr, "WARNING: Unclosed file \"%s\": only the directory entry is copied\n", aname);
            }
            f->kind = ENTRY_DUMMY;
            n++;
            continue;
        }
        int same = -1;
        for (int k = 0; k < n; k++) {
            if (files[k].kind == ENTRY_FILE && files[k].start_track == f->start_track && files[k].start_sector == f->start_sector) {
                same = k;
                break;
            }
        }
        if (same >= 0) {
            /* a second entry for the same blocks (directory art) */
            f->kind = ENTRY_LINK;
            f->link = same;
            n++;
            continue;
        }
        f->kind = ENTRY_FILE;
        f->size = read_chain(type, image, f->start_track, f->start_sector, &f->data, used);
        if (f->size < 0) {
            fprintf(stderr, "WARNING: File \"%s\" has a broken block chain and is not copied\n", aname);
            free(f->data);
            continue;
        }
        if ((ft & 7) == FILETYPEREL) {
            /* the index belongs to the file; it is rebuilt when writing */
            ts_block index[REL_MAX_INDEX_BLOCKS];
            int ni = rel_index_blocks(type, image, e, index, REL_MAX_INDEX_BLOCKS);
            for (int k = 0; k < ni; k++) {
                used[linear_sector(type, index[k].track, index[k].sector)] = 1;
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
                     int sector_interleave, int first_sector_new_track, bool new_image)
{
    source_file *src;
    int n = read_source_files(type, image, &src);
    int first = *num_files;
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
        f->name_alloc = aname;
        memcpy(f->pfilename, src[i].name, FILENAMEMAXSIZE);
        f->filetype = src[i].filetype;
        f->sectorInterleave = sector_interleave;
        f->first_sector_new_track = first_sector_new_track;
        f->nrSectorsShown = src[i].shown_blocks;
        f->direntryindex = -1;
        /* in a new image every entry gets its own slot, so that equal names
           (separators of directory art) do not replace each other */
        f->force_new = new_image || src[i].kind != ENTRY_FILE;
        switch (src[i].kind) {
        case ENTRY_DUMMY:
            f->no_file = true;
            f->nrSectors = 0;
            break;
        case ENTRY_LINK:
            f->loop_file = true;
            f->sectorInterleave = 0;
            f->loop_target = first + src[i].link + 1;
            memcpy(f->plocalname, src[src[i].link].name, FILENAMEMAXSIZE);
            break;
        case ENTRY_FILE:
            f->data = src[i].data;
            f->data_size = src[i].size;
            f->has_data = true;
            f->data_alloc = src[i].data;
            f->record_length = src[i].record_length;
            break;
        }
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

/* PETSCII name in petcat notation: printable characters as they are,
   everything else as {$xx} */
static void
petcat_name(char *out, const unsigned char *p, int len)
{
    int n = 0;
    for (int i = 0; i < len && p[i] != FILENAMEEMPTYCHAR; i++) {
        unsigned char c = p[i];
        if (c >= 0x20 && c < 0x40) {
            out[n++] = c;
        } else if (c >= 0x41 && c <= 0x5a) {
            out[n++] = c + 0x20;          /* unshifted letters list as lower case */
        } else if (c >= 0xc1 && c <= 0xda) {
            out[n++] = c - 0x80;          /* shifted letters as upper case */
        } else {
            n += sprintf(out + n, "{$%02x}", c);
        }
    }
    out[n] = 0;
}

/* directory.txt: the directory as a listing, for reading only (directory
   art, block counts shown, entries without blocks, links); not read back */
static void
write_directory_txt(image_type type, const unsigned char *image, const char *folder, const source_file *src, int n)
{
    size_t plen = strlen(folder) + 16;
    char *path = malloc(plen);
    if (path == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    snprintf(path, plen, "%s/directory.txt", folder);
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "ERROR: Could not write %s\n", path);
        exit(-1);
    }
    static const char *types[] = { "del", "seq", "prg", "usr", "rel", "???", "???", "???" };
    const unsigned char *h = image + linear_sector(type, dirtrack(type), 0) * BLOCKSIZE;
    char name[6 * FILENAMEMAXSIZE + 1];
    char id[6 * 5 + 1];
    petcat_name(name, h + fmt(type)->header_offset, FILENAMEMAXSIZE);
    petcat_name(id, h + fmt(type)->id_offset, 5);
    fprintf(f, "0 \"%s\" %s\n", name, id);
    for (int i = 0; i < n; i++) {
        petcat_name(name, src[i].name, FILENAMEMAXSIZE);
        char q[6 * FILENAMEMAXSIZE + 3];
        snprintf(q, sizeof q, "\"%s\"", name);
        fprintf(f, "%-5d %-18s %c%s%c", src[i].shown_blocks, q,
                (src[i].filetype & 0x80) ? ' ' : '*', types[src[i].filetype & 7],
                (src[i].filetype & 0x40) ? '<' : ' ');
        if (src[i].kind == ENTRY_DUMMY) {
            fprintf(f, "  ; %03d no blocks", i + 1);
        } else if (src[i].kind == ENTRY_LINK) {
            fprintf(f, "  ; %03d same blocks as %03d", i + 1, src[i].link + 1);
        } else {
            fprintf(f, "  ; %03d", i + 1);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    free(path);
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
    int n = read_source_files(type, image, &src);
    write_directory_txt(type, image, folder, src, n);
    for (int i = 0; i < n; i++) {
        /* entries without blocks: 0 byte x00 files; links: a copy of the data */
        const source_file *data = (src[i].kind == ENTRY_LINK) ? &src[src[i].link] : &src[i];
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
                || (data->size > 0 && fwrite(data->data, 1, data->size, f) != (size_t)data->size)) {
            fprintf(stderr, "ERROR: Could not write %s\n", path);
            return -1;
        }
        fclose(f);
        if (!quiet) {
            printf("%s\n", path);
        }
        free(path);
    }
    for (int i = 0; i < n; i++) {
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
        if (de->d_name[0] == '.' || strcasecmp(de->d_name, "directory.txt") == 0) {
            continue; /* hidden files; the listing written by unpacking */
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
        bool x00 = ext != NULL && strlen(ext) == 4 && strchr("pPsSuUrRdD", ext[1])
                   && isdigit((unsigned char)ext[2]) && isdigit((unsigned char)ext[3])
                   && st.st_size >= X00_HEADER_SIZE && memcmp(data, "C64File", 8) == 0;
        if (x00) {
            int t = strchr("sS", ext[1]) ? FILETYPESEQ : strchr("uU", ext[1]) ? FILETYPEUSR
                    : strchr("rR", ext[1]) ? FILETYPEREL : strchr("dD", ext[1]) ? FILETYPEDEL : FILETYPEPRG;
            if (t == FILETYPEREL) {
                fi->record_length = data[25];
                if (fi->record_length == 0) {
                    fprintf(stderr, "WARNING: R00 file %s has no record length and is not copied\n", names[i]);
                    free(data);
                    free(path);
                    continue;
                }
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
            if (fi->data_size == 0) {
                /* no data: a directory entry without blocks (directory art) */
                fi->no_file = true;
                fi->nrSectors = 0;
                fi->nrSectorsShown = 0;
                fi->force_new = true;
                fi->record_length = 0;
            }
        } else {
            /* plain files as -X writes them: name.prg/.seq/.usr, name[rsNN].rel;
               anything else is a PRG with the whole file name */
            char cbmname[3 * FILENAMEMAXSIZE + 16];
            snprintf(cbmname, sizeof cbmname, "%s", names[i]);
            int t = FILETYPEPRG;
            size_t l = strlen(cbmname);
            if (l > 4 && cbmname[l - 4] == '.') {
                const char *e = cbmname + l - 3;
                if (strcasecmp(e, "prg") == 0) {
                    t = FILETYPEPRG;
                    cbmname[l - 4] = 0;
                } else if (strcasecmp(e, "seq") == 0) {
                    t = FILETYPESEQ;
                    cbmname[l - 4] = 0;
                } else if (strcasecmp(e, "usr") == 0) {
                    t = FILETYPEUSR;
                    cbmname[l - 4] = 0;
                } else if (strcasecmp(e, "rel") == 0) {
                    cbmname[l - 4] = 0;
                    char *rs = strrchr(cbmname, '[');
                    int rl = 0;
                    char close = 0;
                    if (rs == NULL || sscanf(rs, "[rs%d%c", &rl, &close) != 2 || close != ']'
                            || rs[strlen(rs) - 1] != ']' || rl < 1 || rl > 254) {
                        fprintf(stderr, "WARNING: %s has no record length [rsNN] and is not copied\n", names[i]);
                        free(data);
                        free(path);
                        continue;
                    }
                    *rs = 0;
                    t = FILETYPEREL;
                    fi->record_length = rl;
                }
            }
            ascii2petscii((unsigned char *)cbmname, fi->pfilename, FILENAMEMAXSIZE);
            fi->filetype = 0x80 | t;
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
