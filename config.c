/*
 * config.c - cit.ini: a few settings that would otherwise have to be given
 * on every command line
 *
 * The first file found is used:
 *   1. --config file
 *   2. cit.ini next to the program (portable use, e.g. together with VICE)
 *   3. %APPDATA%\cit\cit.ini (Windows), $XDG_CONFIG_HOME/cit/cit.ini or
 *      ~/.config/cit/cit.ini (Linux, macOS)
 *
 * Format: "key = value" per line, comments start with ; or #. The command
 * line always wins over the file, the file over the built-in defaults.
 *
 *   basic.tokens   = file with more BASIC versions (see token.c)
 *   7z             = path of 7-Zip (7z, 7za, 7zz), for .zip/.7z archives (planned)
 *   osfmount       = path of OSFMount.com, for --sd-mount (Windows);
 *                    default C:\Program Files\OSFMount\OSFMount.com, else PATH
 *   sd.letter      = drive letter for --sd-mount (default S)
 *   text.eol       = crlf | lf | cr        line end of text from -XA (default: system)
 *   text.charset   = lower | upper         character set of text (default lower)
 *   csv.sep        = ; | , | tab           CSV separator (default ;)
 *   seq.text       = percent               SEQ is text from this share (default 90)
 *   seq.mixed      = percent               text with a note from this share (default 75)
 *   seq.00         = eol | text            $00 in SEQ: line end or this text
 *   rel.00         = sep | text            $00 in REL: field separator or this text
 *   alloc.new      = cc1541 | dos | auto   strategy for a new image
 *   alloc.existing = cc1541 | dos | auto   strategy when adding files
 *   disk.name      = disk name without -n
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

cit_config config = { .petcat = NULL, .sevenzip = NULL, .osfmount = NULL, .sd_letter = 0, .text_eol = -1, .text_upper = -1, .csv_sep = 0, .seq_text = -1, .seq_mixed = -1, .seq00 = NULL, .rel00 = NULL, .alloc_new = -1, .alloc_existing = -1, .disk_name = NULL, .path = NULL };

static char *
copy_string(const char *s)
{
    char *c = malloc(strlen(s) + 1);
    if (c == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    strcpy(c, s);
    return c;
}

static int
alloc_from_name(const char *v)
{
    if (strcasecmp(v, "cc1541") == 0) {
        return ALLOC_CC1541;
    } else if (strcasecmp(v, "dos") == 0) {
        return ALLOC_DOS;
    } else if (strcasecmp(v, "auto") == 0) {
        return ALLOC_AUTO;
    }
    return -1;
}

static bool
read_config(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    config.path = copy_string(path);
    char line[1024];
    int number = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        number++;
        /* comments: a line starting with ; or #, or ; or # after a blank */
        for (char *c = line; *c; c++) {
            if ((*c == ';' || *c == '#') && (c == line || isspace((unsigned char)c[-1]))) {
                *c = 0;
                break;
            }
        }
        char *eq = strchr(line, '=');
        char *key = line;
        while (isspace((unsigned char)*key)) {
            key++;
        }
        if (*key == 0) {
            continue;
        }
        if (eq == NULL) {
            fprintf(stderr, "WARNING: %s line %d: no \"key = value\"\n", path, number);
            continue;
        }
        *eq = 0;
        char *value = eq + 1;
        for (char *e = eq - 1; e >= key && isspace((unsigned char)*e); e--) {
            *e = 0;
        }
        while (isspace((unsigned char)*value)) {
            value++;
        }
        for (char *e = value + strlen(value) - 1; e >= value && isspace((unsigned char)*e); e--) {
            *e = 0;
        }
        if (strcasecmp(key, "petcat") == 0) {
            config.petcat = copy_string(value);     /* no longer needed, -XA lists BASIC itself */
        } else if (strcasecmp(key, "basic.tokens") == 0) {
            load_token_file(value);
        } else if (strcasecmp(key, "7z") == 0) {
            config.sevenzip = copy_string(value);
        } else if (strcasecmp(key, "osfmount") == 0) {
            config.osfmount = copy_string(value);
        } else if (strcasecmp(key, "text.eol") == 0) {
            int e = eol_from_name(value);
            if (e < 0) {
                fprintf(stderr, "WARNING: %s line %d: text.eol must be crlf, lf or cr\n", path, number);
            } else {
                config.text_eol = e;
            }
        } else if (strcasecmp(key, "text.charset") == 0) {
            if (strcasecmp(value, "lower") == 0 || strcasecmp(value, "upper") == 0) {
                config.text_upper = strcasecmp(value, "upper") == 0;
            } else {
                fprintf(stderr, "WARNING: %s line %d: text.charset must be lower or upper\n", path, number);
            }
        } else if (strcasecmp(key, "csv.sep") == 0) {
            char c = sep_from_name(value);
            if (c == 0) {
                fprintf(stderr, "WARNING: %s line %d: csv.sep must be one character or tab\n", path, number);
            } else {
                config.csv_sep = c;
            }
        } else if (strcasecmp(key, "seq.text") == 0 || strcasecmp(key, "seq.mixed") == 0) {
            char *end;
            long v = strtol(value, &end, 10);
            if (end == value || *end != 0 || v < 0 || v > 100) {
                fprintf(stderr, "WARNING: %s line %d: %s must be a percentage 0-100\n", path, number, key);
            } else if (strcasecmp(key, "seq.text") == 0) {
                config.seq_text = (int)v;
            } else {
                config.seq_mixed = (int)v;
            }
        } else if (strcasecmp(key, "seq.00") == 0) {
            config.seq00 = copy_string(value);
        } else if (strcasecmp(key, "rel.00") == 0) {
            config.rel00 = copy_string(value);
        } else if (strcasecmp(key, "sd.letter") == 0) {
            char c = (char)toupper((unsigned char)value[0]);
            if (c >= 'A' && c <= 'Z' && (value[1] == 0 || (value[1] == ':' && value[2] == 0))) {
                config.sd_letter = c;
            } else {
                fprintf(stderr, "WARNING: %s line %d: sd.letter must be a drive letter like S\n", path, number);
            }
        } else if (strcasecmp(key, "alloc.new") == 0 || strcasecmp(key, "alloc.existing") == 0) {
            int a = alloc_from_name(value);
            if (a < 0) {
                fprintf(stderr, "WARNING: %s line %d: unknown strategy \"%s\"\n", path, number, value);
            } else if (strcasecmp(key, "alloc.new") == 0) {
                config.alloc_new = a;
            } else {
                config.alloc_existing = a;
            }
        } else if (strcasecmp(key, "disk.name") == 0) {
            config.disk_name = copy_string(value);
        } else {
            fprintf(stderr, "WARNING: %s line %d: unknown key \"%s\"\n", path, number, key);
        }
    }
    fclose(f);
    return true;
}

