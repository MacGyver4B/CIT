/*
 * textconv.c - PETSCII files as text for -XA and the way back
 *
 *   SEQ  <-> .txt  one line per CR; ASCII or UTF-8
 *   REL  <-> .csv  one line per record, fields split at CR
 *
 * Every byte that has no character in the chosen set is written as
 * {0xNN}, so the way back gives the same bytes. Letters follow the
 * lowercase/uppercase character set by default (--charset upper for the
 * uppercase/graphics one). PETSCII has no { or }, so an escape cannot be
 * mixed up with text.
 *
 * $00 can stand for something else: --seq-00 / --rel-00 "|" or "{EOR}"
 * write it as that text, "eol" makes it the line end (SEQ) or the field
 * separator (REL) instead of CR, which is then written as {0x0D}.
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

text_options textopt;

void
text_options_init(void)
{
#ifdef _WIN32
    textopt.eol = (config.text_eol >= 0) ? config.text_eol : EOL_CRLF;
#else
    textopt.eol = (config.text_eol >= 0) ? config.text_eol : EOL_LF;
#endif
    textopt.upper = config.text_upper > 0;
    textopt.seq_mode = SEQ_AUTO;
    textopt.csv_sep = config.csv_sep ? config.csv_sep : ';';
    textopt.seq_text = config.seq_text >= 0 ? config.seq_text : 90;
    textopt.seq_mixed = config.seq_mixed >= 0 ? config.seq_mixed : 75;
    textopt.seq00 = config.seq00;
    textopt.rel00 = config.rel00;
}

/* ------------------------------------------------------------------ */
/* character maps                                                      */

/* map[b]: the text of byte b, "" = no character (escaped) */
static char map_ascii[2][256][2];
static char map_utf8[2][256][5];
static bool maps_built;

static void
build_maps(void)
{
    static const int order[][2] = { { 0x20, 0x5f }, { 0xc0, 0xdf }, { 0x60, 0x7f }, { 0xa0, 0xbf }, { 0xe0, 0xff } };
    for (int lower = 0; lower < 2; lower++) {
        /* the first byte in this order gets a character, later bytes with
           the same character are escaped, so the way back is unique */
        static unsigned int used_ascii[256];
        static unsigned int used_utf8[512];
        int n_ascii = 0;
        int n_utf8 = 0;
        for (unsigned int r = 0; r < sizeof order / sizeof order[0]; r++) {
            for (int b = order[r][0]; b <= order[r][1]; b++) {
                unsigned int u = petscii_unicode((unsigned char)b, lower == 0);
                if (b == 0x27) {
                    u = '\'';                   /* the table has U+2019 */
                }
                if (u == '{' || u == '}') {
                    continue;
                }
                bool taken = false;
                for (int i = 0; i < n_utf8; i++) {
                    taken |= used_utf8[i] == u;
                }
                if (!taken) {
                    used_utf8[n_utf8++] = u;
                    *utf8_encode((int)u, map_utf8[lower][b]) = 0;
                }
                if (u >= 0x20 && u < 0x7f) {
                    taken = false;
                    for (int i = 0; i < n_ascii; i++) {
                        taken |= used_ascii[i] == u;
                    }
                    if (!taken) {
                        used_ascii[n_ascii++] = u;
                        map_ascii[lower][b][0] = (char)u;
                    }
                }
            }
        }
    }
    maps_built = true;
}

/* index 0: lowercase/uppercase set, 1: uppercase/graphics set */
static const char *
char_of(unsigned char b, bool utf8)
{
    if (!maps_built) {
        build_maps();
    }
    int set = textopt.upper ? 1 : 0;
    return utf8 ? map_utf8[set][b] : map_ascii[set][b];
}

/* byte of the character at s (UTF-8), -1 if PETSCII has none; *len = bytes used */
static int
byte_of(const char *s, size_t avail, int *len)
{
    if (!maps_built) {
        build_maps();
    }
    int set = textopt.upper ? 1 : 0;
    for (int b = 0; b < 256; b++) {
        const char *m = map_utf8[set][b];
        size_t l = strlen(m);
        if (l > 0 && l <= avail && memcmp(s, m, l) == 0) {
            *len = (int)l;
            return b;
        }
    }
    *len = 1;
    return -1;
}

