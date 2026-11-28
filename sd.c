/*
 * sd.c - images of SD cards and USB sticks (FAT, without partition table)
 *
 *   cit --sd-new   image size [label]   new image, formatted (up to 32 GB)
 *   cit --sd-info  image                file system, size, free space
 *   cit --sd-dir   image [folder]       list a folder
 *   cit --sd-put   image [--to folder] file...   copy files/folders into it
 *   cit --sd-get   image [--to folder] path...   copy files/folders out
 *   cit --sd-del   image path...        delete files and empty folders
 *   cit --sd-mkdir image folder...      create folders
 *   cit --sd-mount image [X:]           mount on Windows (OSFMount), default S:
 *   cit --sd-unmount X:                 dismount it again
 *
 * The file system is handled by FatFs (fatfs/, see fatfs/LICENSE.txt), so
 * nothing has to be mounted and no administrator rights are needed. New
 * images have no partition table: the file system starts at sector 0, as
 * on most USB sticks. Images with a partition table can be read and
 * changed too (the first partition).
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#define _FILE_OFFSET_BITS 64
#ifndef _WIN32
#define _DEFAULT_SOURCE        /* fseeko, lstat */
#endif

#include "cit.h"
#ifdef _WIN32
#undef isnan                   /* ff.h defines them again for Windows */
#undef isinf
#endif
#define DIR FF_DIR              /* FatFs and dirent.h both name a type DIR */
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#undef DIR
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>             /* rmdir */
#endif
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#define SD_MAX_SIZE (32ULL * 1024 * 1024 * 1024)   /* 32 GB */
#define SD_SECTOR   512

static FILE *sd_file;                 /* the image */
static unsigned long long sd_sectors; /* its size in sectors */

/* ------------------------------------------------------------------ */
/* image file access, also beyond 2 GB                                 */

static int
seek_to(FILE *f, unsigned long long pos)
{
#ifdef _WIN32
    return _fseeki64(f, (long long)pos, SEEK_SET);
#else
    return fseeko(f, (off_t)pos, SEEK_SET);
#endif
}

static long long
file_size(FILE *f)
{
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) {
        return -1;
    }
    return _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) {
        return -1;
    }
    return (long long)ftello(f);
#endif
}

/* FatFs disk functions: one drive, the image file */
DSTATUS
disk_initialize(BYTE pdrv)
{
    return (pdrv == 0 && sd_file != NULL) ? 0 : STA_NOINIT;
}

DSTATUS
disk_status(BYTE pdrv)
{
    return (pdrv == 0 && sd_file != NULL) ? 0 : STA_NOINIT;
}

DRESULT
disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || sd_file == NULL || (unsigned long long)sector + count > sd_sectors) {
        return RES_PARERR;
    }
    if (seek_to(sd_file, (unsigned long long)sector * SD_SECTOR) != 0
        || fread(buff, SD_SECTOR, count, sd_file) != count) {
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT
disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || sd_file == NULL || (unsigned long long)sector + count > sd_sectors) {
        return RES_PARERR;
    }
    if (seek_to(sd_file, (unsigned long long)sector * SD_SECTOR) != 0
        || fwrite(buff, SD_SECTOR, count, sd_file) != count) {
        return RES_ERROR;
    }
    return RES_OK;
}

