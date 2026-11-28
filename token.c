/*
 * token.c - BASIC programs as listings and back, without petcat
 *
 * The listing uses the notation of petcat (VICE): keywords in lower case,
 * {clr}, {down}, {CBM-A} ... inside strings, {$xx} for any other byte, so
 * listings of both tools can be read by both. In addition:
 *   - a byte outside strings that would become part of a keyword when the
 *     listing is read back is written as {$xx}, so the way back gives the
 *     same bytes (petcat would turn the stored letters "to" into TO)
 *   - spaces at the end of a line are written as {$20}, editors drop them
 *     (machine code in REM lines)
 *
 * Versions: 1p (PET BASIC 1), 2 (C64, VIC-20, PET), 40 (PET BASIC 4),
 * 3 (C16, plus/4 BASIC 3.5), 70/71 (C128 BASIC 7.0/7.1), lre2/lre4
 * (PET 2001 LRE: BASIC 2 or 4 with the graphics BASIC tokens $DB-$F2).
 * More can be added with "basic.tokens = file" in cit.ini, see
 * load_token_file().
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

/* ------------------------------------------------------------------ */
/* keywords                                                            */

static const char *const kw_2[] = {          /* $80-$CB */
    "end", "for", "next", "data", "input#", "input", "dim", "read", "let", "goto", "run", "if",
    "restore", "gosub", "return", "rem", "stop", "on", "wait", "load", "save", "verify", "def",
    "poke", "print#", "print", "cont", "list", "clr", "cmd", "sys", "open", "close", "get", "new",
    "tab(", "to", "fn", "spc(", "then", "not", "step", "+", "-", "*", "/", "^", "and", "or", ">",
    "=", "<", "sgn", "int", "abs", "usr", "fre", "pos", "sqr", "rnd", "log", "exp", "cos", "sin",
    "tan", "atn", "peek", "len", "str$", "val", "asc", "chr$", "left$", "right$", "mid$", "go"
};
static const char *const kw_4[] = {          /* $CC-$DA */
    "concat", "dopen", "dclose", "record", "header", "collect", "backup", "copy", "append",
    "dsave", "dload", "catalog", "rename", "scratch", "directory"
};
static const char *const kw_35[] = {         /* $CC-$FD */
    "rgr", "rclr", "rlum", "joy", "rdot", "dec", "hex$", "err$", "instr", "else", "resume",
    "trap", "tron", "troff", "sound", "vol", "auto", "pudef", "graphic", "paint", "char", "box",
    "circle", "gshape", "sshape", "draw", "locate", "color", "scnclr", "scale", "help", "do",
    "loop", "exit", "directory", "dsave", "dload", "header", "scratch", "collect", "copy",
    "rename", "backup", "delete", "renumber", "key", "monitor", "using", "until", "while"
};
static const char *const kw_7_ce[] = {       /* $CE $02-$0A */
    "pot", "bump", "pen", "rsppos", "rsprite", "rspcolor", "xor", "rwindow", "pointer"
};
static const char *const kw_7_fe[] = {       /* $FE $02-$26, 7.1 up to $37 */
    "bank", "filter", "play", "tempo", "movspr", "sprite", "sprcolor", "rreg", "envelope",
    "sleep", "catalog", "dopen", "append", "dclose", "bsave", "bload", "record", "concat",
    "dverify", "dclear", "sprsav", "collision", "begin", "bend", "window", "boot", "width",
    "sprdef", "quit", "stash", "", "fetch", "", "swap", "off", "fast", "slow",
    /* 7.1 */
    "cwind", "sscrn", "lscrn", "hide", "show", "sfont", "lfont", "view", "fcopy", "esave",
    "send", "check", "esc", "old", "find", "dump", "merge"
};
#define FE_70_LAST 0x26
static const char *const kw_lre[] = {        /* $DB-$F2, PET 2001 LRE graphics BASIC */
    "init", "map", "pspace", "plot", "dplot", "chplot", "chrori", "chrsiz", "lintyp", "cscr",
    "display", "idplot", "iplot", "cursor", "mode", "hcopy", "tstp", "tstb", "icrcl", "arcus",
    "font", "gfxbasic", "caps", "drive"
};

