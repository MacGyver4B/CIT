/*
 * sdcard.c - reading and writing SD cards and USB sticks
 *
 *   cit --sd-list                    removable drives: number, size, name, letters
 *   cit --sd-read  E: card.img       the whole card into an image file
 *   cit --sd-write card.img E: [--yes]   an image onto the card, then checked
 *
 * Only removable drives (USB, SD, MMC) up to 32 GB are offered; the drive
 * Windows runs from never. Before writing cit asks, unless --yes is given
 * (for a frontend that asks itself). After writing the card is read back
 * and compared. Progress goes to stdout, one line per percent.
 *
 * Windows: \\.\PhysicalDriveN, the volumes of the card are locked and
 * dismounted while cit works on it (administrator rights). Linux: the
 * device in /dev (root); mounted cards are refused for writing.
 *
 * For the tests CIT_TEST_DEVICE=file offers that file as removable drive
 * "test", nothing else is touched then.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#ifndef _WIN32
#define _DEFAULT_SOURCE          /* pread, pwrite, fsync */
#define _FILE_OFFSET_BITS 64
#endif

#include "cit.h"
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CARD_MAX   (32ULL * 1024 * 1024 * 1024)
#define CHUNK      (1024 * 1024)
#define MAX_DRIVES 32

typedef struct {
    char path[64];               /* \\.\PhysicalDrive1, /dev/sdb, the test file */
    char name[96];               /* vendor and model */
    unsigned long long size;
    char letters[27];            /* Windows drive letters on it */
    bool test;                   /* CIT_TEST_DEVICE */
} card_t;

typedef struct {
#ifdef _WIN32
    HANDLE h;
    HANDLE vol[26];
    int nvol;
#endif
    int fd;
    FILE *test;
} card_io;

static void
trim(char *s)
{
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\n')) {
        s[--l] = 0;
    }
    size_t b = 0;
    while (s[b] == ' ') {
        b++;
    }
    memmove(s, s + b, strlen(s + b) + 1);
}

/* ------------------------------------------------------------------ */
/* finding the drives                                                  */

static int
test_device(card_t *c)
{
    const char *t = getenv("CIT_TEST_DEVICE");
    if (t == NULL || t[0] == 0) {
        return 0;
    }
    FILE *f = fopen(t, "rb");
    if (f == NULL) {
        return 0;
    }
    long long size = 0;
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    size = _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    size = (long long)ftello(f);
#endif
    fclose(f);
    memset(c, 0, sizeof *c);
    snprintf(c->path, sizeof c->path, "%s", t);
    snprintf(c->name, sizeof c->name, "test device");
    c->size = (unsigned long long)size;
    c->test = true;
    return 1;
}

#ifdef _WIN32
/* number of the physical drive of a volume like "C:", -1 if none */
static int
volume_disk(char letter)
{
    char p[8];
    snprintf(p, sizeof p, "\\\\.\\%c:", letter);
    HANDLE h = CreateFileA(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    STORAGE_DEVICE_NUMBER n;
    DWORD got;
    int r = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &n, sizeof n, &got, NULL)
            ? (int)n.DeviceNumber : -1;
    CloseHandle(h);
    return r;
}
#endif