DRESULT
disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || sd_file == NULL) {
        return RES_PARERR;
    }
    switch (cmd) {
    case CTRL_SYNC:
        return fflush(sd_file) == 0 ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = (LBA_t)sd_sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SD_SECTOR;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

/* time stamp of new files: now, local time */
DWORD
get_fattime(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (tm == NULL || tm->tm_year < 80) {
        return ((DWORD)(2025 - 1980) << 25) | (1 << 21) | (1 << 16);
    }
    return ((DWORD)(tm->tm_year - 80) << 25) | ((DWORD)(tm->tm_mon + 1) << 21) | ((DWORD)tm->tm_mday << 16)
           | ((DWORD)tm->tm_hour << 11) | ((DWORD)tm->tm_min << 5) | ((DWORD)tm->tm_sec >> 1);
}

static const char *
fresult_text(FRESULT r)
{
    switch (r) {
    case FR_OK: return "ok";
    case FR_DISK_ERR: return "read or write error";
    case FR_NOT_READY: return "image not open";
    case FR_NO_FILE: return "file not found";
    case FR_NO_PATH: return "folder not found";
    case FR_INVALID_NAME: return "invalid name";
    case FR_DENIED: return "access denied or image full";
    case FR_EXIST: return "already exists";
    case FR_NO_FILESYSTEM: return "no FAT file system";
    case FR_MKFS_ABORTED: return "size not possible for FAT";
    default: return "FAT error";
    }
}

/* ------------------------------------------------------------------ */
/* names: the image holds UTF-8, local names are in the encoding of the */
/* system (ANSI code page on Windows)                                  */

#ifdef _WIN32
static void
recode(const char *in, UINT from, char *out, int size, UINT to)
{
    wchar_t w[1024];
    if (MultiByteToWideChar(from, 0, in, -1, w, 1024) == 0
        || WideCharToMultiByte(to, 0, w, -1, out, size, NULL, NULL) == 0) {
        snprintf(out, size, "%s", in);
    }
}
#endif

static void
to_fat_name(const char *local, char *fat, int size)
{
#ifdef _WIN32
    recode(local, CP_ACP, fat, size, CP_UTF8);
#else
    snprintf(fat, size, "%s", local);
#endif
}

static void
to_local_name(const char *fat, char *local, int size)
{
#ifdef _WIN32
    recode(fat, CP_UTF8, local, size, CP_ACP);
#else
    snprintf(local, size, "%s", fat);
#endif
}

static int
make_local_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
#ifdef _WIN32
    return mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

/* last part of a path, after / or \ */
static const char *
base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++) {
        if ((*p == '/' || *p == '\\') && p[1] != 0) {
            b = p + 1;
        }
    }
    return b;
}

/* path inside the image: folder + "/" + name */
static void
join(char *out, int size, const char *folder, const char *name)
{
    size_t l = strlen(folder);
    if (l == 0 || folder[l - 1] == '/') {
        snprintf(out, size, "%s%s", folder, name);
    } else {
        snprintf(out, size, "%s/%s", folder, name);
    }
}

/* ------------------------------------------------------------------ */

static FATFS sd_fs;

static int
sd_open(const char *path, bool write)
{
    sd_file = fopen(path, write ? "r+b" : "rb");
    if (sd_file == NULL) {
        fprintf(stderr, "ERROR: Could not open %s\n", path);
        return -1;
    }
    long long size = file_size(sd_file);
    if (size <= 0 || size % SD_SECTOR != 0) {
        fprintf(stderr, "ERROR: %s is no image of a card (size %lld is no multiple of 512)\n", path, size);
        fclose(sd_file);
        sd_file = NULL;
        return -1;
    }
    if ((unsigned long long)size > SD_MAX_SIZE) {
        fprintf(stderr, "ERROR: %s is larger than 32 GB\n", path);
        fclose(sd_file);
        sd_file = NULL;
        return -1;
    }
    sd_sectors = (unsigned long long)size / SD_SECTOR;
    FRESULT r = f_mount(&sd_fs, "", 1);
    if (r != FR_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", path, fresult_text(r));
        fclose(sd_file);
        sd_file = NULL;
        return -1;
    }
    return 0;
}

static int
sd_close(void)
{
    f_unmount("");
    int r = 0;
    if (sd_file != NULL) {
        r = fclose(sd_file);
        sd_file = NULL;
    }
    if (r != 0) {
        fprintf(stderr, "ERROR: Could not write the image\n");
        return -1;
    }
    return 0;
}

/* "64M", "2G", "512K", or bytes */
static unsigned long long
parse_size(const char *s)
{
    char *end;
    unsigned long long v = strtoull(s, &end, 10);
    switch (toupper((unsigned char)*end)) {
    case 'K': v *= 1024ULL; end++; break;
    case 'M': v *= 1024ULL * 1024; end++; break;
    case 'G': v *= 1024ULL * 1024 * 1024; end++; break;
    default: break;
    }
    if (toupper((unsigned char)*end) == 'B') {
        end++;
    }
    return (*end == 0 && end != s) ? v : 0;
}

static void
print_size(unsigned long long bytes)
{
    if (bytes >= 10ULL * 1024 * 1024 * 1024) {
        printf("%llu GB", bytes / (1024ULL * 1024 * 1024));
    } else if (bytes >= 10ULL * 1024 * 1024) {
        printf("%llu MB", bytes / (1024ULL * 1024));
    } else if (bytes >= 10ULL * 1024) {
        printf("%llu KB", bytes / 1024ULL);
    } else {
        printf("%llu bytes", bytes);
    }
}