/* control codes and graphics inside strings, names as petcat writes them */
static const char *const code_names[256] = {
    [0x01] = "CTRL-A", [0x02] = "CTRL-B", [0x03] = "stop", [0x04] = "CTRL-D", [0x05] = "wht",
    [0x06] = "CTRL-F", [0x07] = "CTRL-G", [0x08] = "dish", [0x09] = "ensh", [0x0b] = "CTRL-K",
    [0x0c] = "CTRL-L", [0x0e] = "swlc", [0x0f] = "CTRL-O", [0x10] = "CTRL-P", [0x11] = "down",
    [0x12] = "rvon", [0x13] = "home", [0x14] = "del", [0x15] = "CTRL-U", [0x16] = "CTRL-V",
    [0x17] = "CTRL-W", [0x18] = "CTRL-X", [0x19] = "CTRL-Y", [0x1a] = "CTRL-Z", [0x1b] = "esc",
    [0x1c] = "red", [0x1d] = "rght", [0x1e] = "grn", [0x1f] = "blu", [0x81] = "orng",
    [0x85] = "f1", [0x86] = "f3", [0x87] = "f5", [0x88] = "f7", [0x89] = "f2", [0x8a] = "f4",
    [0x8b] = "f6", [0x8c] = "f8", [0x8d] = "sret", [0x8e] = "swuc", [0x90] = "blk", [0x91] = "up",
    [0x92] = "rvof", [0x93] = "clr", [0x94] = "inst", [0x95] = "brn", [0x96] = "lred",
    [0x97] = "gry1", [0x98] = "gry2", [0x99] = "lgrn", [0x9a] = "lblu", [0x9b] = "gry3",
    [0x9c] = "pur", [0x9d] = "left", [0x9e] = "yel", [0x9f] = "cyn", [0xa1] = "CBM-K",
    [0xa2] = "CBM-I", [0xa3] = "CBM-T", [0xa4] = "CBM-@", [0xa5] = "CBM-G", [0xa6] = "CBM-+",
    [0xa7] = "CBM-M", [0xa8] = "CBM-POUND", [0xa9] = "SHIFT-POUND", [0xaa] = "CBM-N",
    [0xab] = "CBM-Q", [0xac] = "CBM-D", [0xad] = "CBM-Z", [0xae] = "CBM-S", [0xaf] = "CBM-P",
    [0xb0] = "CBM-A", [0xb1] = "CBM-E", [0xb2] = "CBM-R", [0xb3] = "CBM-W", [0xb4] = "CBM-H",
    [0xb5] = "CBM-J", [0xb6] = "CBM-L", [0xb7] = "CBM-Y", [0xb8] = "CBM-U", [0xb9] = "CBM-O",
    [0xba] = "SHIFT-@", [0xbb] = "CBM-F", [0xbc] = "CBM-C", [0xbd] = "CBM-X", [0xbe] = "CBM-V",
    [0xbf] = "CBM-B", [0xc0] = "SHIFT-*", [0xdb] = "SHIFT-+", [0xdc] = "CBM--", [0xdd] = "SHIFT--",
    [0xdf] = "CBM-*",
};
/* BASIC 3.5 (C16, plus/4) names its colours differently */
static const char *const code_names_35[256] = {
    [0x1f] = "dblu", [0x82] = "flon", [0x84] = "flof", [0x8c] = "help", [0x97] = "pink",
    [0x98] = "blgn", [0x99] = "lblu", [0x9a] = "blu", [0x9b] = "lgrn",
};

/* ------------------------------------------------------------------ */
/* versions                                                            */

#define MAX_VERSIONS 24

typedef struct {
    char id[16];
    char description[64];
    const char *tok[256];      /* single byte tokens, NULL = none */
    const char *ce[256];       /* $CE xx (BASIC 7) */
    const char *fe[256];       /* $FE xx (BASIC 7) */
    bool prefixed;             /* $CE and $FE are prefixes */
    bool names35;
} basic_version;

static basic_version versions[MAX_VERSIONS];
static int num_versions;

static basic_version *
new_version(const char *id, const char *description)
{
    basic_version *v = &versions[num_versions++];
    memset(v, 0, sizeof *v);
    snprintf(v->id, sizeof v->id, "%s", id);
    snprintf(v->description, sizeof v->description, "%s", description);
    return v;
}

static void
put_words(basic_version *v, int first, const char *const *w, int n)
{
    for (int i = 0; i < n; i++) {
        v->tok[first + i] = w[i];
    }
}

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static bool versions_built;

