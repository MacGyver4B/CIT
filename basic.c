/*
 * basic.c - BASIC programs as listings for -XA and the way back
 *
 * -XA writes a BASIC program as name[la0801].bas (token.c does the
 * listing). The BASIC version follows from the load address and the tokens
 * used; another one than the first try is kept in the name as [bNN], e.g.
 * [blre4]. A program with bytes behind its end (machine code behind a SYS
 * line) stays a PRG with a warning. The listing is read back once and must
 * give the very same bytes, else the file stays a PRG as well.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

/* does any line contain a SYS token outside quotes? */
static bool
has_sys(const unsigned char *d, int used)
{
    bool quote = false;
    for (int i = 6; i < used; i++) {
        if (d[i] == 0) {
            quote = false;
            i += 4;                             /* link and line number */
        } else if (d[i] == '"') {
            quote = !quote;
        } else if (d[i] == 0x9e && !quote) {
            return true;
        }
    }
    return false;
}

/* LF of the listing -> the chosen line end */
static char *
with_line_end(const char *t, size_t *len)
{
    size_t lines = 0, n = strlen(t);
    for (size_t i = 0; i < n; i++) {
        lines += t[i] == '\n';
    }
    char *out = malloc(n + lines + 1);
    if (out == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '\n' && textopt.eol == EOL_CRLF) {
            out[o++] = '\r';
            out[o++] = '\n';
        } else if (t[i] == '\n' && textopt.eol == EOL_CR) {
            out[o++] = '\r';
        } else {
            out[o++] = t[i];
        }
    }
    out[o] = 0;
    *len = o;
    return out;
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
    const char *ids[8];
    int n = basic_versions_for(la, ids, 8);
    if (n == 0) {
        return 1;
    }
    int used;
    char *probe = basic_list(data, size, ids[0], &used);
    if (probe == NULL) {
        return 1;                               /* no BASIC program */
    }
    free(probe);
    if (used < size) {
        if (has_sys(data, used)) {
            fprintf(stderr, "WARNING: %s: SYS line followed by %d bytes of machine code, kept as PRG\n", cbmname,
                    size - used);
        } else {
            fprintf(stderr, "WARNING: %s: %d bytes behind the end of the BASIC program, kept as PRG\n", cbmname,
                    size - used);
        }
        return 1;
    }
    /* the first version that knows every token, else the first one */
    int pick = 0;
    for (int k = 0; k < n; k++) {
        if (basic_knows_all(data, used, ids[k])) {
            pick = k;
            break;
        }
    }
    char *t = basic_list(data, size, ids[pick], &used);
    unsigned char *back = NULL;
    int back_size = 0;
    if (t == NULL || basic_crunch(t, strlen(t), la, ids[pick], cbmname, &back, &back_size) != 0
            || back_size != size || memcmp(back, data, (size_t)size) != 0) {
        fprintf(stderr, "WARNING: %s: the BASIC listing would not give the same bytes back, kept as PRG\n",
                cbmname);
        free(t);
        free(back);
        return 1;
    }
    free(back);
    *text = with_line_end(t, len);
    free(t);
    if (pick == 0) {
        snprintf(tags, tags_size, "[la%04x]", la);
    } else {
        snprintf(tags, tags_size, "[la%04x][b%s]", la, ids[pick]);
    }
    return 0;
}

/* listing -> BASIC program; version NULL = the default for the load
   address. 0 = ok, -1 = error (message printed) */
int
listing_to_basic(const char *text, size_t len, unsigned int load_address, const char *version, const char *what,
                 unsigned char **data, int *size)
{
    if (version == NULL) {
        const char *ids[8];
        int n = basic_versions_for(load_address, ids, 8);
        version = (n > 0) ? ids[0] : "2";
    }
    return basic_crunch(text, len, load_address, version, what, data, size);
}