/* FatFs puts the label only into the root folder; Windows and fsck also
   expect it in the boot sector (and its FAT32 backup) */
static FRESULT
set_boot_label(const char *label)
{
    char l[64];
    DWORD serial;
    if (f_getlabel("", l, &serial) != FR_OK) {   /* as FatFs stored it: upper case */
        snprintf(l, sizeof l, "%s", label);
    }
    BYTE b[SD_SECTOR];
    if (disk_read(0, b, 0, 1) != RES_OK) {
        return FR_DISK_ERR;
    }
    int at = (sd_fs.fs_type == FS_FAT32) ? 0x47 : 0x2b;
    memset(b + at, ' ', 11);
    for (int i = 0; i < 11 && l[i]; i++) {
        b[at + i] = (BYTE)l[i];
    }
    if (disk_write(0, b, 0, 1) != RES_OK) {
        return FR_DISK_ERR;
    }
    if (sd_fs.fs_type == FS_FAT32) {
        int backup = b[0x32] | (b[0x33] << 8);
        if (backup > 0 && disk_write(0, b, (LBA_t)backup, 1) != RES_OK) {
            return FR_DISK_ERR;
        }
    }
    return FR_OK;
}

/* --sd-new image size [label] */
static int
sd_new(const char *path, const char *size_text, const char *label)
{
    unsigned long long size = parse_size(size_text);
    if (size == 0 || size % SD_SECTOR != 0) {
        fprintf(stderr, "ERROR: Size %s not understood (e.g. 64M, 2G, 32G)\n", size_text);
        return -1;
    }
    if (size > SD_MAX_SIZE) {
        fprintf(stderr, "ERROR: Images are limited to 32 GB\n");
        return -1;
    }
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        fprintf(stderr, "ERROR: %s already exists\n", path);
        return -1;
    }
    sd_file = fopen(path, "w+b");
    if (sd_file == NULL) {
        fprintf(stderr, "ERROR: Could not create %s\n", path);
        return -1;
    }
    unsigned char zero = 0;
    if (seek_to(sd_file, size - 1) != 0 || fwrite(&zero, 1, 1, sd_file) != 1) {
        fprintf(stderr, "ERROR: Could not create %s with %s\n", path, size_text);
        fclose(sd_file);
        sd_file = NULL;
        remove(path);
        return -1;
    }
    sd_sectors = size / SD_SECTOR;
    /* FAT32 from 64 MB on, smaller ones as FatFs chooses (FAT12/16); no
       partition table; cluster size as FatFs chooses it for SD cards */
    MKFS_PARM opt = { (BYTE)((size >= 64ULL * 1024 * 1024 ? FM_FAT32 : FM_ANY) | FM_SFD), 2, 0, 0, 0 };
    static BYTE work[64 * 1024];
    FRESULT r = f_mkfs("", &opt, work, sizeof work);
    if (r == FR_OK) {
        r = f_mount(&sd_fs, "", 1);
    }
    if (r == FR_OK && label != NULL) {
        char l[64];
        to_fat_name(label, l, sizeof l);
        r = f_setlabel(l);
        if (r == FR_OK) {
            r = set_boot_label(l);
        }
    }
    if (r != FR_OK) {
        fprintf(stderr, "ERROR: Could not format %s: %s\n", path, fresult_text(r));
        sd_close();
        remove(path);
        return -1;
    }
    return sd_close();
}

static const char *
fs_name(BYTE t)
{
    return t == FS_FAT12 ? "FAT12" : t == FS_FAT16 ? "FAT16" : t == FS_FAT32 ? "FAT32" : "exFAT";
}

