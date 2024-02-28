/*******************************************************************************
* Copyright (c) 2018-2024 Claus, Björn Esser
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

#define _CRT_SECURE_NO_WARNINGS /* avoid security warnings for MSVC */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>

#define CMD_LINE_LEN  4096 + 1 /* 4k is enough for commandline buffer. */

#define MERGE_OUT_ERR " 2>&1"
#ifdef _WIN32
#define FILESEPARATOR "\\"
#define NULL_DEV      "> nul"
#else
#define FILESEPARATOR "/"
#define NULL_DEV      "> /dev/null"
#endif

enum {
    NO_ERROR = 0,
    ERROR_ALLOCATION,
    ERROR_RETURN_VALUE,
    ERROR_NO_OUTPUT
};

const unsigned int track_offset[] = { /* taken from http://unusedino.de/ec64/technical/formats/d64.html */
    0x00000, 0x01500, 0x02A00, 0x03F00, 0x05400, 0x06900, 0x07E00, 0x09300,
    0x0A800, 0x0BD00, 0x0D200, 0x0E700, 0x0FC00, 0x11100, 0x12600, 0x13B00,
    0x15000, 0x16500, 0x17800, 0x18B00, 0x19E00, 0x1B100, 0x1C400, 0x1D700,
    0x1EA00, 0x1FC00, 0x20E00, 0x22000, 0x23200, 0x24400, 0x25600, 0x26700,
    0x27800, 0x28900, 0x29A00, 0x2AB00, 0x2BC00, 0x2CD00, 0x2DE00, 0x2EF00
};

const unsigned int track_offset_b[] = {
    /* second side of D71 */
    0x2AB00 + 0x00000, 0x2AB00 + 0x01500, 0x2AB00 + 0x02A00, 0x2AB00 + 0x03F00, 0x2AB00 + 0x05400, 0x2AB00 + 0x06900, 0x2AB00 + 0x07E00, 0x2AB00 + 0x09300,
    0x2AB00 + 0x0A800, 0x2AB00 + 0x0BD00, 0x2AB00 + 0x0D200, 0x2AB00 + 0x0E700, 0x2AB00 + 0x0FC00, 0x2AB00 + 0x11100, 0x2AB00 + 0x12600, 0x2AB00 + 0x13B00,
    0x2AB00 + 0x15000, 0x2AB00 + 0x16500, 0x2AB00 + 0x17800, 0x2AB00 + 0x18B00, 0x2AB00 + 0x19E00, 0x2AB00 + 0x1B100, 0x2AB00 + 0x1C400, 0x2AB00 + 0x1D700,
    0x2AB00 + 0x1EA00, 0x2AB00 + 0x1FC00, 0x2AB00 + 0x20E00, 0x2AB00 + 0x22000, 0x2AB00 + 0x23200, 0x2AB00 + 0x24400, 0x2AB00 + 0x25600, 0x2AB00 + 0x26700,
    0x2AB00 + 0x27800, 0x2AB00 + 0x28900, 0x2AB00 + 0x29A00, 0x2AB00 + 0x2AB00, 0x2AB00 + 0x2BC00, 0x2AB00 + 0x2CD00, 0x2AB00 + 0x2DE00, 0x2AB00 + 0x2EF00
};

/* Runs the binary with the provided commandline and returns the content of the output image file in a buffer */
int
run_binary(const char* binary, const char* options, const char* image_name, char **image, size_t *size, bool silent)
{
    struct stat st;
    static char command_line[CMD_LINE_LEN];

    if (*image != NULL) {
        free(*image);
        *image = NULL;
    }

    /* build command line */
    if(silent) {
        snprintf(command_line, CMD_LINE_LEN, "%s %s %s %s %s", binary, options, image_name, NULL_DEV, MERGE_OUT_ERR);
    } else {
        snprintf(command_line, CMD_LINE_LEN, "%s %s %s %s", binary, options, image_name, NULL_DEV);
    }

    if (system(command_line) != 0) {
        return ERROR_RETURN_VALUE;
    }

    if (stat(image_name, &st)) {
        return ERROR_NO_OUTPUT;
    }

    *size = st.st_size;
    *image = calloc(st.st_size, sizeof(unsigned char));

    FILE* f = fopen(image_name, "rb");
    if (f == NULL) {
        return ERROR_NO_OUTPUT;
    }
    if (fread(*image, *size, 1, f) != 1) {
        fprintf(stderr, "ERROR: Unexpected filesize when reading %s\n", image_name);
        return ERROR_NO_OUTPUT;
    }
    fclose(f);

    return NO_ERROR;
}

/* runs the binary with a given command line and image output file, reads the output into a buffer and deletes the file then */
int
run_binary_cleanup(const char* binary, const char* options, const char* image_name, char **image, size_t *size, bool silent)
{
    int status = run_binary(binary, options, image_name, image, size, silent);
    remove(image_name);
    return status;
}

/* writes image to file */
int
write_file(const char* name, size_t size, char *image)
{
    FILE* f = fopen(name, "wb");
    if(f == NULL) {
        fprintf(stderr, "ERROR: Could not create output file %s\n", name);
        return ERROR_NO_OUTPUT;
    }
    for (size_t i = 0; i < size; i++) {
        fputc(image[i], f);
    }
    fclose(f);
    return NO_ERROR;
}

/* creates a file with given name, size and filled with given value */
int
create_value_file(const char* name, int size, char value)
{
    FILE* f = fopen(name, "wb");
    if (f == NULL) {
        fprintf(stderr, "ERROR: Could not create output file %s\n", name);
        return ERROR_NO_OUTPUT;
    }
    for (int i = 0; i < size; i++) {
        fputc(value, f);
    }
    fclose(f);
    return NO_ERROR;
}