/* the removable drives up to 32 GB; returns their number */
static int
list_cards(card_t *cards, int max, bool *larger_found)
{
    *larger_found = false;
    int n = test_device(cards);
    if (n > 0) {
        return n;
    }
#ifdef _WIN32
    char sysdir[MAX_PATH];
    int system_disk = -1;
    if (GetSystemDirectoryA(sysdir, sizeof sysdir) > 0) {
        system_disk = volume_disk(sysdir[0]);
    }
    for (int d = 0; d < MAX_DRIVES && n < max; d++) {
        char p[32];
        snprintf(p, sizeof p, "\\\\.\\PhysicalDrive%d", d);
        HANDLE h = CreateFileA(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }
        STORAGE_PROPERTY_QUERY q;
        memset(&q, 0, sizeof q);
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        BYTE buf[1024];
        DWORD got;
        bool removable = false;
        char name[96] = "";
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf, sizeof buf, &got, NULL)) {
            STORAGE_DEVICE_DESCRIPTOR *sd = (STORAGE_DEVICE_DESCRIPTOR *)buf;
            removable = sd->RemovableMedia || sd->BusType == BusTypeUsb || sd->BusType == BusTypeSd
                        || sd->BusType == BusTypeMmc;
            const char *v = sd->VendorIdOffset ? (const char *)buf + sd->VendorIdOffset : "";
            const char *m = sd->ProductIdOffset ? (const char *)buf + sd->ProductIdOffset : "";
            snprintf(name, sizeof name, "%s %s", v, m);
            trim(name);
        }
        DISK_GEOMETRY_EX g;
        bool medium = DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &g, sizeof g, &got, NULL);
        CloseHandle(h);
        if (!removable || d == system_disk || !medium || g.DiskSize.QuadPart <= 0) {
            continue;
        }
        if ((unsigned long long)g.DiskSize.QuadPart > CARD_MAX) {
            *larger_found = true;
            continue;
        }
        card_t *c = &cards[n++];
        memset(c, 0, sizeof *c);
        snprintf(c->path, sizeof c->path, "%s", p);
        snprintf(c->name, sizeof c->name, "%s", name[0] ? name : "(no name)");
        c->size = (unsigned long long)g.DiskSize.QuadPart;
        int k = 0;
        DWORD drives = GetLogicalDrives();
        for (char l = 'A'; l <= 'Z'; l++) {
            if ((drives & (1UL << (l - 'A'))) && volume_disk(l) == d) {
                c->letters[k++] = l;
            }
        }
    }
#else
    /* the device that holds / is never offered */
    char root_dev[256] = "";
    FILE *mf = fopen("/proc/mounts", "r");
    if (mf != NULL) {
        char dev[256], dir[256];
        while (fscanf(mf, "%255s %255s %*[^\n]", dev, dir) == 2) {
            if (strcmp(dir, "/") == 0 && strncmp(dev, "/dev/", 5) == 0) {
                snprintf(root_dev, sizeof root_dev, "%s", dev + 5);
            }
        }
        fclose(mf);
    }
    DIR *d = opendir("/sys/block");
    if (d == NULL) {
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && n < max) {
        const char *b = de->d_name;
        if (b[0] == '.' || strncmp(b, "loop", 4) == 0 || strncmp(b, "ram", 3) == 0) {
            continue;
        }
        if (root_dev[0] && strncmp(root_dev, b, strlen(b)) == 0) {
            continue;
        }
        char p[512], v[128] = "", m[128] = "";
        unsigned long long sectors = 0;
        int removable = 0;
        snprintf(p, sizeof p, "/sys/block/%s/removable", b);
        FILE *f = fopen(p, "r");
        if (f != NULL) {
            if (fscanf(f, "%d", &removable) != 1) {
                removable = 0;
            }
            fclose(f);
        }
        /* USB and SD/MMC count as well, some readers do not set removable */
        char link[512], target[512];
        snprintf(link, sizeof link, "/sys/block/%s", b);
        ssize_t tl = readlink(link, target, sizeof target - 1);
        target[tl > 0 ? tl : 0] = 0;
        if (!removable && strstr(target, "/usb") == NULL && strncmp(b, "mmcblk", 6) != 0) {
            continue;
        }
        snprintf(p, sizeof p, "/sys/block/%s/size", b);
        f = fopen(p, "r");
        if (f != NULL) {
            if (fscanf(f, "%llu", &sectors) != 1) {
                sectors = 0;
            }
            fclose(f);
        }
        if (sectors == 0) {
            continue;                           /* reader without a card */
        }
        if (sectors * 512ULL > CARD_MAX) {
            *larger_found = true;
            continue;
        }
        snprintf(p, sizeof p, "/sys/block/%s/device/vendor", b);
        f = fopen(p, "r");
        if (f != NULL) {
            if (fgets(v, sizeof v, f) == NULL) {
                v[0] = 0;
            }
            fclose(f);
        }
        snprintf(p, sizeof p, "/sys/block/%s/device/model", b);
        f = fopen(p, "r");
        if (f == NULL) {
            snprintf(p, sizeof p, "/sys/block/%s/device/name", b);
            f = fopen(p, "r");
        }
        if (f != NULL) {
            if (fgets(m, sizeof m, f) == NULL) {
                m[0] = 0;
            }
            fclose(f);
        }
        trim(v);
        trim(m);
        card_t *c = &cards[n++];
        memset(c, 0, sizeof *c);
        snprintf(c->path, sizeof c->path, "/dev/%s", b);
        snprintf(c->name, sizeof c->name, "%s%s%s", v, v[0] && m[0] ? " " : "", m);
        if (c->name[0] == 0) {
            snprintf(c->name, sizeof c->name, "(no name)");
        }
        c->size = sectors * 512ULL;
    }
    closedir(d);
