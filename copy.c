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
int
type_from_size(long size, bool *error_table)
{
    static const struct {
        image_type type;
    } candidates[] = {
        { IMAGE_D64 }, { IMAGE_D71 }, { IMAGE_D81 }, { IMAGE_D80 }, { IMAGE_D82 }, { IMAGE_D60 }, { IMAGE_D90 }
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
    const unsigned char *h = image + linear_sector(type, dirtrack(type), header_sector(type)) * BLOCKSIZE;
    escaped(header, h + fmt(type)->header_offset, FILENAMEMAXSIZE);
    escaped(id, h + fmt(type)->id_offset, 2);
}

/* one directory entry of the source, NULL at the end */


/* all files of the source image in directory order; used blocks that belong
   to no copied file are counted for a warning */
int
read_source_files(image_type type, const unsigned char *image, source_file **out, bool warn)
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
            /* kept in the list (the tools show it), copying skips it */
            free(f->data);
            f->data = NULL;
            f->size = 0;
            f->broken = true;
            n++;
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
    if (lost > 0 && warn) {
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
    int n = read_source_files(type, image, &src, true);
    int first = *num_files;
    for (int i = 0; i < n; i++) {
        if (*num_files >= MAXNUMFILES) {
            fprintf(stderr, "ERROR: Too many files\n");
            exit(-1);
        }
        if (src[i].broken || (src[i].kind == ENTRY_LINK && src[src[i].link].broken)) {
            char a[3 * FILENAMEMAXSIZE + 1];
            petscii2ascii(src[i].name, a, FILENAMEMAXSIZE, false);
            fprintf(stderr, "WARNING: File \"%s\" has a broken block chain and is not copied\n", a);
            continue;
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

/* blocks free as the directory shows them: all free blocks except the
   directory track (as check_bam counts them) */
static int
count_free_blocks(image_type type, const unsigned char *image)
{
    int n = 0;
    for (unsigned int t = 1; t <= image_num_tracks(type); t++) {
        if ((int)t == dirtrack(type) && !fmt(type)->heads) {
            continue; /* the floppy DOS does not count it, DOS 3.0 does */
        }
        for (int s = 0; s < num_sectors(type, t); s++) {
            n += is_sector_free(type, image, t, s, 0, 0) ? 1 : 0;
        }
    }
    return n;
}

/* directory.pet: the directory listing as the drive sends it, PETSCII 1:1
   with CR line ends (directory art, block counts shown, locked, unclosed);
   for reading with a PETSCII viewer, it is not read back */
static void
write_directory_pet(image_type type, const unsigned char *image, const char *folder, const source_file *src, int n)
{
    size_t plen = strlen(folder) + 16;
    char *path = malloc(plen);
    if (path == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    snprintf(path, plen, "%s/directory.pet", folder);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: Could not write %s\n", path);
        exit(-1);
    }
    /* "prg" etc. in PETSCII: the codes of the upper case ASCII letters */
    static const char *types[] = { "DEL", "SEQ", "PRG", "USR", "REL", "???", "???", "???" };
    const unsigned char *h = image + linear_sector(type, dirtrack(type), header_sector(type)) * BLOCKSIZE;
    /* header line: 0 "name" id, the name and id reversed like on the screen */
    fputs("0 \x12\"", f);
    fwrite(h + fmt(type)->header_offset, 1, FILENAMEMAXSIZE, f);
    fputs("\" ", f);
    fwrite(h + fmt(type)->id_offset, 1, 5, f);
    fputs("\x92\r", f);
    for (int i = 0; i < n; i++) {
        char blocks[8];
        int bl = snprintf(blocks, sizeof blocks, "%d", src[i].shown_blocks);
        fwrite(blocks, 1, bl, f);
        for (int k = bl; k < 5; k++) {
            fputc(' ', f);
        }
        /* the name up to the first A0 in quotes, the rest behind it, as listed */
        int len = 0;
        while (len < FILENAMEMAXSIZE && src[i].name[len] != FILENAMEEMPTYCHAR) {
            len++;
        }
        fputc('"', f);
        fwrite(src[i].name, 1, len, f);
        fputc('"', f);
        for (int k = len; k < FILENAMEMAXSIZE; k++) {
            fputc((src[i].name[k] == FILENAMEEMPTYCHAR) ? ' ' : src[i].name[k], f);
        }
        fputc((src[i].filetype & 0x80) ? ' ' : '*', f);
        fputs(types[src[i].filetype & 7], f);
        fputc((src[i].filetype & 0x40) ? '<' : ' ', f);
        fputc('\r', f);
    }
    fprintf(f, "%d BLOCKS FREE.\r", count_free_blocks(type, image));
    fclose(f);
    free(path);
}

/* trailing tags of a file name: [rs64], [la0801], [b40]; removes them */
static void
take_tags(char *name, int *rs, int *la, char *version, size_t vsize)
{
    for (;;) {
        size_t l = strlen(name);
        if (l < 3 || name[l - 1] != ']') {
            return;
        }
        char *open = strrchr(name, '[');
        if (open == NULL) {
            return;
        }
        int v;
        char close;
        if (sscanf(open, "[rs%d%c", &v, &close) == 2 && close == ']') {
            *rs = v;
        } else if (sscanf(open, "[la%x%c", (unsigned int *)&v, &close) == 2 && close == ']') {
            *la = v;
        } else if (open[1] == 'b' && strlen(open) - 3 < vsize) {
            snprintf(version, vsize, "%.*s", (int)(strlen(open) - 3), open + 2);
        } else {
            return;
        }
        *open = 0;
    }
}

/* a local file as CIT writes it, by its name:
     name.prg .seq .usr      as they are
     name[rsNN].rel          REL with that record length
     name.txt                SEQ from text (textconv.c)
     name[rsNN].csv          REL from CSV, without [rsNN] the longest line
     name[la0801][bNN].bas   BASIC from a listing (petcat)
   cbmname gets the name without extension and tags. Returns 0, 1 for an
   unknown extension (cbmname = whole name, PRG as it is), -1 on error.
   *out is data itself or a new buffer */
int
convert_local_file(const char *fname, const char *path, unsigned char *data, int size, char *cbmname,
                   size_t cbmsize, int *filetype, int *record_length, unsigned char **out, int *out_size)
{
    snprintf(cbmname, cbmsize, "%s", fname);
    *filetype = FILETYPEPRG;
    *record_length = 0;
    *out = data;
    *out_size = size;
    size_t l = strlen(cbmname);
    if (l < 5 || cbmname[l - 4] != '.') {
        return 1;
    }
    char ext[4];
    snprintf(ext, sizeof ext, "%s", cbmname + l - 3);
    for (int i = 0; i < 3; i++) {
        ext[i] = (char)tolower((unsigned char)ext[i]);
    }
    if (strcmp(ext, "prg") && strcmp(ext, "seq") && strcmp(ext, "usr") && strcmp(ext, "rel")
            && strcmp(ext, "txt") && strcmp(ext, "csv") && strcmp(ext, "bas")) {
        return 1;
    }
    cbmname[l - 4] = 0;
    int rs = 0;
    int la = -1;
    char version[8] = "";
    if (!strcmp(ext, "rel") || !strcmp(ext, "csv") || !strcmp(ext, "bas")) {
        take_tags(cbmname, &rs, &la, version, sizeof version);
    }
    if (!strcmp(ext, "prg") || !strcmp(ext, "usr") || !strcmp(ext, "seq")) {
        *filetype = !strcmp(ext, "prg") ? FILETYPEPRG : !strcmp(ext, "usr") ? FILETYPEUSR : FILETYPESEQ;
        return 0;
    }
    if (rs < 0 || rs > 254 || (!strcmp(ext, "rel") && rs == 0)) {
        fprintf(stderr, "ERROR: %s needs a record length [rsNN] of 1-254\n", path);
        return -1;
    }
    if (!strcmp(ext, "rel")) {
        *filetype = FILETYPEREL;
        *record_length = rs;
        return 0;
    }
    if (!strcmp(ext, "txt")) {
        *filetype = FILETYPESEQ;
        return text_to_seq((const char *)data, (size_t)size, path, out, out_size);
    }
    if (!strcmp(ext, "csv")) {
        *filetype = FILETYPEREL;
        *record_length = rs;
        return csv_to_rel((const char *)data, (size_t)size, path, record_length, out, out_size);
    }
    /* .bas */
    *filetype = FILETYPEPRG;
    return listing_to_basic((const char *)data, (size_t)size, la >= 0 ? (unsigned int)la : 0x0801,
                            version[0] ? version : NULL, path, out, out_size);
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
    write_directory_pet(type, image, folder, src, n);
    for (int i = 0; i < n; i++) {
        /* entries without blocks: 0 byte x00 files; links: a copy of the data */
        const source_file *data = (src[i].kind == ENTRY_LINK) ? &src[src[i].link] : &src[i];
        if (data->broken) {
            char a[3 * FILENAMEMAXSIZE + 1];
            petscii2ascii(src[i].name, a, FILENAMEMAXSIZE, false);
            fprintf(stderr, "WARNING: File \"%s\" has a broken block chain and is not unpacked\n", a);
            continue;
        }
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
        if (de->d_name[0] == '.' || strcasecmp(de->d_name, "directory.pet") == 0) {
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
            /* plain files as -X and -XA write them (name.prg/.seq/.usr,
               name[rsNN].rel, .bas/.txt/.csv); anything else is a PRG with
               the whole file name */
            char cbmname[3 * FILENAMEMAXSIZE + 16];
            int t;
            int rl;
            unsigned char *conv;
            int conv_size;
            int c = convert_local_file(names[i], path, data, (int)st.st_size, cbmname, sizeof cbmname, &t, &rl,
                                       &conv, &conv_size);
            if (c < 0) {
                fprintf(stderr, "WARNING: %s is not copied\n", names[i]);
                free(data);
                free(path);
                free(names[i]);
                continue;
            }
            if (conv != data) {
                free(data);
                data = conv;
            }
            ascii2petscii((unsigned char *)cbmname, fi->pfilename, FILENAMEMAXSIZE);
            fi->filetype = 0x80 | t;
            fi->record_length = rl;
            fi->data = data;
            fi->data_size = conv_size;
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