/* share of text bytes in percent: characters of the ASCII map and CR */
int
text_share(const unsigned char *data, int size)
{
    if (size == 0) {
        return 100;
    }
    int n = 0;
    for (int i = 0; i < size; i++) {
        if (data[i] == 0x0d || char_of(data[i], false)[0] != 0) {
            n++;
        }
    }
    return (int)((100LL * n) / size);
}

/* ------------------------------------------------------------------ */
/* growing buffer                                                      */

typedef struct {
    char *p;
    size_t len;
    size_t cap;
} buf_t;

static void
put(buf_t *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 4096;
        while (c < b->len + n + 1) {
            c *= 2;
        }
        char *p = realloc(b->p, c);
        if (p == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        b->p = p;
        b->cap = c;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void
puts_(buf_t *b, const char *s)
{
    put(b, s, strlen(s));
}

static void
put_eol(buf_t *b)
{
    puts_(b, textopt.eol == EOL_CRLF ? "\r\n" : textopt.eol == EOL_CR ? "\r" : "\n");
}

/* "eol"/"sep": $00 separates instead of CR; NULL: escaped; else its text */
static bool
zero_separates(const char *rep)
{
    return rep != NULL && (strcmp(rep, "eol") == 0 || strcmp(rep, "sep") == 0);
}

/* the replacement for $00 must not be text of another byte */
int
check_zero_text(const char *rep, const char *option)
{
    if (rep == NULL || zero_separates(rep)) {
        return 0;
    }
    if (rep[0] == 0) {
        fprintf(stderr, "ERROR: %s needs a text\n", option);
        return -1;
    }
    if (rep[0] == '{') {
        size_t l = strlen(rep);
        if (rep[l - 1] != '}' || strncasecmp(rep, "{0x", 3) == 0 || strchr(rep + 1, '{') != NULL) {
            fprintf(stderr, "ERROR: %s %s: a tag must look like {EOR} and must not start with {0x\n", option, rep);
            return -1;
        }
        return 0;
    }
    for (const char *c = rep; *c; c++) {
        int l;
        if (*c == '\r' || *c == '\n' || *c == '}' || byte_of(c, 1, &l) >= 0 || *c == textopt.csv_sep || *c == '"') {
            fprintf(stderr, "ERROR: %s %s: '%c' is also text of the file, use e.g. \"|\" or \"{EOR}\"\n",
                    option, rep, *c);
            return -1;
        }
    }
    return 0;
}

/* one byte as text */
static void
put_byte(buf_t *b, unsigned char c, bool utf8, const char *rep00)
{
    if (c == 0 && rep00 != NULL && !zero_separates(rep00)) {
        puts_(b, rep00);
        return;
    }
    const char *m = char_of(c, utf8);
    if (m[0]) {
        puts_(b, m);
    } else {
        char e[8];
        snprintf(e, sizeof e, "{0x%02X}", c);
        puts_(b, e);
    }
}

/* parses one character of text at s into a byte; returns the length used,
   0 on error (message printed) */
static int
parse_char(const char *s, size_t avail, const char *rep00, int line, const char *what, int *byte)
{
    if (rep00 != NULL && !zero_separates(rep00) && strncmp(s, rep00, strlen(rep00)) == 0
            && strlen(rep00) <= avail) {
        *byte = 0;
        return (int)strlen(rep00);
    }
    if (s[0] == '{') {
        unsigned int v;
        char close;
        if (avail >= 6 && (s[1] == '0') && (s[2] == 'x' || s[2] == 'X') && sscanf(s + 3, "%2x%c", &v, &close) == 2
                && close == '}' && isxdigit((unsigned char)s[3]) && isxdigit((unsigned char)s[4])) {
            *byte = (int)v;
            return 6;
        }
        fprintf(stderr, "ERROR: %s line %d: unknown escape %.8s\n", what, line, s);
        return 0;
    }
    int l;
    int b = byte_of(s, avail, &l);
    if (b < 0) {
        unsigned char c = (unsigned char)s[0];
        int ul = (c >= 0xf0) ? 4 : (c >= 0xe0) ? 3 : (c >= 0xc0) ? 2 : 1;
        fprintf(stderr, "ERROR: %s line %d: '%.*s' has no PETSCII character, write it as {0xNN}\n",
                what, line, ul, s);
        return 0;
    }
    *byte = b;
    return l;
}

/* ------------------------------------------------------------------ */
/* SEQ                                                                 */

char *
seq_to_text(const unsigned char *data, int size, bool utf8, size_t *len)
{
    buf_t b = { NULL, 0, 0 };
    put(&b, "", 0);
    unsigned char eol = zero_separates(textopt.seq00) ? 0x00 : 0x0d;
    for (int i = 0; i < size; i++) {
        if (data[i] == eol) {
            put_eol(&b);
        } else {
            put_byte(&b, data[i], utf8, textopt.seq00);
        }
    }
    *len = b.len;
    return b.p;
}

/* text (ASCII or UTF-8, any line end) back to SEQ data; -1 on error */
int
text_to_seq(const char *text, size_t len, const char *what, unsigned char **data, int *size)
{
    unsigned char eol = zero_separates(textopt.seq00) ? 0x00 : 0x0d;
    unsigned char *d = malloc(len + 1);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    int n = 0;
    int line = 1;
    size_t i = 0;
    if (len >= 3 && memcmp(text, "\xef\xbb\xbf", 3) == 0) {
        i = 3;                                  /* UTF-8 byte order mark */
    }
    while (i < len) {
        if (text[i] == '\r' || text[i] == '\n') {
            i += (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') ? 2 : 1;
            d[n++] = eol;
            line++;
            continue;
        }
        int byte;
        int used = parse_char(text + i, len - i, textopt.seq00, line, what, &byte);
        if (used == 0) {
            free(d);
            return -1;
        }
        d[n++] = (unsigned char)byte;
        i += used;
    }
    *data = d;
    *size = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* REL as CSV                                                          */

static void
put_field(buf_t *b, const char *f, size_t l, bool only)
{
    bool quote = (only && l == 0);
    for (size_t i = 0; i < l; i++) {
        quote |= f[i] == textopt.csv_sep || f[i] == '"';
    }
    if (!quote) {
        put(b, f, l);
        return;
    }
    puts_(b, "\"");
    for (size_t i = 0; i < l; i++) {
        put(b, f[i] == '"' ? "\"\"" : f + i, f[i] == '"' ? 2 : 1);
    }
    puts_(b, "\"");
}

char *
rel_to_csv(const unsigned char *data, int size, int record_length, size_t *len)
{
    buf_t b = { NULL, 0, 0 };
    put(&b, "", 0);
    unsigned char sep = zero_separates(textopt.rel00) ? 0x00 : 0x0d;
    for (int r = 0; r * record_length < size; r++) {
        const unsigned char *rec = data + r * record_length;
        int n = size - r * record_length;
        if (n > record_length) {
            n = record_length;
        }
        bool empty = (n > 0 && rec[0] == 0xff);
        for (int i = 1; empty && i < n; i++) {
            empty = rec[i] == 0;
        }
        if (!empty) {
            while (n > 0 && rec[n - 1] == 0) {
                n--;                            /* filled up by the DOS */
            }
            buf_t field = { NULL, 0, 0 };
            put(&field, "", 0);
            bool first = true;
            for (int i = 0; i <= n; i++) {
                if (i == n || rec[i] == sep) {
                    if (!first) {
                        put(&b, &textopt.csv_sep, 1);
                    }
                    put_field(&b, field.p, field.len, first && i == n);
                    first = false;
                    field.len = 0;
                    field.p[0] = 0;
                } else {
                    put_byte(&field, rec[i], false, textopt.rel00);
                }
            }
            free(field.p);
        }
        put_eol(&b);
    }
    *len = b.len;
    return b.p;
}

/* CSV back to REL records; record_length 0 = from the longest line.
   Excel may add separators at the line ends: a line of nothing but
   separators is an unused record. -1 on error */
int
csv_to_rel(const char *text, size_t len, const char *what, int *record_length, unsigned char **data, int *size)
{
    unsigned char sep = zero_separates(textopt.rel00) ? 0x00 : 0x0d;
    unsigned char *recs = NULL;
    int *lens = NULL;
    int count = 0;
    int cap = 0;
    unsigned char cur[256 * 6];
    int n = 0;
    bool quoted = false;
    bool any = false;                           /* line had a character */
    bool content = false;                       /* ... other than separators */
    int line = 1;
    size_t i = 0;
    if (len >= 3 && memcmp(text, "\xef\xbb\xbf", 3) == 0) {
        i = 3;
    }
    for (;;) {
        bool end_of_line = (i >= len) || (!quoted && (text[i] == '\r' || text[i] == '\n'));
        if (end_of_line) {
            if (i >= len && !any) {
                break;
            }
            if (count == cap) {
                cap = cap ? cap * 2 : 256;
                recs = realloc(recs, (size_t)cap * 256);
                lens = realloc(lens, (size_t)cap * sizeof(int));
                if (recs == NULL || lens == NULL) {
                    fprintf(stderr, "ERROR: Memory allocation error\n");
                    exit(-1);
                }
            }
            if (n > 254) {
                fprintf(stderr, "ERROR: %s line %d: record of %d bytes, at most 254 fit\n", what, line, n);
                free(recs);
                free(lens);
                return -1;
            }
            memcpy(recs + (size_t)count * 256, cur, (size_t)n);
            lens[count++] = content ? n : -1;   /* -1: unused record */
            n = 0;
            any = content = false;
            if (i >= len) {
                break;
            }
            i += (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') ? 2 : 1;
            line++;
            continue;
        }
        any = true;
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < len && text[i + 1] == '"') {
                    cur[n++] = 0x22;
                    i += 2;
                } else {
                    quoted = false;
                    i++;
                }
                continue;
            }
        } else if (c == '"') {
            quoted = true;
            content = true;                     /* "" is a record of only $00 */
            i++;
            continue;
        } else if (c == textopt.csv_sep) {
            cur[n++] = sep;
            i++;
            continue;
        }
        if (c == '\r' || c == '\n') {
            fprintf(stderr, "ERROR: %s line %d: line break inside a quoted field\n", what, line);
            free(recs);
            free(lens);
            return -1;
        }
        int byte;
        int used = parse_char(text + i, len - i, textopt.rel00, line, what, &byte);
        if (used == 0) {
            free(recs);
            free(lens);
            return -1;
        }
        cur[n++] = (unsigned char)byte;
        content = true;
        i += used;
        if (n >= (int)sizeof cur - 8) {
            fprintf(stderr, "ERROR: %s line %d: record longer than 254 bytes\n", what, line);
            free(recs);
            free(lens);
            return -1;
        }
    }
    int rl = *record_length;
    if (rl == 0) {
        for (int r = 0; r < count; r++) {
            rl = lens[r] > rl ? lens[r] : rl;
        }
        rl = rl < 1 ? 1 : rl;
    }
    for (int r = 0; r < count; r++) {
        if (lens[r] > rl) {
            fprintf(stderr, "ERROR: %s line %d: record of %d bytes, the record length is %d\n", what, r + 1,
                    lens[r], rl);
            free(recs);
            free(lens);
            return -1;
        }
    }
    unsigned char *d = calloc((size_t)(count ? count : 1), (size_t)rl);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    for (int r = 0; r < count; r++) {
        if (lens[r] < 0) {
            d[(size_t)r * rl] = 0xff;           /* unused record */
        } else {
            memcpy(d + (size_t)r * rl, recs + (size_t)r * 256, (size_t)lens[r]);
        }
    }
    free(recs);
    free(lens);
    *record_length = rl;
    *data = d;
    *size = count * rl;
    return 0;
}