/* --sd-info image */
static int
sd_info(const char *path)
{
    if (sd_open(path, false) != 0) {
        return -1;
    }
    char label[64] = "";
    DWORD serial = 0;
    f_getlabel("", label, &serial);
    DWORD free_clusters = 0;
    FATFS *fs;
    f_getfree("", &free_clusters, &fs);
    unsigned long long cluster = (unsigned long long)fs->csize * SD_SECTOR;
    char local[256];
    to_local_name(label, local, sizeof local);
    printf("%s\n", path);
    printf("  label      %s\n", local[0] ? local : "(none)");
    printf("  serial     %04X-%04X\n", (unsigned)(serial >> 16), (unsigned)(serial & 0xffff));
    printf("  layout     %s\n", fs->volbase == 0 ? "no partition table (file system at sector 0)"
                                                 : "partition table, first partition");
    printf("  file sys.  %s, cluster %llu bytes, %lu clusters\n", fs_name(fs->fs_type), cluster,
           (unsigned long)(fs->n_fatent - 2));
    printf("  size       ");
    print_size(sd_sectors * SD_SECTOR);
    printf(", free ");
    print_size((unsigned long long)free_clusters * cluster);
    printf("\n");
    return sd_close();
}

/* --sd-dir image [folder] */
static int
sd_dir(const char *path, const char *folder)
{
    if (sd_open(path, false) != 0) {
        return -1;
    }
    char f[1024];
    to_fat_name(folder != NULL ? folder : "/", f, sizeof f);
    FF_DIR d;
    FILINFO fi;
    FRESULT r = f_opendir(&d, f);
    if (r != FR_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", folder ? folder : "/", fresult_text(r));
        sd_close();
        return -1;
    }
    int files = 0;
    int dirs = 0;
    unsigned long long bytes = 0;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0] != 0) {
        char local[1024];
        to_local_name(fi.fname, local, sizeof local);
        printf("  %04u-%02u-%02u %02u:%02u  ", (fi.fdate >> 9) + 1980, (fi.fdate >> 5) & 15, fi.fdate & 31,
               fi.ftime >> 11, (fi.ftime >> 5) & 63);
        if (fi.fattrib & AM_DIR) {
            printf("%12s  %s/\n", "<DIR>", local);
            dirs++;
        } else {
            printf("%12llu  %s\n", (unsigned long long)fi.fsize, local);
            files++;
            bytes += fi.fsize;
        }
    }
    f_closedir(&d);
    DWORD free_clusters = 0;
    FATFS *fs;
    f_getfree("", &free_clusters, &fs);
    printf("  %d files, %d folders, %llu bytes; ", files, dirs, bytes);
    print_size((unsigned long long)free_clusters * fs->csize * SD_SECTOR);
    printf(" free\n");
    return sd_close();
}

/* copy one local file into the image */
static int
put_file(const char *local, const char *target)
{
    FILE *in = fopen(local, "rb");
    if (in == NULL) {
        fprintf(stderr, "ERROR: Could not open %s\n", local);
        return -1;
    }
    FIL out;
    FRESULT r = f_open(&out, target, FA_WRITE | FA_CREATE_ALWAYS);
    if (r != FR_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", target, fresult_text(r));
        fclose(in);
        return -1;
    }
    static unsigned char buf[64 * 1024];
    size_t n;
    int ret = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        UINT w;
        r = f_write(&out, buf, (UINT)n, &w);
        if (r != FR_OK || w != n) {
            fprintf(stderr, "ERROR: %s: %s\n", target, r != FR_OK ? fresult_text(r) : "image full");
            ret = -1;
            break;
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "ERROR: Could not read %s\n", local);
        ret = -1;
    }
    fclose(in);
    if (f_close(&out) != FR_OK) {
        ret = -1;
    }
    if (ret != 0) {
        f_unlink(target);
    }
    return ret;
}

/* copy a local file or folder (with everything in it) into a folder of the image */
static int
put_path(const char *local, const char *folder, int *count)
{
    struct stat st;
#ifdef _WIN32
    if (stat(local, &st) != 0) {
#else
    if (lstat(local, &st) != 0) {
#endif
        fprintf(stderr, "ERROR: %s not found\n", local);
        return -1;
    }
#ifndef _WIN32
    if (S_ISLNK(st.st_mode)) {
        fprintf(stderr, "WARNING: %s is a link, skipped\n", local);
        return 0;
    }
#endif
    char name[1024];
    char target[2048];
    to_fat_name(base_name(local), name, sizeof name);
    size_t l = strlen(name);
    while (l > 0 && (name[l - 1] == '/' || name[l - 1] == '\\')) {
        name[--l] = 0;
    }
    join(target, sizeof target, folder, name);
    if (!S_ISDIR(st.st_mode)) {
        char shown[2048];
        to_local_name(target, shown, sizeof shown);
        printf("Copying %s -> %s\n", local, shown);
        fflush(stdout);
        if (put_file(local, target) != 0) {
            return -1;
        }
        (*count)++;
        return 0;
    }
    FRESULT r = f_mkdir(target);
    if (r != FR_OK && r != FR_EXIST) {
        fprintf(stderr, "ERROR: %s: %s\n", target, fresult_text(r));
        return -1;
    }
    DIR *d = opendir(local);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Could not read folder %s\n", local);
        return -1;
    }
    struct dirent *de;
    int ret = 0;
    while (ret == 0 && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char sub[2048];
        snprintf(sub, sizeof sub, "%s/%s", local, de->d_name);
        ret = put_path(sub, target, count);
    }
    closedir(d);
    return ret;
}