static void
build_versions(void)
{
    if (versions_built) {
        return;
    }
    versions_built = true;
    basic_version *v;
    v = new_version("1p", "PET BASIC 1.0");
    put_words(v, 0x80, kw_2, COUNT(kw_2) - 1);          /* no GO yet */
    v->tok[0xff] = "~";
    v = new_version("2", "BASIC 2.0 (C64, VIC-20, PET)");
    put_words(v, 0x80, kw_2, COUNT(kw_2));
    v->tok[0xff] = "~";
    v = new_version("40", "BASIC 4.0 (PET)");
    put_words(v, 0x80, kw_2, COUNT(kw_2));
    put_words(v, 0xcc, kw_4, COUNT(kw_4));
    v->tok[0xff] = "~";
    v = new_version("3", "BASIC 3.5 (C16, plus/4)");
    put_words(v, 0x80, kw_2, COUNT(kw_2));
    put_words(v, 0xcc, kw_35, COUNT(kw_35));
    v->tok[0xff] = "~";
    v->names35 = true;
    for (int x = 0; x < 2; x++) {
        v = new_version(x ? "71" : "70", x ? "BASIC 7.1 (C128)" : "BASIC 7.0 (C128)");
        put_words(v, 0x80, kw_2, COUNT(kw_2));
        put_words(v, 0xcc, kw_35, COUNT(kw_35));
        v->tok[0xce] = NULL;                              /* prefix instead of RLUM */
        v->tok[0xfe] = NULL;
        v->tok[0xff] = "~";
        v->prefixed = true;
        for (int i = 0; i < COUNT(kw_7_ce); i++) {
            v->ce[0x02 + i] = kw_7_ce[i];
        }
        int last = x ? 0x02 + COUNT(kw_7_fe) - 1 : FE_70_LAST;
        for (int i = 0x02; i <= last; i++) {
            v->fe[i] = kw_7_fe[i - 0x02][0] ? kw_7_fe[i - 0x02] : NULL;
        }
    }
    v = new_version("lre2", "PET 2001 LRE, BASIC 2 with graphics BASIC");
    put_words(v, 0x80, kw_2, COUNT(kw_2));
    put_words(v, 0xdb, kw_lre, COUNT(kw_lre));
    v->tok[0xff] = "~";
    v = new_version("lre4", "PET 2001 LRE, BASIC 4 with graphics BASIC");
    put_words(v, 0x80, kw_2, COUNT(kw_2));
    put_words(v, 0xcc, kw_4, COUNT(kw_4));
    put_words(v, 0xdb, kw_lre, COUNT(kw_lre));
    v->tok[0xff] = "~";
}

static basic_version *
find_version(const char *id)
{
    build_versions();
    for (int i = 0; i < num_versions; i++) {
        if (strcmp(versions[i].id, id) == 0) {
            return &versions[i];
        }
    }
    return NULL;
}

/* "basic.tokens = file" in cit.ini: more versions, one block each

       [simons]                 id, used as [bsimons] in file names
       base = 2                 a built-in version to start from
       load = 0801              optional: load address it is tried for
       cc = fred                token = keyword (hex, keyword in lower case)

   Returns 0, or -1 (message printed). */
static unsigned int extra_load[MAX_VERSIONS];