#endif
    return n;
}

static void
print_size(unsigned long long b)
{
    if (b >= 1024ULL * 1024 * 1024) {
        printf("%.1f GB", b / (1024.0 * 1024 * 1024));
    } else {
        printf("%llu MB", b / (1024ULL * 1024));
    }
}

/* the card given as "E:", "1", "PhysicalDrive1", "/dev/sdb" or "test";
   only drives of the list are accepted */
static int
find_card(const char *arg, card_t *card)
{
    card_t cards[MAX_DRIVES];
    bool larger;
    int n = list_cards(cards, MAX_DRIVES, &larger);
    for (int i = 0; i < n; i++) {
        const card_t *c = &cards[i];
        bool match = strcmp(arg, c->path) == 0 || (c->test && strcmp(arg, "test") == 0);
#ifdef _WIN32
        const char *num = strstr(c->path, "PhysicalDrive");
        match |= num != NULL && (strcasecmp(arg, num) == 0 || strcmp(arg, num + 13) == 0);
        if (isalpha((unsigned char)arg[0]) && (arg[1] == 0 || (arg[1] == ':' && arg[2] == 0))) {
            match |= strchr(c->letters, toupper((unsigned char)arg[0])) != NULL;
        }
#endif
        if (match) {
            *card = *c;
            return 0;
        }
    }
    fprintf(stderr, "ERROR: %s is no removable drive up to 32 GB (see cit --sd-list)%s\n", arg,
            larger ? "; larger drives are left out" : "");
    return -1;
}

/* ------------------------------------------------------------------ */
/* access                                                              */