/* program: argv[0]; explicit: --config file or NULL */
void
load_config(const char *program, const char *explicit_path)
{
    if (explicit_path != NULL) {
        if (!read_config(explicit_path)) {
            fprintf(stderr, "ERROR: Could not read config file %s\n", explicit_path);
            exit(-1);
        }
        return;
    }
    /* next to the program */
    const char *slash = strrchr(program, '/');
#ifdef _WIN32
    const char *backslash = strrchr(program, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    if (slash != NULL) {
        size_t len = (size_t)(slash - program) + 1;
        char *path = malloc(len + 8);
        if (path == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        memcpy(path, program, len);
        strcpy(path + len, "cit.ini");
        bool found = read_config(path);
        free(path);
        if (found) {
            return;
        }
    }
    /* the user's configuration directory */
    char path[1024];
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata != NULL) {
        snprintf(path, sizeof path, "%s\\cit\\cit.ini", appdata);
        read_config(path);
    }
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg != NULL && *xdg) {
        snprintf(path, sizeof path, "%s/cit/cit.ini", xdg);
    } else if (home != NULL) {
        snprintf(path, sizeof path, "%s/.config/cit/cit.ini", home);
    } else {
        return;
    }
    read_config(path);
#endif
}

/* OSFMount.com: from cit.ini, else where its installer puts it, else
   searched in PATH */
const char *
osfmount_path(void)
{
    if (config.osfmount != NULL) {
        return config.osfmount;
    }
#ifdef _WIN32
    static const char *installed = "C:\\Program Files\\OSFMount\\OSFMount.com";
    FILE *f = fopen(installed, "rb");
    if (f != NULL) {
        fclose(f);
        return installed;
    }
#endif
    return "OSFMount.com";
}

/* "crlf", "lf", "cr" -> EOL_*, -1 if unknown */
int
eol_from_name(const char *s)
{
    return strcasecmp(s, "crlf") == 0 ? EOL_CRLF : strcasecmp(s, "lf") == 0 ? EOL_LF
           : strcasecmp(s, "cr") == 0 ? EOL_CR : -1;
}

/* CSV separator: one character or "tab", 0 if invalid */
char
sep_from_name(const char *s)
{
    if (strcasecmp(s, "tab") == 0) {
        return '\t';
    }
    if (s[0] != 0 && s[1] == 0 && s[0] != '"' && s[0] != '{' && s[0] != '\r' && s[0] != '\n') {
        return s[0];
    }
    return 0;
}