/* a name from the image that is safe as a local file name: FAT forbids
   these characters, but a damaged or made-up image may have them */
static bool
safe_name(const char *n)
{
    if (n[0] == 0 || strcmp(n, ".") == 0 || strcmp(n, "..") == 0) {
        return false;
    }
    for (const char *c = n; *c; c++) {
        if (*c == '/' || *c == '\\' || *c == ':' || (unsigned char)*c < 0x20) {
            return false;
        }
    }
    return true;
}

/* copy a file or folder of the image into a local folder */
static int
get_path(const char *src, const char *folder, int *count)
{
    FILINFO fi;
    bool is_dir;
    if (strcmp(src, "/") == 0 || src[0] == 0) {
        is_dir = true;
        fi.fname[0] = 0;
    } else {
        FRESULT r = f_stat(src, &fi);
        if (r != FR_OK) {
            fprintf(stderr, "ERROR: %s: %s\n", src, fresult_text(r));
            return -1;
        }
        is_dir = (fi.fattrib & AM_DIR) != 0;
    }
    if (fi.fname[0] && !safe_name(fi.fname)) {
        fprintf(stderr, "WARNING: %s: the name in the image is no valid file name, skipped\n", src);
        return 0;
    }
    char local_name[1024];
    to_local_name(fi.fname, local_name, sizeof local_name);
    char target[2048];
    if (local_name[0]) {
        snprintf(target, sizeof target, "%s/%s", folder, local_name);
    } else {
        snprintf(target, sizeof target, "%s", folder);
    }
    if (!is_dir) {
        char shown[2048];
        to_local_name(src, shown, sizeof shown);
        printf("Copying %s -> %s\n", shown, target);
        fflush(stdout);
        FIL in;
        FRESULT r = f_open(&in, src, FA_READ);
        if (r != FR_OK) {
            fprintf(stderr, "ERROR: %s: %s\n", src, fresult_text(r));
            return -1;
        }
        FILE *out = fopen(target, "wb");
        if (out == NULL) {
            fprintf(stderr, "ERROR: Could not create %s\n", target);
            f_close(&in);
            return -1;
        }
        static unsigned char buf[64 * 1024];
        UINT n;
        int ret = 0;
        while ((r = f_read(&in, buf, sizeof buf, &n)) == FR_OK && n > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                ret = -1;
                break;
            }
        }
        if (r != FR_OK) {
            fprintf(stderr, "ERROR: %s: %s\n", src, fresult_text(r));
            ret = -1;
        }
        f_close(&in);
        if (fclose(out) != 0 || ret != 0) {
            fprintf(stderr, "ERROR: Could not write %s\n", target);
            return -1;
        }
        (*count)++;
        return 0;
    }
    if (make_local_dir(target) != 0) {
        fprintf(stderr, "ERROR: Could not create folder %s\n", target);
        return -1;
    }
    FF_DIR d;
    FRESULT r = f_opendir(&d, src[0] ? src : "/");
    if (r != FR_OK) {
        fprintf(stderr, "ERROR: %s: %s\n", src, fresult_text(r));
        return -1;
    }
    int ret = 0;
    while (ret == 0 && f_readdir(&d, &fi) == FR_OK && fi.fname[0] != 0) {
        char sub[2048];
        join(sub, sizeof sub, src, fi.fname);
        ret = get_path(sub, target, count);
    }
    f_closedir(&d);
    return ret;
}