int
load_token_file(const char *path)
{
    build_versions();
    unsigned char *text;
    long size;
    if (read_whole_file(path, &text, &size) != 0) {
        fprintf(stderr, "WARNING: basic.tokens: could not read %s\n", path);
        return -1;
    }
    basic_version *v = NULL;
    int line = 0;
    char *next_line = (char *)text;
    while (next_line != NULL && *next_line) {
        char *l = next_line;
        char *nl = strchr(l, '\n');
        if (nl != NULL) {
            *nl = 0;
            next_line = nl + 1;
        } else {
            next_line = NULL;
        }
        line++;
        char *c = strpbrk(l, "#;\r");
        if (c != NULL && (*c != '#' || c == l || isspace((unsigned char)c[-1]))) {
            *c = 0;
        }
        while (isspace((unsigned char)*l)) {
            l++;
        }
        size_t n = strlen(l);
        while (n > 0 && isspace((unsigned char)l[n - 1])) {
            l[--n] = 0;
        }
        if (n == 0) {
            continue;
        }
        if (l[0] == '[' && l[n - 1] == ']') {
            l[n - 1] = 0;
            if (num_versions >= MAX_VERSIONS) {
                fprintf(stderr, "WARNING: %s line %d: too many BASIC versions\n", path, line);
                break;
            }
            v = new_version(l + 1, l + 1);
            continue;
        }
        char *eq = strchr(l, '=');
        if (v == NULL || eq == NULL) {
            fprintf(stderr, "WARNING: %s line %d not understood\n", path, line);
            continue;
        }
        *eq = 0;
        char *key = l, *val = eq + 1;
        for (size_t k = strlen(key); k > 0 && isspace((unsigned char)key[k - 1]); k--) {
            key[k - 1] = 0;
        }
        while (isspace((unsigned char)*val)) {
            val++;
        }
        if (strcmp(key, "base") == 0) {
            basic_version *b = find_version(val);
            if (b == NULL || b == v) {
                fprintf(stderr, "WARNING: %s line %d: unknown base %s\n", path, line, val);
                continue;
            }
            char id[16];
            snprintf(id, sizeof id, "%s", v->id);
            *v = *b;
            snprintf(v->id, sizeof v->id, "%s", id);
            snprintf(v->description, sizeof v->description, "%s (on %s)", id, b->id);
        } else if (strcmp(key, "load") == 0) {
            extra_load[v - versions] = (unsigned int)strtoul(val, NULL, 16);
        } else {
            char *end;
            unsigned long t = strtoul(key, &end, 16);
            if (*end != 0 || t < 0x80 || t > 0xff || val[0] == 0) {
                fprintf(stderr, "WARNING: %s line %d: token must be 80-ff\n", path, line);
                continue;
            }
            for (char *p = val; *p; p++) {
                *p = (char)tolower((unsigned char)*p);
            }
            char *kwc = malloc(strlen(val) + 1);
            if (kwc == NULL) {
                fprintf(stderr, "ERROR: Memory allocation error\n");
                exit(-1);
            }
            strcpy(kwc, val);
            v->tok[t] = kwc;
        }
    }
    free(text);
    return 0;
}

/* ------------------------------------------------------------------ */
/* listing                                                             */

typedef struct {
    char *p;
    size_t len, cap;
} sbuf;

static void
sput(sbuf *b, const char *s)
{
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->cap + n + 1) * 2;
        char *p = realloc(b->p, b->cap);
        if (p == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        b->p = p;
    }
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

static void
sput_hex(sbuf *b, unsigned char c)
{
    char e[8];
    snprintf(e, sizeof e, "{$%02x}", c);
    sput(b, e);
}

/* text of a plain character: letters, digits, punctuation; NULL = none */
static const char *
plain_char(unsigned char c)
{
    static char s[256][2];
    if (c >= 0x20 && c <= 0x5f) {
        s[c][0] = (c >= 0x41 && c <= 0x5a) ? (char)(c + 0x20) : (char)c;   /* lower case */
        return s[c];
    }
    if (c >= 0xc1 && c <= 0xda) {
        s[c][0] = (char)(c - 0x80);                                       /* upper case */
        return s[c];
    }
    return NULL;
}

static const char *
code_name(const basic_version *v, unsigned char c)
{
    if (v->names35 && code_names_35[c] != NULL) {
        return code_names_35[c];
    }
    return code_names[c];
}

/* inside quotes */
static void
put_quoted(sbuf *b, const basic_version *v, unsigned char c)
{
    const char *n = code_name(v, c);
    if (c == 0xff) {
        sput(b, "~");
    } else if (n != NULL) {
        sput(b, "{");
        sput(b, n);
        sput(b, "}");
    } else if (c != 0xa0 && plain_char(c) != NULL) {
        sput(b, plain_char(c));
    } else {
        sput_hex(b, c);
    }
}

/* after REM and in DATA: plain characters up to $5F, control codes by
   name, everything else as {$xx} */
static void
put_raw(sbuf *b, const basic_version *v, unsigned char c)
{
    if (c >= 0x20 && c <= 0x5f) {
        sput(b, plain_char(c));
    } else if (c < 0x20 && code_name(v, c) != NULL) {
        sput(b, "{");
        sput(b, code_name(v, c));
        sput(b, "}");
    } else {
        sput_hex(b, c);
    }
}

/* the keyword that the text at s would be read as, NULL if none: the
   longest one (BASIC 7 reads DOPEN as one keyword, not DO and PEN) */
