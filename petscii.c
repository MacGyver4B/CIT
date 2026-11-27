/*******************************************************************************
* Copyright (c) 2008-2024 JackAsser, Krill, Claus, Björn Esser
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*******************************************************************************/

/* petscii.c: PETSCII and console output */

#include "cit.h"

/* Table for conversion of uppercase PETSCII to Unicode */
static unsigned int p2u_uppercase_tab[256] = {
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    ' ', '!', '\"', '#', '$', '%', '&', 0x2019, '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?',
    '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', 0xa3, ']', 0x2191, 0x2190,
    0x1fb79, 0x2660, 0x1fb72, 0x1fb78, 0x1fb77, 0x1fb76, 0x1fb7a, 0x1fb71, 0x1fb74, 0x256e, 0x2570, 0x256f, 0x1fb7c, 0x2572, 0x2571, 0x1fb7d,
    0x1fb7e, 0x25cf, 0x1fb7b, 0x2665, 0x1fb70, 0x256d, 0x2573, 0x25cb, 0x2663, 0x1fb75, 0x2666, 0x253c, 0x1fb8c, 0x2502, 0x3c0, 0x25e5,
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    ' ', 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x1fb95, 0x1fb75, 0x1fb8f, 0x25e4, 0x1fb75, 0x251c, 0x2597, 0x2514, 0x2510, 0x2581,
    0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0x2590, 0x2594, 0x2580, 0x2583, 0x1fb7f, 0x2596, 0x259d, 0x2518, 0x2598, 0x259a,
    0x2500, 0x2660, 0x1fb72, 0x2500, 0x1fb77, 0x1fb76, 0x1fb7a, 0x1fb71, 0x1fb74, 0x256e, 0x2570, 0x256f, 0x1fb7c, 0x2572, 0x2571, 0x1fb7d,
    0x1fb7e, 0x25cf, 0x1fb7b, 0x2665, 0x1fb70, 0x256d, 0x2573, 0x25cb, 0x2663, 0x1fb75, 0x2666, 0x253c, 0x1fb8c, 0x2502, 0x3c0, 0x25e5,
    ' ', 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x1fb95, 0x1fb75, 0x1fb8f, 0x25e4, 0x1fb75, 0x251c, 0x2597, 0x2514, 0x2510, 0x2581,
    0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0x1fb88, 0x1fb82, 0x1fb83, 0x2583, 0x1fb7f, 0x2596, 0x259d, 0x2518, 0x2598, 0x3c0
};

/* Table for conversion of lowercase PETSCII to Unicode */
static unsigned int p2u_lowercase_tab[256] = {
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    ' ', '!', '\"', '#', '$', '%', '&', 0x2019, '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':', ';', '<', '=', '>', '?',
    '@', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '[', 0xa3, ']', 0x2191, 0x2190,
    0x1fb79, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0x253c, 0x1fb8c, 0x2502, 0x1fb95, 0x1fb98,
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.', '.',
    ' ', 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x1fb95, 0x1fb75, 0x1fb8f, 0x1fb99, 0x1fb75, 0x251c, 0x2597, 0x2514, 0x2510, 0x2581,
    0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0x2590, 0x2594, 0x2580, 0x2583, 0x1fb7f, 0x2713, 0x259d, 0x2518, 0x2598, 0x259a,
    0x2500, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0x253c, 0x1fb8c, 0x2502, 0x1fb95, 0x1fb98,
    ' ', 0x258c, 0x2584, 0x2594, 0x2581, 0x258e, 0x1fb95, 0x1fb75, 0x1fb8f, 0x1fb99, 0x1fb75, 0x251c, 0x2597, 0x2514, 0x2510, 0x2581,
    0x250c, 0x2534, 0x252c, 0x2524, 0x258e, 0x258d, 0x1fb88, 0x1fb82, 0x1fb83, 0x2583, 0x2713, 0x2596, 0x259d, 0x2518, 0x2598, 0x1fb96
};

/* Converts an ASCII character to PETSCII */
unsigned char
a2p(unsigned char a)
{
    switch (a) {
    case '\n':
        return 0x0d;
    case '_':
        return 0xa4;
    case 0x7e:
        return 0xff;
    default:
        if ((a >= 0x5b) && (a < 0x5f)) {
            return a;
        }
        if ((a >= 0x60) && (a <= 0x7e)) {
            return a ^ 0x20;
        }
        if ((a >= 'A') && (a <= 'Z')) {
            return a | 0x80;
        }
        return a;
    }
}