/* --sd-mount / --sd-unmount: OSFMount.com does it (Windows only) */
#ifdef _WIN32
static int
run_osfmount(const char *const args[])
{
    int r = run_program(osfmount_path(), args, false);
    if (r < 0) {
        fprintf(stderr, "ERROR: Could not start %s (set osfmount in cit.ini)\n", osfmount_path());
    }
    return r;
}

/* "J:", "j", "J:\" -> 'J', 0 if it is no drive letter */
static char
drive_letter(const char *s)
{
    char c = (char)toupper((unsigned char)s[0]);
    if (c < 'A' || c > 'Z' || (s[1] != 0 && s[1] != ':')) {
        return 0;
    }
    return c;
}

static int
sd_mount(const char *image, const char *letter_text)
{
    /* a partition table needs the first partition, else the whole image
       as a removable drive (FAT without partition table) */
    if (sd_open(image, false) != 0) {
        return -1;
    }
    bool partitioned = sd_fs.volbase != 0;
    sd_close();
    char full[MAX_PATH];
    if (_fullpath(full, image, sizeof full) == NULL) {
        fprintf(stderr, "ERROR: %s not found\n", image);
        return -1;
    }
    DWORD used = GetLogicalDrives();
    char letter = 0;
    if (letter_text != NULL) {
        letter = drive_letter(letter_text);
        if (letter == 0) {
            fprintf(stderr, "ERROR: %s is no drive letter\n", letter_text);
            return -1;
        }
        if (used & (1UL << (letter - 'A'))) {
            fprintf(stderr, "ERROR: %c: is in use\n", letter);
            return -1;
        }
    } else {
        /* sd.letter from cit.ini, else S:; if that is taken the first free one */
        char wanted = config.sd_letter ? config.sd_letter : 'S';
        if (!(used & (1UL << (wanted - 'A')))) {
            letter = wanted;
        } else {
            for (char c = 'D'; c <= 'Z'; c++) {
                if (!(used & (1UL << (c - 'A')))) {
                    letter = c;
                    break;
                }
            }
            if (letter == 0) {
                fprintf(stderr, "ERROR: No free drive letter\n");
                return -1;
            }
            printf("%c: is in use, taking %c:\n", wanted, letter);
        }
    }
    char drive[3] = { letter, ':', 0 };
    const char *plain[] = { "-a", "-t", "file", "-f", full, "-m", drive, "-o", "rw,rem,logical", NULL };
    const char *part[] = { "-a", "-t", "file", "-f", full, "-m", drive, "-o", "rw,logical", "-v", "1", NULL };
    if (run_osfmount(partitioned ? part : plain) != 0 || !(GetLogicalDrives() & (1UL << (letter - 'A')))) {
        fprintf(stderr, "ERROR: OSFMount could not mount %s (it needs administrator rights)\n", image);
        return -1;
    }
    printf("%s mounted as %c: (read/write); cit --sd-unmount %c: when done\n", image, letter, letter);
    return 0;
}

static int
sd_unmount(const char *letter_text)
{
    char letter = drive_letter(letter_text);
    if (letter == 0) {
        fprintf(stderr, "ERROR: %s is no drive letter\n", letter_text);
        return -1;
    }
    char drive[3] = { letter, ':', 0 };
    const char *args[] = { "-d", "-m", drive, NULL };
    if (run_osfmount(args) != 0) {
        fprintf(stderr, "ERROR: OSFMount could not dismount %c: (still in use?)\n", letter);
        return -1;
    }
    printf("%c: dismounted\n", letter);
    return 0;
}
#endif

/* --sd-get --clean: counts or deletes everything in a local folder,
   without following links. Returns -1 on an error */
static int
clean_walk(const char *folder, bool delete_it, int *files, int *dirs)
{
    DIR *d = opendir(folder);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Could not read folder %s\n", folder);
        return -1;
    }
    struct dirent *de;
    int ret = 0;
    while (ret == 0 && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char p[4096];
        snprintf(p, sizeof p, "%s/%s", folder, de->d_name);
        struct stat st;
#ifdef _WIN32
        if (stat(p, &st) != 0) {
#else
        if (lstat(p, &st) != 0) {
#endif
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            ret = clean_walk(p, delete_it, files, dirs);
            (*dirs)++;
            if (ret == 0 && delete_it && rmdir(p) != 0) {
                fprintf(stderr, "ERROR: Could not delete folder %s\n", p);
                ret = -1;
            }
        } else {
            (*files)++;
            if (delete_it && remove(p) != 0) {
                fprintf(stderr, "ERROR: Could not delete %s\n", p);
                ret = -1;
            }
        }
    }
    closedir(d);
    return ret;
}