static const char *
keyword_at(const basic_version *v, const char *s, size_t avail)
{
    const char *best = NULL;
    size_t best_len = 0;
    for (int t = 0; t < 256; t++) {
        const char *k3[3] = { t >= 0x80 ? v->tok[t] : NULL, v->prefixed ? v->ce[t] : NULL,
                              v->prefixed ? v->fe[t] : NULL };
        for (int x = 0; x < 3; x++) {
            const char *k = k3[x];
            size_t l = k != NULL ? strlen(k) : 0;
            if (l > best_len && l <= avail && strncmp(s, k, l) == 0) {
                best = k;
                best_len = l;
            }
        }
    }
    return best;
}

/* would the plain bytes from body[i] on start a keyword when read back? */
static bool
starts_keyword(const basic_version *v, const unsigned char *body, int len, int i)
{
    char s[16];
    int n = 0;
    for (int k = i; k < len && n < (int)sizeof s - 1; k++) {
        if (body[k] >= 0x80 || body[k] == '"') {
            break;
        }
        const char *p = plain_char(body[k]);
        if (p == NULL) {
            break;
        }
        s[n++] = p[0];
    }
    s[n] = 0;
    return n > 0 && keyword_at(v, s, (size_t)n) != NULL;
}

/* one line (the bytes between line number and 0) */
static void
list_line(sbuf *b, const basic_version *v, const unsigned char *body, int len)
{
    bool quote = false, rem = false, data = false;
    int lead = 0;
    while (lead < len && body[lead] == ' ') {
        lead++;
    }
    int trail = len;
    while (trail > lead && body[trail - 1] == ' ') {
        trail--;
    }
    for (int i = 0; i < len; i++) {
        unsigned char c = body[i];
        if (c == ' ' && (i < lead || i >= trail)) {
            sput(b, "{$20}");                   /* would be lost when read back */
        } else if (rem) {
            put_raw(b, v, c);
        } else if (data) {
            /* raw up to the next colon outside quotes, also inside them */
            if (c == '"') {
                quote = !quote;
            } else if (c == ':' && !quote) {
                data = false;
            }
            put_raw(b, v, c);
        } else if (quote) {
            if (c == '"') {
                sput(b, "\"");
                quote = false;
            } else {
                put_quoted(b, v, c);
            }
        } else if (c == '"') {
            sput(b, "\"");
            quote = true;
        } else if (v->prefixed && (c == 0xce || c == 0xfe) && i + 1 < len
                   && (c == 0xce ? v->ce : v->fe)[body[i + 1]] != NULL) {
            sput(b, (c == 0xce ? v->ce : v->fe)[body[i + 1]]);
            i++;
        } else if (c >= 0x80) {
            if (v->tok[c] != NULL) {
                sput(b, v->tok[c]);
                rem = c == 0x8f;
                data = c == 0x83;
                quote = false;
            } else {
                sput_hex(b, c);
            }
        } else if (strchr("+-*/^<=>", c) != NULL && c != 0) {
            char e[4] = { '{', (char)c, '}', 0 };   /* not the operator token */
            sput(b, e);
        } else if (plain_char(c) != NULL && !starts_keyword(v, body, len, i)) {
            sput(b, plain_char(c));
        } else if (c < 0x20 && code_name(v, c) != NULL) {
            sput(b, "{");
            sput(b, code_name(v, c));
            sput(b, "}");
        } else {
            sput_hex(b, c);
        }
    }
}

/* walks the program; -1 if this is no BASIC program, else the number of
   bytes used (behind them may follow machine code) */
static int
walk(const unsigned char *d, int size, unsigned int la)
{
    unsigned int p = la;
    for (int lines = 0; lines < 65536; lines++) {
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
        for (int i = o + 4; i < next - 1; i++) {
            if (d[i] == 0) {
                return -1;
            }
        }
        p = link;
    }
    return -1;
}

/* the listing of a program (with load address) in the given version;
   NULL if it is no BASIC program. *used = bytes of the program */
char *
basic_list(const unsigned char *d, int size, const char *version, int *used)
{
    basic_version *v = find_version(version);
    if (v == NULL || size < 4) {
        return NULL;
    }
    unsigned int la = d[0] | (d[1] << 8);
    int end = walk(d, size, la);
    if (end < 0) {
        return NULL;
    }
    *used = end;
    sbuf b = { NULL, 0, 0 };
    sput(&b, "");
    unsigned int p = la;
    for (;;) {
        int o = (int)(p - la) + 2;
        unsigned int link = d[o] | (d[o + 1] << 8);
        if (link == 0) {
            break;
        }
        int next = (int)(link - la) + 2;
        char num[16];
        snprintf(num, sizeof num, "%5u ", d[o + 2] | (d[o + 3] << 8));
        sput(&b, num);
        list_line(&b, v, d + o + 4, next - 1 - (o + 4));
        sput(&b, "\n");
        p = link;
    }
    return b.p;
}