static int
card_open(const card_t *c, bool write, card_io *io)
{
    memset(io, 0, sizeof *io);
    io->fd = -1;
    if (c->test) {
        io->test = fopen(c->path, write ? "r+b" : "rb");
        return io->test != NULL ? 0 : -1;
    }
#ifdef _WIN32
    /* lock and dismount the volumes on the card, so that Windows does not
       write in between and lets us write their sectors */
    for (int i = 0; c->letters[i]; i++) {
        char p[8];
        snprintf(p, sizeof p, "\\\\.\\%c:", c->letters[i]);
        HANDLE v = CreateFileA(p, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
        DWORD got;
        if (v == INVALID_HANDLE_VALUE || !DeviceIoControl(v, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &got, NULL)) {
            if (v != INVALID_HANDLE_VALUE) {
                CloseHandle(v);
            }
            if (write) {
                fprintf(stderr, "ERROR: %c: is in use (open files or an Explorer window?)\n", c->letters[i]);
                for (int k = 0; k < io->nvol; k++) {
                    CloseHandle(io->vol[k]);
                }
                io->nvol = 0;
                return -1;
            }
            continue;
        }
        DeviceIoControl(v, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &got, NULL);
        io->vol[io->nvol++] = v;
    }
    io->h = CreateFileA(c->path, GENERIC_READ | (write ? GENERIC_WRITE : 0), FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | (write ? FILE_FLAG_WRITE_THROUGH : 0), NULL);
    if (io->h == INVALID_HANDLE_VALUE) {
        for (int k = 0; k < io->nvol; k++) {
            CloseHandle(io->vol[k]);
        }
        io->nvol = 0;
        return -1;
    }
    return 0;
#else
    if (write) {
        /* a mounted card is refused; cit does not unmount by itself */
        FILE *mf = fopen("/proc/mounts", "r");
        if (mf != NULL) {
            char dev[256];
            while (fscanf(mf, "%255s %*[^\n]", dev) == 1) {
                if (strncmp(dev, c->path, strlen(c->path)) == 0) {
                    fclose(mf);
                    fprintf(stderr, "ERROR: %s is mounted, unmount it first (umount %s)\n", c->path, dev);
                    return -1;
                }
            }
            fclose(mf);
        }
    }
    io->fd = open(c->path, write ? O_RDWR : O_RDONLY);
    return io->fd >= 0 ? 0 : -1;
#endif
}

static void
card_close(card_io *io)
{
    if (io->test != NULL) {
        fclose(io->test);
        io->test = NULL;
        return;
    }
#ifdef _WIN32
    if (io->h != INVALID_HANDLE_VALUE && io->h != NULL) {
        DWORD got;
        DeviceIoControl(io->h, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &got, NULL);
        CloseHandle(io->h);
    }
    for (int k = 0; k < io->nvol; k++) {
        CloseHandle(io->vol[k]);                /* unlocks; Windows mounts it anew */
    }
    io->nvol = 0;
#else
    if (io->fd >= 0) {
        fsync(io->fd);
        close(io->fd);
        io->fd = -1;
    }
#endif
}

/* n bytes at offset; n is a multiple of 512. 0 = ok */
static int
card_rw(card_io *io, unsigned long long offset, void *buf, size_t n, bool write)
{
    if (io->test != NULL) {
#ifdef _WIN32
        if (_fseeki64(io->test, (long long)offset, SEEK_SET) != 0) {
#else
        if (fseeko(io->test, (off_t)offset, SEEK_SET) != 0) {
#endif
            return -1;
        }
        size_t done = write ? fwrite(buf, 1, n, io->test) : fread(buf, 1, n, io->test);
        return done == n ? 0 : -1;
    }
#ifdef _WIN32
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)offset;
    DWORD done = 0;
    if (!SetFilePointerEx(io->h, pos, NULL, FILE_BEGIN)) {
        return -1;
    }
    BOOL ok = write ? WriteFile(io->h, buf, (DWORD)n, &done, NULL) : ReadFile(io->h, buf, (DWORD)n, &done, NULL);
    return (ok && done == n) ? 0 : -1;
#else
    ssize_t done = write ? pwrite(io->fd, buf, n, (off_t)offset) : pread(io->fd, buf, n, (off_t)offset);
    return done == (ssize_t)n ? 0 : -1;
#endif
}

/* a buffer aligned for unbuffered access */
static unsigned char *
chunk_buffer(void)
{
#ifdef _WIN32
    unsigned char *b = VirtualAlloc(NULL, CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    unsigned char *b = malloc(CHUNK);
#endif
    if (b == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    return b;
}

static void
free_chunk(unsigned char *b)
{
#ifdef _WIN32
    VirtualFree(b, 0, MEM_RELEASE);
#else
    free(b);
#endif
}

/* "Reading 1250/7600 MB (16%)", one line per percent */
static void
progress(const char *what, unsigned long long done, unsigned long long total, int *last)
{
    int pct = total ? (int)(done * 100 / total) : 100;
    if (pct != *last) {
        *last = pct;
        printf("%s %llu/%llu MB (%d%%)\n", what, done / (1024 * 1024), total / (1024 * 1024), pct);
        fflush(stdout);
    }
}

static void
admin_hint(void)
{
#ifdef _WIN32
    fprintf(stderr, "       (this needs a command prompt started with \"Run as administrator\")\n");
#else
    fprintf(stderr, "       (this needs root rights)\n");
#endif
}

/* ------------------------------------------------------------------ */
/* commands                                                            */

static int
sd_list(void)
{
    card_t cards[MAX_DRIVES];
    bool larger;
    int n = list_cards(cards, MAX_DRIVES, &larger);
    if (n == 0) {
        printf("No removable drive up to 32 GB found%s.\n", larger ? " (larger ones are left out)" : "");
        return 0;
    }
    for (int i = 0; i < n; i++) {
        const char *num = strstr(cards[i].path, "PhysicalDrive");
        printf("  %-20s ", num != NULL ? num : cards[i].path);
        print_size(cards[i].size);
        printf("  %s", cards[i].name);
        if (cards[i].letters[0]) {
            printf("  [");
            for (int k = 0; cards[i].letters[k]; k++) {
                printf("%s%c:", k ? " " : "", cards[i].letters[k]);
            }
            printf("]");
        }
        printf("\n");
    }
    if (larger) {
        printf("Larger removable drives are left out (more than 32 GB).\n");
    }
    return 0;
}

static int
sd_read(const char *drive, const char *image)
{
    card_t c;
    if (find_card(drive, &c) != 0) {
        return -1;
    }
    FILE *probe = fopen(image, "rb");
    if (probe != NULL) {
        fclose(probe);
        fprintf(stderr, "ERROR: %s already exists\n", image);
        return -1;
    }
    card_io io;
    if (card_open(&c, false, &io) != 0) {
        fprintf(stderr, "ERROR: Could not open %s\n", c.path);
        admin_hint();
        return -1;
    }
    FILE *out = fopen(image, "wb");
    if (out == NULL) {
        card_close(&io);
        fprintf(stderr, "ERROR: Could not create %s\n", image);
        return -1;
    }
    printf("Reading %s (%s, ", c.path, c.name);
    print_size(c.size);
    printf(") into %s\n", image);
    unsigned char *buf = chunk_buffer();
    int last = -1;
    int ret = 0;
    for (unsigned long long off = 0; off < c.size; off += CHUNK) {
        size_t n = (c.size - off < CHUNK) ? (size_t)(c.size - off) : CHUNK;
        if (card_rw(&io, off, buf, n, false) != 0) {
            fprintf(stderr, "ERROR: Read error at %llu MB\n", off / (1024 * 1024));
            ret = -1;
            break;
        }
        if (fwrite(buf, 1, n, out) != n) {
            fprintf(stderr, "ERROR: Could not write %s (disk full?)\n", image);
            ret = -1;
            break;
        }
        progress("Reading", off + n, c.size, &last);
    }
    free_chunk(buf);
    card_close(&io);
    if (fclose(out) != 0) {
        ret = -1;
    }
    if (ret != 0) {
        remove(image);
        return -1;
    }
    printf("%s written\n", image);
    return 0;
}

static int
sd_write(const char *image, const char *drive, bool yes)
{
    card_t c;
    if (find_card(drive, &c) != 0) {
        return -1;
    }
    FILE *in = fopen(image, "rb");
    if (in == NULL) {
        fprintf(stderr, "ERROR: Could not open %s\n", image);
        return -1;
    }
    long long size;
#ifdef _WIN32
    _fseeki64(in, 0, SEEK_END);
    size = _ftelli64(in);
    _fseeki64(in, 0, SEEK_SET);
#else
    fseeko(in, 0, SEEK_END);
    size = (long long)ftello(in);
    fseeko(in, 0, SEEK_SET);
#endif
    if (size <= 0 || size % 512 != 0) {
        fprintf(stderr, "ERROR: %s is no image of a card (size no multiple of 512)\n", image);
        fclose(in);
        return -1;
    }
    if ((unsigned long long)size > c.size) {
        fprintf(stderr, "ERROR: %s (%lld MB) is larger than the card (%llu MB)\n", image, size / (1024 * 1024),
                c.size / (1024 * 1024));
        fclose(in);
        return -1;
    }
    printf("Card %s: %s, ", c.path, c.name);
    print_size(c.size);
    if (c.letters[0]) {
        printf(", drive %s", c.letters);
    }
    printf("\nEverything on it will be overwritten with %s (%lld MB).\n", image, size / (1024 * 1024));
    if (!yes) {
        printf("Continue? (yes/no) ");
        fflush(stdout);
        char answer[16] = "";
        if (fgets(answer, sizeof answer, stdin) == NULL || (strncasecmp(answer, "yes", 3) != 0
                && !(tolower((unsigned char)answer[0]) == 'y' && (answer[1] == '\n' || answer[1] == 0)))) {
            printf("Nothing written.\n");
            fclose(in);
            return -1;
        }
    }
    card_io io;
    if (card_open(&c, true, &io) != 0) {
        fprintf(stderr, "ERROR: Could not open %s for writing\n", c.path);
        admin_hint();
        fclose(in);
        return -1;
    }
    unsigned char *buf = chunk_buffer();
    unsigned char *check = malloc(CHUNK);
    if (check == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int last = -1;
    int ret = 0;
    unsigned long long total = (unsigned long long)size;
    for (unsigned long long off = 0; off < total; off += CHUNK) {
        size_t n = (total - off < CHUNK) ? (size_t)(total - off) : CHUNK;
        if (fread(buf, 1, n, in) != n) {
            fprintf(stderr, "ERROR: Could not read %s\n", image);
            ret = -1;
            break;
        }
        if (card_rw(&io, off, buf, n, true) != 0) {
            fprintf(stderr, "ERROR: Write error at %llu MB, the card is now incomplete\n", off / (1024 * 1024));
            ret = -1;
            break;
        }
        progress("Writing", off + n, total, &last);
    }
    card_close(&io);
    /* read back and compare, with the card opened anew */
    if (ret == 0 && card_open(&c, false, &io) == 0) {
#ifdef _WIN32
        _fseeki64(in, 0, SEEK_SET);
#else
        fseeko(in, 0, SEEK_SET);
#endif
        last = -1;
        for (unsigned long long off = 0; off < total; off += CHUNK) {
            size_t n = (total - off < CHUNK) ? (size_t)(total - off) : CHUNK;
            if (fread(check, 1, n, in) != n || card_rw(&io, off, buf, n, false) != 0) {
                fprintf(stderr, "ERROR: Could not read back at %llu MB\n", off / (1024 * 1024));
                ret = -1;
                break;
            }
            if (memcmp(buf, check, n) != 0) {
                fprintf(stderr, "ERROR: The card differs from the image at %llu MB (card faulty?)\n",
                        off / (1024 * 1024));
                ret = -1;
                break;
            }
            progress("Verifying", off + n, total, &last);
        }
        card_close(&io);
    } else if (ret == 0) {
        fprintf(stderr, "ERROR: Could not open the card again to check it\n");
        ret = -1;
    }
    free(check);
    free_chunk(buf);
    fclose(in);
    if (ret == 0) {
        printf("%s written to %s and checked\n", image, c.path);
    }
    return ret;
}

/* cit --sd-list / --sd-read / --sd-write */
int
sdcard_main(int argc, char **argv)
{
    const char *cmd = argv[1];
    if (strcmp(cmd, "--sd-list") == 0) {
        return sd_list();
    }
    bool yes = false;
    const char *a[2];
    int n = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--yes") == 0) {
            yes = true;
        } else if (n < 2) {
            a[n++] = argv[i];
        } else {
            n = 3;
        }
    }
    if (n != 2) {
        fprintf(stderr, "Usage: cit --sd-list\n"
                        "       cit --sd-read  drive image\n"
                        "       cit --sd-write image drive [--yes]\n"
                        "       drive: E:, 1 or PhysicalDrive1 (Windows), /dev/sdb (Linux)\n");
        return -1;
    }
    if (strcmp(cmd, "--sd-read") == 0) {
        return sd_read(a[0], a[1]);
    }
    return sd_write(a[0], a[1], yes);
}
