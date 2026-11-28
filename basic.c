/*
 * basic.c - BASIC programs as listings through petcat (VICE)
 *
 * -XA writes a BASIC program as name[la0801].bas when petcat gives back
 * the very same bytes from the listing. Otherwise, and when machine code
 * follows the program (a SYS line with code behind it), the file stays a
 * PRG and a warning says why. The BASIC version follows from the load
 * address; another one than the first try is kept in the name as [bNN].
 *
 * petcat comes from "petcat" in cit.ini, else from PATH.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

/* BASIC versions to try for a load address, the first one is the default */
static const char *const *
versions_for(unsigned int la)
{
    static const char *const c64[] = { "2", NULL };                    /* C64, VIC-20 +8K */
    static const char *const pet[] = { "40", "2", "1p", NULL };        /* PET, VIC-20 +3K */
    static const char *const vic[] = { "2", "3", NULL };               /* VIC-20, C16/plus4 */
    static const char *const c128[] = { "70", "71", NULL };
    switch (la) {
    case 0x0801:
    case 0x1201:
        return c64;
    case 0x0401:
        return pet;
    case 0x1001:
        return vic;
    case 0x1c01:
    case 0x4001:
        return c128;
    default:
        return NULL;
    }
}

/* runs petcat, output to nowhere; 0 = ok, -1 = could not start, 1 = failed */
static int
run_petcat(const char *const args[])
{
    return run_program(config.petcat != NULL ? config.petcat : "petcat", args, true);
}

static int
write_file(const char *name, const void *data, size_t size)
{
    FILE *f = fopen(name, "wb");
    if (f == NULL) {
        return -1;
    }
    size_t w = fwrite(data, 1, size, f);
    return (fclose(f) == 0 && w == size) ? 0 : -1;
}

static int
read_file(const char *name, unsigned char **data, size_t *size)
{
    long l;
    int r = read_whole_file(name, data, &l);
    *size = (size_t)l;
    return r;
}

static bool petcat_missing_reported;

static void
report_missing(void)
{
    if (!petcat_missing_reported) {
        fprintf(stderr, "WARNING: petcat not found (set petcat in cit.ini); BASIC programs stay PRG files\n");
        petcat_missing_reported = true;
    }
}

/* walks the line links: offset of the byte behind the end marker, -1 if
   this is no BASIC program; *sys = a SYS token outside quotes */
static int
basic_end(const unsigned char *d, int size, unsigned int la, bool *sys)
{
    *sys = false;
    unsigned int p = la;
    for (int lines = 0; lines < 10000; lines++) {
        int o = (int)(p - la) + 2;
        if (o + 1 >= size) {
            return -1;
        }
        unsigned int link = d[o] | (d[o + 1] << 8);
        if (link == 0) {
            return o + 2;
        }
        int next = (int)(link - la) + 2;
        if (link <= p + 4 || next > size || d[next - 1] != 0) {
            return -1;
        }
        bool quote = false;
        for (int i = o + 4; i < next - 1; i++) {
            if (d[i] == 0) {
                return -1;
            }
            if (d[i] == '"') {
                quote = !quote;
            } else if (d[i] == 0x9e && !quote) {
                *sys = true;
            }
        }
        p = link;
    }
    return -1;
}

/* BASIC program -> listing. 0 = converted (text, tags "[la0801]" ...),
   1 = keep the PRG (a warning was printed if it looked like BASIC) */