/* ------------------------------------------------------------------ */
/* back to a program                                                   */

/* {name}, {$xx}, {147}, {3 down}: the byte and the repeat count; 0 on error */
static int
parse_code(const basic_version *v, const char *s, size_t avail, int *byte, int *count)
{
    const char *close = memchr(s, '}', avail);
    if (close == NULL) {
        return 0;
    }
    int n = (int)(close - s - 1);
    char in[40];
    if (n <= 0 || n >= (int)sizeof in) {
        return 0;
    }
    memcpy(in, s + 1, (size_t)n);
    in[n] = 0;
    *count = 1;
    char *name = in;
    if (isdigit((unsigned char)in[0])) {
        char *end;
        long c = strtol(in, &end, 10);
        if (*end == ' ') {
            *count = (int)c;
            name = end + 1;
        } else if (*end == 0 && c >= 0 && c < 256) {
            *byte = (int)c;
            return n + 2;
        }
    }
    if (name[0] == '$' && isxdigit((unsigned char)name[1])) {
        char *end;
        long c = strtol(name + 1, &end, 16);
        if (*end == 0 && c >= 0 && c < 256) {
            *byte = (int)c;
            return n + 2;
        }
    }
    if (name[0] != 0 && name[1] == 0 && strchr("+-*/^<=>", name[0]) != NULL) {
        *byte = (unsigned char)name[0];
        return n + 2;
    }
    for (int c = 0; c < 256; c++) {
        const char *cn = code_name(v, (unsigned char)c);
        if (cn != NULL && strcmp(cn, name) == 0) {
            *byte = c;
            return n + 2;
        }
    }
    for (int c = 0; c < 256; c++) {                 /* the other names also */
        if (code_names[c] != NULL && strcmp(code_names[c], name) == 0) {
            *byte = c;
            return n + 2;
        }
    }
    return 0;
}

/* a plain character to PETSCII, -1 if none */
static int
char_byte(char c)
{
    if (c >= 'a' && c <= 'z') {
        return c - 0x20;
    }
    if (c >= 'A' && c <= 'Z') {
        return c + 0x80;
    }
    if (c == '~') {
        return 0xff;
    }
    if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x5f) {
        return (unsigned char)c;
    }
    return -1;
}

typedef struct {
    unsigned char *p;
    int len, cap;
} bbuf;

static void
bput(bbuf *b, int c)
{
    if (b->len + 1 > b->cap) {
        b->cap = (b->cap + 1) * 2;
        unsigned char *p = realloc(b->p, (size_t)b->cap);
        if (p == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        b->p = p;
    }
    b->p[b->len++] = (unsigned char)c;
}

/* tokenizes one line of text (without line number) into body */
static int
crunch_line(const basic_version *v, const char *s, size_t len, bbuf *body, const char *what, int line)
{
    bool quote = false, rem = false, data = false;
    size_t i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '{') {
            int byte, count;
            int used = parse_code(v, s + i, len - i, &byte, &count);
            if (used == 0 && memchr(s + i, '}', len - i) == NULL) {
                fprintf(stderr, "ERROR: %s line %d: { without }\n", what, line);
                return -1;
            }
            if (used == 0) {
                int show = 0;
                while (show < 12 && i + show < len && s[i + show] != '}') {
                    show++;
                }
                fprintf(stderr, "ERROR: %s line %d: unknown code %.*s}\n", what, line, show, s + i);
                return -1;
            }
            for (int k = 0; k < count; k++) {
                bput(body, byte);
            }
            i += (size_t)used;
            continue;
        }
        if (!quote && !rem && !data) {
            const char *k = keyword_at(v, s + i, len - i);
            if (k != NULL) {
                int t;
                for (t = 0x80; t <= 0xff && v->tok[t] != k; t++) {
                }
                if (t <= 0xff) {
                    bput(body, t);
                } else {
                    for (t = 0; t < 256 && v->ce[t] != k && v->fe[t] != k; t++) {
                    }
                    bput(body, v->ce[t] == k ? 0xce : 0xfe);
                    bput(body, t);
                }
                rem = (t == 0x8f);
                data = (t == 0x83);
                i += strlen(k);
                continue;
            }
        }
        int b = char_byte(c);
        if (b < 0) {
            fprintf(stderr, "ERROR: %s line %d: '%c' has no PETSCII character, write it as {$xx}\n", what,
                    line, c);
            return -1;
        }
        bput(body, b);
        if (c == '"' && !rem) {
            quote = !quote;
        } else if (c == ':' && data && !quote) {
            data = false;
        }
        i++;
    }
    return 0;
}