/* the same folder? (full paths, without trailing separators) */
static bool
same_folder(const char *a, const char *b)
{
    char fa[4096], fb[4096];
#ifdef _WIN32
    if (_fullpath(fa, a, sizeof fa) == NULL || _fullpath(fb, b, sizeof fb) == NULL) {
        return false;
    }
#else
    if (realpath(a, fa) == NULL || realpath(b, fb) == NULL) {
        return false;
    }
#endif
    for (char *p = fa; *p; p++) {
        *p = (*p == '\\') ? '/' : *p;
    }
    for (char *p = fb; *p; p++) {
        *p = (*p == '\\') ? '/' : *p;
    }
    size_t la = strlen(fa), lb = strlen(fb);
    while (la > 1 && fa[la - 1] == '/') {
        fa[--la] = 0;
    }
    while (lb > 1 && fb[lb - 1] == '/') {
        fb[--lb] = 0;
    }
#ifdef _WIN32
    return strcasecmp(fa, fb) == 0;
#else
    return strcmp(fa, fb) == 0;
#endif
}

/* empties the target folder of --sd-get --clean. Only on a removable
   drive, other folders with --force; never the root of the system drive
   or the home folder. Asks unless yes. 0 = done or nothing to do */
static int
clean_target(const char *folder, bool yes, bool force)
{
    struct stat st;
    if (stat(folder, &st) != 0) {
        return 0;                               /* nothing there yet */
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ERROR: %s is no folder\n", folder);
        return -1;
    }
#ifdef _WIN32
    char sysroot[4] = "C:\\";
    char sysdir[MAX_PATH];
    if (GetSystemDirectoryA(sysdir, sizeof sysdir) > 0) {
        sysroot[0] = sysdir[0];
    }
    const char *home = getenv("USERPROFILE");
#else
    const char *sysroot = "/";
    const char *home = getenv("HOME");
#endif
    if (same_folder(folder, sysroot) || (home != NULL && home[0] && same_folder(folder, home))) {
        fprintf(stderr, "ERROR: --clean is never allowed on %s\n", folder);
        return -1;
    }
    if (!force && !path_on_removable(folder)) {
        fprintf(stderr, "ERROR: %s is not on a removable drive; --clean only empties cards and sticks "
                        "(--force for other folders)\n", folder);
        return -1;
    }
    int files = 0, dirs = 0;
    if (clean_walk(folder, false, &files, &dirs) != 0) {
        return -1;
    }
    if (files == 0 && dirs == 0) {
        return 0;
    }
    printf("%s: %d files and %d folders will be deleted before copying.\n", folder, files, dirs);
    if (!yes) {
        printf("Continue? (yes/no) ");
        fflush(stdout);
        char answer[16] = "";
        if (fgets(answer, sizeof answer, stdin) == NULL || (strncasecmp(answer, "yes", 3) != 0
                && !(tolower((unsigned char)answer[0]) == 'y' && (answer[1] == '\n' || answer[1] == 0)))) {
            printf("Nothing deleted, nothing copied.\n");
            return -1;
        }
    }
    files = dirs = 0;
    if (clean_walk(folder, true, &files, &dirs) != 0) {
        return -1;
    }
    printf("%d files and %d folders deleted\n", files, dirs);
    return 0;
}

static void
usage_sd(void)
{
    fprintf(stderr, "Usage: cit --sd-new image size [label]   (size e.g. 64M, 2G; up to 32G)\n"
                    "       cit --sd-info image\n"
                    "       cit --sd-dir image [folder]\n"
                    "       cit --sd-put image [--to folder] file|folder...\n"
                    "       cit --sd-get image [--to folder] [--clean [--yes] [--force]] path...\n"
                    "       cit --sd-del image path...\n"
                    "       cit --sd-mkdir image folder...\n"
                    "       cit --sd-mount image [X:]       (Windows, OSFMount)\n"
                    "       cit --sd-unmount X:\n"
                    "       cit --sd-list\n"
                    "       cit --sd-read  drive image\n"
                    "       cit --sd-write image drive [--yes]\n");
}