/* checks if a given block in the image is filled with a given value */
int
block_is_filled(char* image, int block, int value)
{
    for (int i = 0; i < 254; i++) {
        if (image[block * 256 + 2 + i] != value) {
            return 0;
        }
    }
    return 1;
}

int
main(int argc, char* argv[])
{
    struct stat st;
    const char* binary;
    char *image = NULL;
    size_t size;
    int test = 0;
    int passed = 0;
    char *description;
    int result = 0;

    enum {
        TEST_PASS = 0,
        TEST_FAIL = 1,
        TEST_UNRESOLVED = 2
    };
    const char *const result_str[] = {
        "PASS",
        "FAIL",
        "UNRESOLVED"
    };
    const int test_pad = 3; /* Decimal digits of the test counter */

    if (argc != 2) {
        printf("Test suite for cc1541\n");
        printf("Usage: test_cc1541 <path to cc1541 binary>\n");
        return(-1);
    }
    if (stat(argv[1], &st)) {
        printf("ERROR: Test binary %s does not exist.\n", argv[1]);
        return(-1);
    }

    binary = argv[1];

    description = "Size of empty D64 image should be 174848";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 174848) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Size of empty G64 image should be 269862";
    ++test;
    if (run_binary_cleanup(binary, "-g image.g64", "image.g64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 269862) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Size of empty D71 image should be 2*174848";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 2 * 174848) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Size of empty Speed DOS D64 image should be 174848+5*17*256";
    ++test;
    if (run_binary_cleanup(binary, "-4", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 174848 + 5 * 17 * 256) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Size of empty Dolphin DOS D64 image should be 174848+5*17*256";
    ++test;
    if (run_binary_cleanup(binary, "-5", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 174848 + 5 * 17 * 256) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Size of empty D81 image should be 80*40*256";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (size == 80 * 40 * 256) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Writing file with one block should fill track 1 sector 0";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 0, 37) && image[0 * 256] == 0 && image[0 * 256 + 1] == (char)255) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Diskname should be found in track 18 sector 0 offset $90";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-n 0123456789abcdef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 0x90], "0123456789ABCDEF", 16) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Diskname should be found in track 40 sector 0 offset 4 for d81";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-n 0123456789abcdef -w 1.prg", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[40*39*256 + 4], "0123456789ABCDEF", 16) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Diskname should be truncated to 16 characters";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-n 0123456789abcdef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 0x90], "0123456789ABCDEF", 16) == 0 && image[track_offset[17] + 0xa0] == (char)0xa0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Diskname hex escape should be evaluated correctly";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-n 0123456789abcde#ef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 0x90 + 15] == (char)0xef) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Disk ID should be found in track 18 sector 0 offset $a2";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-i 01234 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 0xa2], "01234", 5) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Disk ID should be found in track 40 sector 0 offset 0x16 for d81";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-i 01234 -w 1.prg", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[40 * 39 * 256 + 0x16], "01234", 5) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Disk ID should be truncated to 5 characters";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-i 0123456789ABCDEFGHI -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 0xa2], "01234", 5) == 0 && image[track_offset[17] + 0xa7] == (char)0xa0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Disk ID hex escape should be evaluated correctly";
    ++test;
    create_value_file("1.prg", 254, 37);
    if (run_binary_cleanup(binary, "-i 0123#ef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 0xa2 + 4] == (char)0xef) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Setting minimum sector to 7 should fill track 1 sector 7";
    ++test;
    create_value_file("1.prg", 254 * 1, 1);
    if (run_binary_cleanup(binary, "-F 7 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 7, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Setting minimum sector to 7 for second track should fill track 2 sector 7";
    ++test;
    create_value_file("1.prg", 254 * 21, 1);
    create_value_file("2.prg", 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -F 7 -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 21 + 7, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Setting minimum sector to 7 should fill track 2 sector 7 for longer file";
    ++test;
    create_value_file("1.prg", 254 * 22, 1);
    if (run_binary_cleanup(binary, "-F 7 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 21 + 7, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Setting minimum sector to negative for second track should fill track 2 sector 10";
    ++test;
    create_value_file("1.prg", 254 * 22, 1);
    if (run_binary_cleanup(binary, "-F -3 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 21 + 4, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Minimum sector should fall back to 0 after track change";
    ++test;
    create_value_file("1.prg", 254 * 21, 1);
    create_value_file("2.prg", 254, 2);
    if (run_binary_cleanup(binary, "-F 7 -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 21 + 0, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File with default sector interleave 7 should fill sector 0 and 7 on track 1";
    ++test;
    create_value_file("1.prg", 254 * 2, 37);
    if (run_binary_cleanup(binary, "-S 7 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 0, 37) && block_is_filled(image, 7, 37)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File with sector interleave 9 should fill sector 0 and 9 on track 1";
    ++test;
    create_value_file("1.prg", 254 * 2, 37);
    if (run_binary_cleanup(binary, "-s 9 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 0, 37) && block_is_filled(image, 9, 37)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File with sector interleave 20 should fill sector 3 and 1 on track 1";
    ++test;
    create_value_file("1.prg", 254 * 2, 37);
    if (run_binary_cleanup(binary, "-F 3 -s 20 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 3, 37) && block_is_filled(image, 2, 37)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Sector interleave should go back to default for next file";
    ++test;
    create_value_file("1.prg", 254 * 2, 1);
    create_value_file("2.prg", 254 * 2, 2);
    if (run_binary_cleanup(binary, "-S 3 -s 2 -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 4, 2) && block_is_filled(image, 7, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Filename should be found at track 18 sector 1 offset 5";
    ++test;
    create_value_file("1.prg", 254 * 2, 1);
    if (run_binary_cleanup(binary, "-f 0123456789abcdef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 256 + 5], "0123456789ABCDEF", 16) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Filename hexvalue should be interpreted correctly";
    ++test;
    create_value_file("1.prg", 254 * 2, 1);
    if (run_binary_cleanup(binary, "-f 0123456789ABCDE#ef -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 5 + 15] == (char)0xef) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Filename capital hexvalue should be interpreted correctly";
    ++test;
    create_value_file("1.prg", 254 * 2, 1);
    if (run_binary_cleanup(binary, "-f 0123456789ABCDE#EF -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 5 + 15] == (char)0xef) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Input path should be stripped of folders for filename";
    ++test;
    create_value_file(".." FILESEPARATOR "1.prg", 254 * 2, 1);
    if (run_binary_cleanup(binary, "-w .." FILESEPARATOR "1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + 256 + 5], "1.PRG", 5) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("../1.prg");

    description = "Second file should start on track 2 sector 10 for -e";
    ++test;
    create_value_file("1.prg", 254, 1);
    create_value_file("2.prg", 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -e -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 0 + 21 + 10, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Second file should start on track 2 sector 3 for -e -b 3";
    ++test;
    create_value_file("1.prg", 254, 1);
    create_value_file("2.prg", 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -e -b 3 -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 3 + 21, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Second file should start on track 1 when it fits for -E";
    ++test;
    create_value_file("1.prg", 20 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -E -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 11, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Second file should not start on track 1 when it does not fit for -E";
    ++test;
    create_value_file("1.prg", 20 * 254, 1);
    create_value_file("2.prg", 2 * 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -E -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 11, 0)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File should start on track 13 for -r";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-r 13 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[12] / 256 + 0, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Transwarp file should start on track 13 for -r";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-r 13 -W 1.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (!block_is_filled(image, track_offset[12] / 256 + 0, 0)) { /* not the best test, check only if not 0 */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Next file should be allocated in track 0 after -r";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-r 13 -f 1 -w 1.prg -f 2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[0] / 256 + 10, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File should start on sector 14 for -b";
    ++test;
    create_value_file("1.prg", 254, 1);
    create_value_file("2.prg", 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -b 14 -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 14, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File should be distributed to both sides for -c";
    ++test;
    create_value_file("1.prg", 22 * 254, 1);
    if (run_binary_cleanup(binary, "-c -w 1.prg", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset_b[0] / 256 + 0, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File should cover 1 sector on track 19 for -x not set";
    ++test;
    create_value_file("1.prg", 356 * 254, 1); /* leaves only one sector free before track 18 */
    create_value_file("2.prg", 2 * 254, 2);
    if (run_binary_cleanup(binary, "-w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[18] / 256 + 13, 0)) { /* check only second sector */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File should cover 2 sectors on track 19 for -x";
    ++test;
    create_value_file("1.prg", 356 * 254, 1); /* leaves only one sector free before track 18 */
    create_value_file("2.prg", 2 * 254, 2);
    if (run_binary_cleanup(binary, "-x -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[18] / 256 + 10, 2)) { /* check only second sector */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File should be placed on track 18 for -t";
    ++test;
    create_value_file("1.prg", 357 * 254, 1); /* fills all tracks up to 18 */
    create_value_file("2.prg", 2 * 254, 2);
    if (run_binary_cleanup(binary, "-t -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[17] / 256 + 12, 2)) { /* check only second sector, 2 sectors are filled by dir */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File with 3 sectors should be placed on track 18 for -u";
    ++test;
    create_value_file("1.prg", 357 * 254, 1); /* fills all tracks up to 18 */
    create_value_file("2.prg", 3 * 254, 2);
    if (run_binary_cleanup(binary, "-t -u 3 -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[17] / 256 + 3 /* (3+10+10)%19-1 */, 2)) { /* check only third sector */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Track 23 sector 0 and 1 should be identical to track 18 for -d";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    create_value_file("2.prg", 5 * 254, 2);
    if (run_binary_cleanup(binary, "-d 23 -w 1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (memcmp(&image[track_offset[17] + 1], &image[track_offset[22] + 1], 1) == 0 /* shadow BAM is not valid, only need the sector link */
               && memcmp(&image[track_offset[17] + 1] + 256, &image[track_offset[22] + 1] + 256, 255) == 0) {
        /* leave out next dir block track */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "File should have DIR block size 0 for -B";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    if (run_binary_cleanup(binary, "-B 0 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 30] == 0 && image[track_offset[17] + 256 + 31] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File should have DIR block size 65535 for -B";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    if (run_binary_cleanup(binary, "-B 65535 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 30] == (char)255 && image[track_offset[17] + 256 + 31] == (char)255) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should have actual DIR block size per default";
    ++test;
    create_value_file("1.prg", 258 * 254, 1);
    if (run_binary_cleanup(binary, "-w 1.prg -f loop -l 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 32 + 30] == 2 && image[track_offset[17] + 256 + 32 + 31] == 1) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should have DIR block size 258 for -B";
    ++test;
    create_value_file("1.prg", 39 * 254, 1);
    if (run_binary_cleanup(binary, "-w 1.prg -f loop -B 258 -l 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 32 + 30] == 2 && image[track_offset[17] + 256 + 32 + 31] == 1) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File should have DIR block size 258 for -B, but actual block size in shadow dir for -d";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    if (run_binary_cleanup(binary, "-B 258 -d 23 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 30] == 258%256 && image[track_offset[17] + 256 + 31] == 258/256 && image[track_offset[22] + 256 + 30] == 3 && image[track_offset[22] + 256 + 31] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should share track and sector with file using wildcard * for -l";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f loop -l 1.* -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if ((image[track_offset[17] + 256 + 3] == image[track_offset[17] + 256 + 32 + 3]) && (image[track_offset[17] + 256 + 4] == image[track_offset[17] + 256 + 32 + 4])) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should share track and sector with file using wildcard ? for -l";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f loop -l 1.p?g -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if ((image[track_offset[17] + 256 + 3] == image[track_offset[17] + 256 + 32 + 3]) && (image[track_offset[17] + 256 + 4] == image[track_offset[17] + 256 + 32 + 4])) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should share track and sector with later file using -l";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f loop -l 1.prg -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if ((image[track_offset[17] + 256 + 3] == image[track_offset[17] + 256 + 32 + 3]) && (image[track_offset[17] + 256 + 4] == image[track_offset[17] + 256 + 32 + 4])) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Loop file should share track and sector with file using -l when modifying image";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-f loop -l file", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if ((image[track_offset[17] + 256 + 3] == image[track_offset[17] + 256 + 32 + 3]) && (image[track_offset[17] + 256 + 4] == image[track_offset[17] + 256 + 32 + 4])) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing a PRG should result in first two blocks allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-F 0 -S 1 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfc) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing a PRG should result in first two blocks allocated on d81";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[40*39*256+256 + 6 + 11] == (char)0xfc) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing a DEL should result in first two blocks allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-T DEL -F 0 -S 1 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfc) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing two files with type 0 should result in first two blocks allocated";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-T 0 -S 1 -F 0 -f 1 -w 1.prg -T 0 -f 2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfc) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing no file with -L should create dir entry, but not allocate any block";
    ++test;
    if (run_binary_cleanup(binary, "-F 0 -S 1 -f file -L", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if ((image[track_offset[17] + 256 + 2] == (char)0x82) && image[track_offset[17] + 5] == (char)0xff) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Overwriting a PRG should result in only first block allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary(binary, "-F 0 -S 1 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-F 0 -S 1 -f file -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfe) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Overwriting a non-standard file type should result in only first block allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary(binary, "-F 0 -S 1 -T 69 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-F 0 -S 1 -f file -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfe) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Overwriting a PRG on d81 should result in only first block allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary(binary, "-f file -w 1.prg", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-f file -w 2.prg", "image.d81", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[40 * 39 * 256 + 256 + 6 + 11] == (char)0xfe) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Overwriting a PRG with a USR should result in only first block allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary(binary, "-F 0 -S 1 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-T USR -F 0 -S 1 -f file -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfe) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Overwriting a PRG with a DEL should result in only first block allocated";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary(binary, "-F 0 -S 1 -f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-T DEL -F 0 -S 1 -f file -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 5] == (char)0xfe) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Protect flag should be set for -P";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-P -f file1 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 2] == (char)0xc2) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Protect flag should be set for transwarp file for -P";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-P -f file1 -W 1.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 2] == (char)0xc2) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Open flag should be set for -O";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-O -f file1 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 2] == (char)0x02) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Open flag should be set for transwarp file for -O";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-O -f file1 -W 1.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 2] == (char)0x02) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "After having set type, open and protected flag next file should go back to normal PRG as default";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-T USR -P -O -f file1 -w 1.prg -f file2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 32 + 2] == (char)0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "After having set type, open and protected flag next file should go back to normal PRG as default";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-T USR -P -O -f file1 -w 1.prg -f file2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 32 + 2] == (char)0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "File should be overwritten even if there is a free dir slot before";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary(binary, "-f file1 -w 1.prg -f file2 -w 1.prg -f file1 -T DEL -O -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-f file2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 +2] == (char)0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing 9 files should allocate new dir sector";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg -f 3 -w 1.prg -f 4 -w 1.prg -f 5 -w 1.prg -f 6 -w 1.prg -f 7 -w 1.prg -f 8 -w 1.prg -f 9 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 256 + 3*256 + 2] == (char)0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Directory should allow for 144 entries";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary(binary, "-f 00 -w 1.prg -f 01 -w 1.prg -f 02 -w 1.prg -f 03 -w 1.prg -f 04 -w 1.prg -f 05 -w 1.prg -f 06 -w 1.prg -f 07 -w 1.prg -f 08 -w 1.prg -f 09 -w 1.prg -f 0a -w 1.prg -f 0b -w 1.prg -f 0c -w 1.prg -f 0d -w 1.prg -f 0e -w 1.prg -f 0f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 10 -w 1.prg -f 11 -w 1.prg -f 12 -w 1.prg -f 13 -w 1.prg -f 14 -w 1.prg -f 15 -w 1.prg -f 16 -w 1.prg -f 17 -w 1.prg -f 18 -w 1.prg -f 19 -w 1.prg -f 1a -w 1.prg -f 1b -w 1.prg -f 1c -w 1.prg -f 1d -w 1.prg -f 1e -w 1.prg -f 1f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 20 -w 1.prg -f 21 -w 1.prg -f 22 -w 1.prg -f 23 -w 1.prg -f 24 -w 1.prg -f 25 -w 1.prg -f 26 -w 1.prg -f 27 -w 1.prg -f 28 -w 1.prg -f 29 -w 1.prg -f 2a -w 1.prg -f 2b -w 1.prg -f 2c -w 1.prg -f 2d -w 1.prg -f 2e -w 1.prg -f 2f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 30 -w 1.prg -f 31 -w 1.prg -f 32 -w 1.prg -f 33 -w 1.prg -f 34 -w 1.prg -f 35 -w 1.prg -f 36 -w 1.prg -f 37 -w 1.prg -f 38 -w 1.prg -f 39 -w 1.prg -f 3a -w 1.prg -f 3b -w 1.prg -f 3c -w 1.prg -f 3d -w 1.prg -f 3e -w 1.prg -f 3f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 40 -w 1.prg -f 41 -w 1.prg -f 42 -w 1.prg -f 43 -w 1.prg -f 44 -w 1.prg -f 45 -w 1.prg -f 46 -w 1.prg -f 47 -w 1.prg -f 48 -w 1.prg -f 49 -w 1.prg -f 4a -w 1.prg -f 4b -w 1.prg -f 4c -w 1.prg -f 4d -w 1.prg -f 4e -w 1.prg -f 4f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 50 -w 1.prg -f 51 -w 1.prg -f 52 -w 1.prg -f 53 -w 1.prg -f 54 -w 1.prg -f 55 -w 1.prg -f 56 -w 1.prg -f 57 -w 1.prg -f 58 -w 1.prg -f 59 -w 1.prg -f 5a -w 1.prg -f 5b -w 1.prg -f 5c -w 1.prg -f 5d -w 1.prg -f 5e -w 1.prg -f 5f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 60 -w 1.prg -f 61 -w 1.prg -f 62 -w 1.prg -f 63 -w 1.prg -f 64 -w 1.prg -f 65 -w 1.prg -f 66 -w 1.prg -f 67 -w 1.prg -f 68 -w 1.prg -f 69 -w 1.prg -f 6a -w 1.prg -f 6b -w 1.prg -f 6c -w 1.prg -f 6d -w 1.prg -f 6e -w 1.prg -f 6f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary(binary, "-f 70 -w 1.prg -f 71 -w 1.prg -f 72 -w 1.prg -f 73 -w 1.prg -f 74 -w 1.prg -f 75 -w 1.prg -f 76 -w 1.prg -f 77 -w 1.prg -f 78 -w 1.prg -f 79 -w 1.prg -f 7a -w 1.prg -f 7b -w 1.prg -f 7c -w 1.prg -f 7d -w 1.prg -f 7e -w 1.prg -f 7f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-f 80 -w 1.prg -f 81 -w 1.prg -f 82 -w 1.prg -f 83 -w 1.prg -f 84 -w 1.prg -f 85 -w 1.prg -f 86 -w 1.prg -f 87 -w 1.prg -f 88 -w 1.prg -f 89 -w 1.prg -f 8a -w 1.prg -f 8b -w 1.prg -f 8c -w 1.prg -f 8d -w 1.prg -f 8e -w 1.prg -f 8f -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (strncmp(&image[track_offset[17] + (1+18*3)%19*256 + 7*32 + 5], "8F", 2) == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing a duplicate DIR entry with -N should write a new file";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -N -f 1 -w 2.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 10, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Writing a duplicate DIR entry with -o should return an error";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary(binary, "-m -f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-m -o -f 1 -w 1.prg ", "image.d64", &image, &size, true) != NO_ERROR) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Sector chain with invalid track link should be re-added to dir but left invalid for -R 0";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 0 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[10 * 256] == 36) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring scratched first file should regenerate the dir entry for -R 1";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratched */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring huge scratched file should regenerate the dir entry for -R 1";
    ++test;
    create_value_file("1.prg", 664 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratched */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring scratched last file should regenerate the dir entry for -R 1";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg -f 3 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2*32 + 2] = 0; /* scratched */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2*32 + 2] == 0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring scratched mid file should regenerate the dir entry for -R 1";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg -f 3 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratched */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 1*32 + 2] == 0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring scratched file should result in correct file size for -R 1";
    ++test;
    create_value_file("1.prg", (1*256 + 13) * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 30] = 0; /* delete file size */
    image[track_offset[17] + 256 + 31] = 0;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 30] == 13 && (unsigned char)image[track_offset[17] + 256 + 31] == 1) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring 10 files with same name should create correct filename appendix for -R 1";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-m -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg -N -f file -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 2*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 4*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 5*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 6*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 7*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 4*256 + 0*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 4*256 + 1*32 + 2] = 0; /* scratch file */
    image[track_offset[17] + 4*256 + 2*32 + 2] = 0; /* scratch file */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 4*256 + 2*32 + 10] == '1' && (unsigned char)image[track_offset[17] + 4*256 + 2*32 + 11] == '0') {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring unlinked dir sector should link it for -R 1";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg -f 3 -w 1.prg -f 4 -w 1.prg -f 5 -w 1.prg -f 6 -w 1.prg -f 7 -w 1.prg -f 8 -w 1.prg -f 9 -w 1.prg -f 10 -w 1.prg -f 11 -w 1.prg -f 12 -w 1.prg -f 13 -w 1.prg -f 14 -w 1.prg -f 15 -w 1.prg -f 16 -w 1.prg -f 17 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 1*256 + 1] = 7; /* make first sector link point to third dir sector */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 7*256 + 1] == 4) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Scratched file with conflicting t/s chain should be ignored for -R 1";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[20*256+1] = 10; /* make t/s link point to second sector of first file */
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch second file */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 1*32 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring should be robust against invalid track pointer in directory for -R 1";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 1 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        result = TEST_PASS;
        ++passed;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild valid sector chain should be restored for -R 2";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = (unsigned char)0xff; /* overwrite track pointer */
    image[track_offset[17] + 256 + 4] = (unsigned char)0xff; /* overwrite sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && (unsigned char)image[track_offset[17] + 256 + 3] == 1 && (unsigned char)image[track_offset[17] + 256 + 4] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Restoring wild sector chain should result in correct file size for -R 2";
    ++test;
    create_value_file("1.prg", (1*256 + 13) * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[track_offset[17] + 256 + 30] = 0; /* delete file size */
    image[track_offset[17] + 256 + 31] = 0;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 30] == 13 && (unsigned char)image[track_offset[17] + 256 + 31] == 1) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with invalid track link should be ignored for -R 2";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with invalid sector link should be ignored for -R 2";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[10 * 256 + 0] = 1;
    image[10 * 256 + 1] = 21; /* invalid sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with conflict should be ignored for -R 2";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[20*256+1] = 10; /* make t/s link point to second sector of first file */
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 1*32 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 1*32 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 1*32 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with loop should be ignored for -R 2";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[10*256+0] = 1; /* make t/s link loop to first block */
    image[10*256+1] = 0;
    image[track_offset[17] + 256 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild valid single sector should be ignored for -R 2";
    ++test;
    create_value_file("1.prg", 1 * 113, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 2 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Scratched file with invalid track link should be fixed for -R 3";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 3 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[10 * 256] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Fixed file with long name should receive marker for -R 3";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 0123456789123456 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 3 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 20] == '<') {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Fixed file with existing name should receive appendix and marker for -R 3";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-m -f file1 -w 1.prg -N -f \"file1<\" -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 3 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 13] == '<') {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Conflicting t/s chain should result in correct file size for -R 3";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    create_value_file("2.prg", 2 * 254, 2);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 2.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[20*256+1] = 10; /* make t/s link point to second sector of first file */
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 1*32 + 30] = (unsigned char)0xff; /* overwrite file size */
    image[track_offset[17] + 256 + 1*32 + 31] = (unsigned char)0xff;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 3 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 +1*32 + 2] == 0x82 && (unsigned char)image[track_offset[17] + 256 + 1*32 + 30] == 1 && (unsigned char)image[track_offset[17] + 256 + 1*32 + 31] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        printf(" (%d/%d) ", (unsigned char)image[track_offset[17] + 256 + 1*32 + 30], (unsigned char)image[track_offset[17] + 256 + 1*32 + 31]);
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with invalid track link should be fixed for -R 4";
    ++test;
    create_value_file("1.prg", 254 + 11, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[10 * 256] = 36; /* invalid track pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[10 * 256] == 0 && (unsigned char)image[10 * 256 + 1] == 0xff) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with invalid sector link should be fixed for -R 4";
    ++test;
    create_value_file("1.prg", 254 + 11, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[10 * 256 + 0] = 1;
    image[10 * 256 + 1] = 21; /* invalid sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[10 * 256] == 0 && (unsigned char)image[10 * 256 + 1] == 0xff) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with conflict should be fixed for -R 4";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[19*256+1] = 10; /* make t/s link point to second sector of first file */
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 1*32 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 1*32 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 1*32 + 2] == 0x82 && image[19*256] == 0 && (unsigned char)image[19*256+1] == 0xff) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild sector chain with loop should be fixed for -R 4";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[10*256+0] = 1; /* make t/s link loop to first block */
    image[10*256+1] = 0;
    image[track_offset[17] + 256 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[10 * 256] == 0 && (unsigned char)image[10 * 256 + 1] == 0xff) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild valid single sector should be ignored for -R 4";
    ++test;
    create_value_file("1.prg", 1 * 113, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Scratched file with immediate conflict should be ignored for -R 4";
    ++test;
    create_value_file("1.prg", 3 * 254, 1);
    create_value_file("2.prg", 1 * 254, 2);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg -f 2 -w 2.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 1*32 + 2] = 0; /* scratch second file */
    image[track_offset[17] + 256 + 1*32 + 3] = 1; /* let t/s point to first file */
    image[track_offset[17] + 256 + 1*32 + 4] = 0;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 4 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 1*32 + 2] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Wild valid single sector should be restored for -R 5";
    ++test;
    create_value_file("1.prg", 1 * 113, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 5 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Wild valid single sector should have correct size for -R 5";
    ++test;
    create_value_file("1.prg", 1 * 113, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratch file */
    image[track_offset[17] + 256 + 3] = 0; /* delete track pointer */
    image[track_offset[17] + 256 + 4] = 0; /* delete sector pointer */
    image[track_offset[17] + 256 + 30] = (unsigned char)0xff; /* overwrite file size */
    image[track_offset[17] + 256 + 31] = (unsigned char)0xff;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-R 5 ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if((unsigned char)image[track_offset[17] + 256 + 2] == 0x82 && image[track_offset[17] + 256 + 30] == 1 && image[track_offset[17] + 256 + 31] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Recursive DIR sector chain should not run into endless loop";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 0] = 18; /* track link points to dir track */
    image[track_offset[17] + 256 + 1] = 1;  /* sector link points to itself */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-f 2 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        result = TEST_PASS;
        ++passed;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Recursive file sector chain should not run into endless loop for overwriting";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[10 * 256 + 0] = 1; /* t/s link points to first block */
    image[10 * 256 + 1] = 0;
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-f 1 -w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        result = TEST_PASS;
        ++passed;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Should not return error for illegal t/s link in DIR chain";
    ++test;
    create_value_file("1.prg", 254 + 11, 1);
    if (run_binary_cleanup(binary, "-w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 0] = 18;  /* legal track */
    image[track_offset[17] + 256 + 1] = 127; /* illegal sector */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "", "image.d64", &image, &size, true) == NO_ERROR) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Should not return error for illegal t/s pointer in DIR";
    ++test;
    create_value_file("1.prg", 254 + 11, 1);
    if (run_binary_cleanup(binary, "-w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 4] = 127; /* illegal sector */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "", "image.d64", &image, &size, true) == NO_ERROR) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Verbose output should not return error for illegal t/s pointer in DIR";
    ++test;
    create_value_file("1.prg", 254 + 11, 1);
    if (run_binary_cleanup(binary, "-w 1.prg ", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 4] = 127; /* illegal sector */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-v", "image.d64", &image, &size, true) == NO_ERROR) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Adding a transwarp v0.84 file should allocate track 1 completely";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-r 1 -f file1 -W 1.prg -w \"transwarp v0.84.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 4] == 0 && image[track_offset[17] + 5] == 0 && image[track_offset[17] + 6] == 0 && image[track_offset[17] + 7] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Adding a transwarp v0.86 file should allocate track 1 completely";
    ++test;
    create_value_file("1.prg", 2 * 254, 1);
    if (run_binary_cleanup(binary, "-r 1 -f file1 -W 1.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17] + 4] == 0 && image[track_offset[17] + 5] == 0 && image[track_offset[17] + 6] == 0 && image[track_offset[17] + 7] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Standard file should be allocated in track 0 for transwarp image";
    ++test;
    create_value_file("1.prg", 1 * 254, 1);
    if (run_binary_cleanup(binary, "-b 0 -f file1 -w 1.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[0] / 256 + 0, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Sector on new track should not be limited to number of sectors on old track";
    ++test;
    create_value_file("1.prg", 254 * 20, 1); /* track 24 has 19 blocks, track 25 only 18 */
    if (run_binary_cleanup(binary, "-r 24 -F 18 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[24]/256, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Last file block should set unused bytes to zero";
    ++test;
    create_value_file("1.prg", 254 * 1, 1);
    create_value_file("2.prg", 1, 2);
    if (run_binary_cleanup(binary, "-w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    image[track_offset[17] + 256 + 2] = 0; /* scratched */
    image[track_offset[17] + 5] = (char)0xff; /* deallocated in BAM */
    write_file("image.d64", size, image);
    if (run_binary_cleanup(binary, "-w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    }
    if(result != TEST_UNRESOLVED) {
        result = TEST_PASS;
        if(image[2] != 2) {
            result = TEST_FAIL;
        }
        for(int b = 3; b < 256; b++) {
            if(image[b] != 0) {
                result = TEST_FAIL;
            }
        }
        if(result == TEST_PASS) {
            ++passed;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "BAM message for D64 should be limited to 85 chars";
    ++test;
    if (run_binary_cleanup(binary, "-H 012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17]+0xab] == '0' && image[track_offset[17]+255] == '4' && image[track_offset[17]+256] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "BAM message for SPEED DOS should be limited to 20 chars";
    ++test;
    if (run_binary_cleanup(binary, "-4 -H 012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (image[track_offset[17]+0xab] == '0' && image[track_offset[17]+0xab+19] == '9' && image[track_offset[17]+0xab+20] == 0) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "Two large and two single track transwarp files that just fit should leave track 21 empty";
    ++test;
    create_value_file("1.prg", 8 * 21 * 223, 1); /* big file to fill everything until track 17 */
    create_value_file("2.prg", 8 * 21 * 223, 2); /* big file to fill everything until track 17 */
    create_value_file("3.prg", 19 * 223, 3); /* fits perfectly on track 20 */
    create_value_file("4.prg", 21 * 223, 4); /* fits perfectly on track 17 */
    if (run_binary_cleanup(binary, "-W 1.prg -W 2.prg -W 3.prg -W 4.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[20] / 256, 0)) { /* only check, if track 21 is free */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");
    remove("3.prg");
    remove("4.prg");

    description = "Lots of mixed files should just fit on disk";
    ++test;
    create_value_file("1.prg", (5 * 19 + 18) * 223, 1);
    create_value_file("2.prg", 4 * 254, 2);
    create_value_file("3.prg", 17 * 254, 3);
    create_value_file("4.prg", (5 * 18 + 17) * 223, 4);
    create_value_file("5.prg", 12 * 21 * 223, 5);
    create_value_file("6.prg", 30 * 254, 6);
    create_value_file("7.prg", 2 * 254, 7);
    create_value_file("8.prg", 38 * 254, 8);
    create_value_file("9.prg", 4 * 21 * 223, 9);
    if (run_binary_cleanup(binary, "-W 1.prg -w 2.prg -w 3.prg -W 4.prg -W 5.prg -w 6.prg -w 7.prg -w 8.prg -W 9.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_FAIL;
    } else {
        result = TEST_PASS; /* passes if not unresolved */
        ++passed;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");
    remove("3.prg");
    remove("4.prg");
    remove("5.prg");
    remove("6.prg");
    remove("7.prg");
    remove("8.prg");
    remove("9.prg");

    description = "Transwarp allocation should avoid reserved track 10";
    ++test;
    create_value_file("1.prg", 21 * 223, 1);
    create_value_file("2.prg", 8 * 21 * 223, 2);
    if (run_binary_cleanup(binary, "-r 10 -W 1.prg -W 2.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (!block_is_filled(image, track_offset[8] / 256, 0)) { /* only check that track 9 is not free */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Transwarp allocation should avoid reserved track 27";
    ++test;
    create_value_file("1.prg", 8 * 21 * 223, 1);
    create_value_file("2.prg", 9 * 21 * 223, 1);
    create_value_file("3.prg", 18 * 223, 2);
    create_value_file("4.prg", 7 * 19 * 223, 2);
    if (run_binary_cleanup(binary, "-W 1.prg -W 2.prg -r 27 -W 3.prg -W 4.prg -w \"transwarp v0.86.prg\"", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_FAIL;
    } else if (block_is_filled(image, track_offset[19] / 256, 0)) { /* only check that track 20 is free */
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");
    remove("3.prg");
    remove("4.prg");

    description = "-M width too low value should return error";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-M 5 -f file1 -w 1.prg -f file2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-M 4 -f file1 -w 1.prg -f file2 -w 1.prg", "image.d64", &image, &size, true) != NO_ERROR) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for wrong format specifier";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[track_offset[17]+2] = 0;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "-V should return error for wrong file type";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary(binary, "-f file -T 5 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for illegal track reference in directory";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[track_offset[17]+256+3] = 0;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for illegal sector reference in directory";
    ++test;
    create_value_file("1.prg", 254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[track_offset[17]+256+4] = 21;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for illegal track reference in file chain";
    ++test;
    create_value_file("1.prg", 2*254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[0] = 36;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for illegal sector reference in file chain";
    ++test;
    create_value_file("1.prg", 2*254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[1] = 21;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for loop in file chain";
    ++test;
    create_value_file("1.prg", 2*254, 1);
    if (run_binary_cleanup(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[1] = 0;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for collision in file chain";
    ++test;
    create_value_file("1.prg", 2*254, 1);
    if (run_binary_cleanup(binary, "-f file1 -w 1.prg -r 2 -b 0 -f file2 -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[10*256+0] = 2;
        image[10*256+1] = 10;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "-V should return error for BAM being out of sync with files";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else {
        image[track_offset[17]+5] = (char)254;
        write_file("image.d64", size, image);
        if (run_binary_cleanup(binary, "-V", "image.d64", &image, &size, true) != NO_ERROR) {
            result = TEST_PASS;
            ++passed;
        } else {
            result = TEST_FAIL;
        }
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Patch should overwrite block 27";
    ++test;
    create_value_file("1.prg", 256, 1);
    if (run_binary_cleanup(binary, "-p 2,6,0,1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 27, 1)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Patch should mark multiple blocks as used";
    ++test;
    create_value_file("1.prg", 2*256, 1);
    if (run_binary_cleanup(binary, "-p 19,18,1,1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (((unsigned char)(image[track_offset[17] + 4 + 18*4 + 3]) == 3) && ((unsigned char)(image[track_offset[17] + 4 + 19*4 + 1]) == 252)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    description = "Writing file should avoid patched sector";
    ++test;
    create_value_file("1.prg", 2*256, 1);
    create_value_file("2.prg", 1*254, 2);
    if (run_binary_cleanup(binary, "-p 1,0,0,1.prg -w 2.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, track_offset[0] / 256 + 2, 2)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("2.prg");

    description = "Rewriting an extracted file should fill block 0";
    ++test;
    create_value_file("1.prg", 1*256, 1);
    if (run_binary(binary, "-f file -w 1.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-X file", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (run_binary_cleanup(binary, "-w file.prg", "image.d64", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (block_is_filled(image, 1, 0)) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");
    remove("file.prg");

    description = "New d71 image should have correct numbers of free blocks per track on track 18";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (
        /* check only the first value for each zone */
        ((unsigned char)(image[track_offset[17] + 0xdd]) == 0x15) &&
        ((unsigned char)(image[track_offset[17] + 0xee]) == 0x00) &&
        ((unsigned char)(image[track_offset[17] + 0xef]) == 0x13) &&
        ((unsigned char)(image[track_offset[17] + 0xf5]) == 0x12) &&
        ((unsigned char)(image[track_offset[17] + 0xfb]) == 0x11)
    ) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "New d71 image should not have free block numbers on track 53";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (
        /* check only the first value for each zone */
        ((unsigned char)(image[track_offset_b[17] + 0xdd]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0xee]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0xef]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0xf5]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0xfb]) == 0)
    ) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "New d71 image should have complete track 53 marked as used in BAM";
    ++test;
    if (run_binary_cleanup(binary, "", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (
        ((unsigned char)(image[track_offset_b[17] + 0x33]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0x34]) == 0) &&
        ((unsigned char)(image[track_offset_b[17] + 0x35]) == 0)
    ) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);

    description = "File on second side should result in correct BAM and free block numbers for d71";
    ++test;
    create_value_file("1.prg", 1*254, 1);
    if (run_binary_cleanup(binary, "-r 36 -b 0 -w 1.prg", "image.d71", &image, &size, false) != NO_ERROR) {
        result = TEST_UNRESOLVED;
    } else if (((unsigned char)(image[track_offset_b[17] + 0]) == 0xfe) && (((unsigned char)(image[track_offset[17] + 0xdd]) == 0x14))) {
        result = TEST_PASS;
        ++passed;
    } else {
        result = TEST_FAIL;
    }
    printf("%0*d:  %s:  %s\n", test_pad, test, result_str[result], description);
    remove("1.prg");

    /* ideas for tests:
       - test writing of transwarp files
       - test encryption of transwarp files
       - check -g in more detail?
    */

    /* clean up */
    if (image != NULL) {
        free(image);
    }

    /* print summary */
    printf("\nPassed %d of %d tests.\n", passed, test);
    if (passed == test) {
        return 0;
    }
    return 1;
}