/* listing -> program with load address; 0 = ok, -1 = error (printed) */
int
basic_crunch(const char *text, size_t len, unsigned int la, const char *version, const char *what,
             unsigned char **data, int *size)
{
    basic_version *v = find_version(version);
    if (v == NULL) {
        fprintf(stderr, "ERROR: %s: unknown BASIC version %s\n", what, version);
        return -1;
    }
    bbuf out = { NULL, 0, 0 };
    bput(&out, (int)(la & 0xff));
    bput(&out, (int)(la >> 8));
    unsigned int addr = la;
    int line = 0;
    size_t i = 0;
    if (len >= 3 && memcmp(text, "\xef\xbb\xbf", 3) == 0) {
        i = 3;
    }
    long last_number = -1;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n' && text[e] != '\r') {
            e++;
        }
        line++;
        size_t p = i;
        while (p < e && (text[p] == ' ' || text[p] == '\t')) {
            p++;
        }
        if (p < e) {
            if (!isdigit((unsigned char)text[p])) {
                fprintf(stderr, "ERROR: %s line %d: no line number\n", what, line);
                free(out.p);
                return -1;
            }
            long number = 0;
            while (p < e && isdigit((unsigned char)text[p])) {
                number = number * 10 + (text[p] - '0');
                p++;
                if (number > 65535) {
                    break;
                }
            }
            if (number > 65535) {
                fprintf(stderr, "ERROR: %s line %d: line number above 65535\n", what, line);
                free(out.p);
                return -1;
            }
            if (number <= last_number) {
                fprintf(stderr, "WARNING: %s line %d: line number %ld not ascending\n", what, line, number);
            }
            last_number = number;
            while (p < e && text[p] == ' ') {
                p++;
            }
            bbuf body = { NULL, 0, 0 };
            if (crunch_line(v, text + p, e - p, &body, what, line) != 0) {
                free(body.p);
                free(out.p);
                return -1;
            }
            unsigned int next = addr + 4 + (unsigned int)body.len + 1;
            bput(&out, (int)(next & 0xff));
            bput(&out, (int)(next >> 8));
            bput(&out, (int)(number & 0xff));
            bput(&out, (int)(number >> 8));
            for (int k = 0; k < body.len; k++) {
                bput(&out, body.p[k]);
            }
            bput(&out, 0);
            free(body.p);
            addr = next;
        }
        i = e;
        if (i < len && text[i] == '\r') {
            i++;
        }
        if (i < len && text[i] == '\n') {
            i++;
        }
    }
    bput(&out, 0);
    bput(&out, 0);
    *data = out.p;
    *size = out.len;
    return 0;
}

/* versions to try for a load address, in order; the first is the default */
int
basic_versions_for(unsigned int la, const char **ids, int max)
{
    build_versions();
    int n = 0;
    static const struct {
        unsigned int la;
        const char *ids[4];
    } table[] = {
        { 0x0801, { "2", NULL } }, { 0x1201, { "2", NULL } }, { 0x1001, { "2", "3", NULL } },
        { 0x0401, { "40", "lre4", "lre2", NULL } }, { 0x1c01, { "70", "71", NULL } },
        { 0x4001, { "70", "71", NULL } },
    };
    for (unsigned int t = 0; t < sizeof table / sizeof table[0]; t++) {
        if (table[t].la == la) {
            for (int k = 0; k < 4 && table[t].ids[k] != NULL && n < max; k++) {
                ids[n++] = table[t].ids[k];
            }
        }
    }
    for (int i = 0; i < num_versions && n < max; i++) {
        if (extra_load[i] == la && extra_load[i] != 0) {
            ids[n++] = versions[i].id;
        }
    }
    return n;
}