/* Converts a PETSCII character to ASCII */
static unsigned char
p2a(unsigned char p)
{
    switch (p) {
    case 0x0a:
    case 0x0d:
        return '\n';
    case 0x40:
    case 0x60:
        return p;
    case 0xa0:
    case 0xe0:
        return ' ';
    case 0xa4:
    case 0xe4:
        return '_';
    default:
        switch (p & 0xe0) {
        case 0x40:
        case 0x60:
            p ^= 0x20;
            break;
        case 0xc0:
            p ^= 0x80;
            break;
        }
    }
    return ((isprint(p) ? p : '.'));
}

/* Converts a PETSCII character to ASCII if possible, returns negative PETSCII code otherwise */
static int
p2a_escape(unsigned char p)
{
    if((p >= 32 && p <= 122) && (p != 33) && (p != 34) && (p != 35) && (p != 36) && (p != 92) && (p != 94) && (p != 95) && (p != 96)) {
        switch (p & 0xe0) {
        case 0x40:
        case 0x60:
            return p ^ 0x20;
            break;
        default:
            return p;
        }
    } else {
        return -p;
    }
}

/* Converts a PETSCII string to ASCII with hex escapes where needed, esacpe slash if slash false */
char*
petscii2ascii(const unsigned char* petscii, char* ascii, int len, bool slash)
{
    /* find end of petscii string */
    while(len > 1 && petscii[len-1] == 0xa0) {
        len--;
    }
    int apos = 0;
    for(int ppos = 0; ppos < len; ppos++) {
        int a = p2a_escape(petscii[ppos]);
        if(!slash && (a == 47)) {
            a = -a; /* force slash to be escaped */
        }
        if(a < 0) {
            snprintf(ascii+apos, 4, "#%02x", -a);
            apos += 3;
        } else {
            ascii[apos++] = a;
        }
    }
    ascii[apos] = 0;
    return ascii;
}

/* Converts an ASCII string to PETSCII filled up with Shift-Space to length */
void
ascii2petscii(const unsigned char* ascii, unsigned char* petscii, int len)
{
    int pos = 0;
    while (ascii[pos] != '\0' && pos < len) {
        petscii[pos] = a2p(ascii[pos]);
        ++pos;
    }
    while (pos < len) {
        petscii[pos] = FILENAMEEMPTYCHAR;
        ++pos;
    }
}

/* Prints a PETSCII filename as ASCII with escapes */
void
print_filename_with_escapes(const unsigned char* petscii, int len)
{
    char ascii[3 * FILENAMEMAXSIZE + 1];
    fputs(petscii2ascii(petscii, ascii, len, true), stdout);
}

/* Determines length of 0xa0 terminated PETSCII string */
int
pstrlen(const unsigned char* string)
{
    int len = 0;
    while(string[len] != 0xa0 && len < FILENAMEMAXSIZE) {
        len++;
    }
    return len;
}

/* Writes decimal number into PETSCII string */
#define PPUTNUM_BUF_LEN 17
void
pputnum(unsigned char* string, unsigned int num)
{
    char buffer[PPUTNUM_BUF_LEN];
    snprintf(buffer, PPUTNUM_BUF_LEN, "%d", num);
    for(unsigned int len = 0; len < strlen(buffer); len++) {
        string[len] = a2p(buffer[len]);
    }
}

/* Writes 2 digit decimal number into PETSCII string */
#define PPUTNUM2_BUF_LEN 3
void
pputnum2(unsigned char* string, unsigned int num)
{
    char buffer[PPUTNUM2_BUF_LEN];
    snprintf(buffer, PPUTNUM2_BUF_LEN, "%02d", num);
    for(unsigned int len = 0; len < strlen(buffer); len++) {
        string[len] = a2p(buffer[len]);
    }
}

/* Writes 4 digit hex number into PETSCII string */
#define PPUTHEX_BUF_LEN 5
void
pputhex(unsigned char* string, unsigned int num)
{
    char buffer[PPUTHEX_BUF_LEN];
    snprintf(buffer, PPUTHEX_BUF_LEN, "%04x", num);
    for(int len = 0; len < 4; len++) {
        string[len] = a2p(buffer[len]);
    }
}

/* Converts a two digit hex string to an int */
static unsigned int
hex2int(char hex)
{
    if ((hex < '0' || hex > '9') && (hex < 'a' || hex > 'f') && (hex < 'A' || hex > 'F')) {
        fprintf(stderr, "ERROR: Invalid hex string in filename\n");
        exit(-1);
    }
    if (hex <= '9') {
        hex -= '0';
    } else if(hex >= 'A' && hex <= 'F') {
        hex -= 'A' - 10;
    } else if(hex >= 'a' && hex <= 'f') {
        hex -= 'a' - 10;
    }
    return (unsigned int)hex;
}