int
basic_to_listing(const unsigned char *data, int size, const char *cbmname, char **text, size_t *len,
                 char *tags, size_t tags_size)
{
    if (size < 4) {
        return 1;
    }
    unsigned int la = data[0] | (data[1] << 8);
    const char *const *versions = versions_for(la);
    if (versions == NULL) {
        return 1;
    }
    bool sys;
    int end = basic_end(data, size, la, &sys);
    if (end < 0) {
        return 1;
    }
    if (end < size) {
        if (sys) {
            fprintf(stderr, "WARNING: %s: SYS line followed by %d bytes of machine code, kept as PRG\n", cbmname,
                    size - end);
        } else {
            fprintf(stderr, "WARNING: %s: %d bytes behind the end of the BASIC program, kept as PRG\n", cbmname,
                    size - end);
        }
        return 1;
    }
    char prg[1024], bas[1024], back[1024];
    if (temp_file_name(prg, sizeof prg) != 0 || temp_file_name(bas, sizeof bas) != 0 || temp_file_name(back, sizeof back) != 0) {
        fprintf(stderr, "WARNING: %s: no temporary file, kept as PRG\n", cbmname);
        return 1;
    }
    int result = 1;
    bool started = true;
    if (write_file(prg, data, (size_t)size) == 0) {
        char lahex[8];
        snprintf(lahex, sizeof lahex, "%x", la);
        for (int v = 0; versions[v] != NULL && result != 0 && started; v++) {
            char list_opt[16], token_opt[16];
            snprintf(list_opt, sizeof list_opt, "-%s", versions[v]);
            snprintf(token_opt, sizeof token_opt, "-w%s", versions[v]);
            const char *a1[] = { list_opt, "-o", bas, "--", prg, NULL };
            int r = run_petcat(a1);
            if (r < 0) {
                report_missing();
                started = false;
                break;
            }
            const char *a2[] = { token_opt, "-l", lahex, "-o", back, "--", bas, NULL };
            unsigned char *b = NULL;
            size_t bl = 0;
            if (r == 0 && run_petcat(a2) == 0 && read_file(back, &b, &bl) == 0 && bl == (size_t)size
                    && memcmp(b, data, bl) == 0) {
                unsigned char *t;
                size_t tl;
                if (read_file(bas, &t, &tl) == 0) {
                    /* petcat writes LF; give the chosen line end */
                    size_t lines = 0;
                    for (size_t i = 0; i < tl; i++) {
                        lines += t[i] == '\n';
                    }
                    char *out = malloc(tl + lines + 1);
                    if (out == NULL) {
                        fprintf(stderr, "ERROR: Memory allocation error\n");
                        exit(-1);
                    }
                    size_t o = 0;
                    for (size_t i = 0; i < tl; i++) {
                        if (t[i] == '\n' && textopt.eol != EOL_LF) {
                            if (textopt.eol == EOL_CRLF) {
                                out[o++] = '\r';
                                out[o++] = '\n';
                            } else {
                                out[o++] = '\r';
                            }
                        } else if (t[i] != '\r') {
                            out[o++] = (char)t[i];
                        }
                    }
                    out[o] = 0;
                    free(t);
                    *text = out;
                    *len = o;
                    if (v == 0) {
                        snprintf(tags, tags_size, "[la%04x]", la);
                    } else {
                        snprintf(tags, tags_size, "[la%04x][b%s]", la, versions[v]);
                    }
                    result = 0;
                }
            }
            free(b);
        }
        if (result != 0 && started) {
            fprintf(stderr, "WARNING: %s: the BASIC listing would not give the same bytes back, kept as PRG\n",
                    cbmname);
        }
    }
    remove(prg);
    remove(bas);
    remove(back);
    return result;
}

/* listing -> BASIC program; version NULL = the default for the load
   address. 0 = ok, -1 = error (message printed) */
int
listing_to_basic(const char *text, size_t len, unsigned int load_address, const char *version, const char *what,
                 unsigned char **data, int *size)
{
    if (version == NULL) {
        const char *const *v = versions_for(load_address);
        version = (v != NULL) ? v[0] : "2";
    }
    char bas[1024], prg[1024];
    if (temp_file_name(bas, sizeof bas) != 0 || temp_file_name(prg, sizeof prg) != 0) {
        fprintf(stderr, "ERROR: %s: no temporary file\n", what);
        return -1;
    }
    /* petcat wants LF */
    char *t = malloc(len + 1);
    if (t == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\r') {
            t[n++] = '\n';
            if (i + 1 < len && text[i + 1] == '\n') {
                i++;
            }
        } else {
            t[n++] = text[i];
        }
    }
    int ret = -1;
    if (write_file(bas, t, n) == 0) {
        char lahex[8], token_opt[16];
        snprintf(lahex, sizeof lahex, "%x", load_address);
        snprintf(token_opt, sizeof token_opt, "-w%s", version);
        const char *a[] = { token_opt, "-l", lahex, "-o", prg, "--", bas, NULL };
        int r = run_petcat(a);
        size_t l;
        if (r < 0) {
            fprintf(stderr, "ERROR: %s: petcat not found (set petcat in cit.ini)\n", what);
        } else if (r != 0 || read_file(prg, data, &l) != 0) {
            fprintf(stderr, "ERROR: %s: petcat could not convert the listing\n", what);
        } else {
            *size = (int)l;
            ret = 0;
        }
    }
    free(t);
    remove(bas);
    remove(prg);
    return ret;
}