/* does the version know every token of the program? */
bool
basic_knows_all(const unsigned char *d, int used, const char *version)
{
    basic_version *v = find_version(version);
    if (v == NULL) {
        return false;
    }
    unsigned int la = d[0] | (d[1] << 8);
    unsigned int p = la;
    for (;;) {
        int o = (int)(p - la) + 2;
        unsigned int link = d[o] | (d[o + 1] << 8);
        if (link == 0 || o + 4 > used) {
            return true;
        }
        int next = (int)(link - la) + 2;
        bool quote = false, rem = false;
        for (int i = o + 4; i < next - 1 && !rem; i++) {
            unsigned char c = d[i];
            if (c == '"') {
                quote = !quote;
            } else if (!quote && c >= 0x80) {
                if (v->prefixed && (c == 0xce || c == 0xfe)) {
                    if ((c == 0xce ? v->ce : v->fe)[d[i + 1]] == NULL) {
                        return false;
                    }
                    i++;
                } else if (v->tok[c] == NULL) {
                    return false;
                }
                rem = c == 0x8f;
                if (c == 0x83) {                /* DATA: up to the next colon */
                    while (i + 1 < next - 1 && d[i + 1] != ':') {
                        if (d[i + 1] == '"') {
                            for (i += 2; i < next - 1 && d[i] != '"'; i++) {
                            }
                        } else {
                            i++;
                        }
                    }
                }
            }
        }
        p = link;
    }
}

/* all versions: "id  description" lines */
void
basic_print_versions(void)
{
    build_versions();
    for (int i = 0; i < num_versions; i++) {
        printf("  %-6s %s\n", versions[i].id, versions[i].description);
    }
}

/* cit --basic ...: list a program, turn a listing into one, show versions

     cit --basic list prog.prg [version]
     cit --basic crunch prog.bas out.prg [load address in hex] [version]
     cit --basic versions
*/
int
basic_main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[2], "versions") == 0) {
        basic_print_versions();
        return 0;
    }
    if (argc >= 4 && strcmp(argv[2], "list") == 0) {
        unsigned char *d;
        long size;
        if (read_whole_file(argv[3], &d, &size) != 0 || size < 4) {
            fprintf(stderr, "ERROR: Could not read %s\n", argv[3]);
            return -1;
        }
        const char *ids[8];
        const char *version = argc >= 5 ? argv[4] : NULL;
        if (version == NULL) {
            int n = basic_versions_for(d[0] | (d[1] << 8), ids, 8);
            version = n > 0 ? ids[0] : "2";
            int used;
            char *probe = basic_list(d, (int)size, version, &used);
            for (int k = 0; probe != NULL && k < n; k++) {
                if (basic_knows_all(d, used, ids[k])) {
                    version = ids[k];
                    break;
                }
            }
            free(probe);
        }
        int used;
        char *text = basic_list(d, (int)size, version, &used);
        if (text == NULL) {
            fprintf(stderr, "ERROR: %s is no BASIC program\n", argv[3]);
            free(d);
            return -1;
        }
        fputs(text, stdout);
        if (used < size) {
            fprintf(stderr, "WARNING: %ld bytes behind the end of the program are not listed\n", size - used);
        }
        free(text);
        free(d);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[2], "crunch") == 0) {
        unsigned char *t;
        long len;
        if (read_whole_file(argv[3], &t, &len) != 0) {
            fprintf(stderr, "ERROR: Could not read %s\n", argv[3]);
            return -1;
        }
        unsigned int la = argc >= 6 ? (unsigned int)strtoul(argv[5], NULL, 16) : 0x0801;
        const char *ids[8];
        int n = basic_versions_for(la, ids, 8);
        const char *version = argc >= 7 ? argv[6] : (n > 0 ? ids[0] : "2");
        unsigned char *d;
        int size;
        int r = basic_crunch((const char *)t, (size_t)len, la, version, argv[3], &d, &size);
        free(t);
        if (r != 0) {
            return -1;
        }
        FILE *f = fopen(argv[4], "wb");
        if (f == NULL || fwrite(d, 1, (size_t)size, f) != (size_t)size || fclose(f) != 0) {
            fprintf(stderr, "ERROR: Could not write %s\n", argv[4]);
            free(d);
            return -1;
        }
        free(d);
        return 0;
    }
    fprintf(stderr, "Usage: cit --basic list prog.prg [version]\n"
                    "       cit --basic crunch prog.bas out.prg [load address hex] [version]\n"
                    "       cit --basic versions\n");
    return -1;
}