/* Converts an ASCII string to PETSCII with escape evaluation, filled up with emptychar to length */
void
evalhexescape(const unsigned char* ascii, unsigned char* petscii, int len, unsigned char emptychar)
{
    int read = 0, write = 0;

    while (ascii[read] != '\0' && write < len) {
        if (ascii[read] == '#') {
            unsigned int hi = hex2int(ascii[++read]);
            unsigned int lo = hex2int(ascii[++read]);
            petscii[write] = (unsigned char)(16 * hi + lo);
        } else {
            petscii[write] = a2p(ascii[read]);
        }
        read++;
        write++;
    }
    while (write < len) {
        petscii[write] = emptychar;
        ++write;
    }
}

/* Converts a unicode character to utf8 */
char* utf8_encode(int c, char* out)
{
    if (c < 0x80)
        *out++ =(c & 0xff);
    else if (c < 0x800) {
        *out++ = (0xC0 | ((c >> 6) & 0x1f));
        *out++ = (0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        *out++ = (0xE0 | ((c >> 12) & 0xf));
        *out++ = (0x80 | ((c >> 6) & 0x3f));
        *out++ = (0x80 | (c & 0x3f));
    } else {
        *out++ = (0xF0 | ((c >> 18) & 0x07));
        *out++ = (0x80 | ((c >> 12) & 0x3f));
        *out++ = (0x80 | ((c >> 6) & 0x3f));
        *out++ = (0x80 | (c & 0x3f));
    }
    return out;
}

#ifdef _WIN32
/* Enables console formatting under Windows if possible */
static bool
EnableVTMode()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        return false;
    }
    dwMode |= 0x0004; /* ENABLE_VIRTUAL_TERMINAL_PROCESSING not defined for older SDKs */
    if (!SetConsoleMode(hOut, dwMode)) {
        return false;
    }
    return true;
}
#endif

/* Enables reverse printing to console */
void
reverse_print_on(void)
{
#ifdef _WIN32
    /* Avoid escape values for inverse printing under Windows if they are not supported by the console */
    if (EnableVTMode()) {
        printf("\033[7m");
    }
#else
    printf("\033[7m");
#endif
}

/* Disables reverse printing to console */
void
reverse_print_off(void)
{
#ifdef _WIN32
    /* Avoid escape values for inverse printing under Windows if they are not supported by the console */
    if (EnableVTMode()) {
        printf("\033[m");
    }
#else
    printf("\033[m");
#endif
}

/* Prints a PETSCII character */
void
putp(unsigned char petscii, FILE *file)
{
    if (unicode) {
        int u;
        int reverse = 0;

        if (petscii < 0x20 || (petscii >= 0x80 && petscii <= 0x9f)) {
            reverse = 1;
            reverse_print_on();
            petscii += 0x40;
        }

        if (unicode == 1) {
            u = p2u_uppercase_tab[petscii];
        } else {
            u = p2u_lowercase_tab[petscii];
        }

#ifdef _WIN32
        _setmode(_fileno(file), _O_U16TEXT);
        putwc(u, file);
        _setmode(_fileno(file), _O_TEXT);
#else
        {
            char temp[5];
            *utf8_encode(u, temp) = 0; /* fancy way to zero-terminate temp after conversion */
            fputs(temp, file);
        }
#endif

        if(reverse) {
            reverse_print_off();
        }
    } else {
        putc(p2a(petscii), file);
    }
}

/* Prints a PETSCII string */
void
print_petscii(unsigned char *petscii, int len)
{
    for(int i = 0; i < len; i++) {
        putp(petscii[i], stdout);
    }
}

/* Prints the given PETSCII filename like the C64 when listing the directory */
void
print_dirfilename(unsigned char* pfilename)
{
    int ended = 0;
    putc('\"', stdout);
    for (int pos = 0; pos < FILENAMEMAXSIZE; pos++) {
        unsigned char c = pfilename[pos];
        if (c == FILENAMEEMPTYCHAR) {
            if (!ended) {
                putc('\"', stdout);
                ended = 1;
            } else {
                putc(' ', stdout);
            }
        } else {
            putp(c, stdout);
        }
    }
    if (!ended) {
        putc('\"', stdout);
    } else {
        putc(' ', stdout);
    }
}

/* Prints the given PETSCII filename */
void
print_filename(FILE *file, unsigned char* pfilename)
{
    putc('\"', file);
    for (int pos = 0; pos < FILENAMEMAXSIZE; pos++) {
        if (pfilename[pos] == FILENAMEEMPTYCHAR) {
            break;
        }
        putp(pfilename[pos], file);
    }
    putc('\"', file);
}