/* argv[1] is one of the --sd- commands */
int
sd_main(int argc, char **argv)
{
    const char *cmd = argv[1];
    if (strcmp(cmd, "--sd-list") == 0 || strcmp(cmd, "--sd-read") == 0 || strcmp(cmd, "--sd-write") == 0) {
        return sdcard_main(argc, argv);
    }
    if (argc < 3) {
        usage_sd();
        return -1;
    }
    const char *image = argv[2];
    if (strcmp(cmd, "--sd-mount") == 0 || strcmp(cmd, "--sd-unmount") == 0) {
#ifdef _WIN32
        if (cmd[5] == 'm') {
            return argc <= 4 ? sd_mount(image, argc == 4 ? argv[3] : NULL) : (usage_sd(), -1);
        }
        return argc == 3 ? sd_unmount(image) : (usage_sd(), -1);
#else
        fprintf(stderr, "ERROR: %s is for Windows (OSFMount); on Linux: "
                        "sudo mount -o loop image /mnt, or udisksctl loop-setup -f image\n", cmd);
        return -1;
#endif
    }
    if (strcmp(cmd, "--sd-new") == 0) {
        if (argc < 4 || argc > 5) {
            usage_sd();
            return -1;
        }
        return sd_new(image, argv[3], argc == 5 ? argv[4] : NULL);
    }
    if (strcmp(cmd, "--sd-info") == 0) {
        return sd_info(image);
    }
    if (strcmp(cmd, "--sd-dir") == 0) {
        return sd_dir(image, argc > 3 ? argv[3] : NULL);
    }
    /* the rest: --to folder, --clean, --yes, --force, then the paths */
    const char *to = NULL;
    bool clean = false, yes = false, force = false;
    const char *paths[256];
    int npaths = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            to = argv[++i];
        } else if (strcmp(argv[i], "--clean") == 0) {
            clean = true;
        } else if (strcmp(argv[i], "--yes") == 0) {
            yes = true;
        } else if (strcmp(argv[i], "--force") == 0) {
            force = true;
        } else if (npaths < 256) {
            paths[npaths++] = argv[i];
        }
    }
    if (npaths == 0 || ((clean || yes || force) && strcmp(cmd, "--sd-get") != 0)) {
        usage_sd();
        return -1;
    }
    int ret = 0;
    int count = 0;
    if (strcmp(cmd, "--sd-put") == 0) {
        if (sd_open(image, true) != 0) {
            return -1;
        }
        char folder[1024];
        to_fat_name(to != NULL ? to : "/", folder, sizeof folder);
        for (int i = 0; i < npaths && ret == 0; i++) {
            ret = put_path(paths[i], folder, &count);
        }
        printf("%d file%s copied into %s\n", count, count == 1 ? "" : "s", image);
    } else if (strcmp(cmd, "--sd-get") == 0) {
        if (sd_open(image, false) != 0) {
            return -1;
        }
        if (clean && clean_target(to != NULL ? to : ".", yes, force) != 0) {
            sd_close();
            return -1;
        }
        for (int i = 0; i < npaths && ret == 0; i++) {
            char p[1024];
            to_fat_name(paths[i], p, sizeof p);
            ret = get_path(p, to != NULL ? to : ".", &count);
        }
        printf("%d file%s copied from %s\n", count, count == 1 ? "" : "s", image);
    } else if (strcmp(cmd, "--sd-del") == 0 || strcmp(cmd, "--sd-mkdir") == 0) {
        bool del = strcmp(cmd, "--sd-del") == 0;
        if (to != NULL) {
            usage_sd();
            return -1;
        }
        if (sd_open(image, true) != 0) {
            return -1;
        }
        for (int i = 0; i < npaths; i++) {
            char p[1024];
            to_fat_name(paths[i], p, sizeof p);
            FRESULT r = del ? f_unlink(p) : f_mkdir(p);
            if (r != FR_OK) {
                fprintf(stderr, "ERROR: %s: %s%s\n", paths[i], fresult_text(r),
                        (del && r == FR_DENIED) ? " (a folder must be empty)" : "");
                ret = -1;
            }
        }
    } else {
        usage_sd();
        return -1;
    }
    if (sd_close() != 0) {
        ret = -1;
    }
    return ret;
}
