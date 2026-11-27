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

/* transwarp.c: Transwarp files and Krill's loader filename hash */

#include "cit.h"

/* Calculates a hash from a filename to be used by Krill's loader */
unsigned int
filenamehash(const unsigned char *filename)
{
    int pos = min(max_hash_length, (int)strlen((char *)filename));
    while ((pos > 0) && (((unsigned char) filename[pos - 1]) == FILENAMEEMPTYCHAR)) {
        --pos;
    }

    unsigned char hashlo = pos;
    unsigned char hashhi = pos;
    int carry = 1;

    for (int i = pos - 1; i >= 0; --i) {
        unsigned int sum = hashlo + filename[i] + carry;
        carry = (sum >= 256) ? 1 : 0;
        sum &= 0xff;
        hashlo = sum;
        sum += hashhi + carry;
        carry = (sum >= 256) ? 1 : 0;
        hashhi = sum;
    }

    return (hashhi << 8) | hashlo;
}

/* Computes Transwarp dirdata checksum */
unsigned char
transwarp_dirdata_checksum(const unsigned char *image, int dir_entry_offset)
{
    int dirdata_checksum = 0;
    int carry = 1;
    for (int offset = DIRDATACHECKSUMOFFSET; offset <= FILEBLOCKSLOOFFSET; ++offset) {
        dirdata_checksum += (image[dir_entry_offset + offset] + carry);
        carry = (dirdata_checksum >= 0x0100) ? 1 : 0;
        dirdata_checksum &= 0xff;
    }

    return dirdata_checksum;
}

/* Checks if a given dir entry points to a Transwarp file */
bool
is_transwarp_file(const unsigned char *image, int dir_entry_offset)
{
    return (image[dir_entry_offset + TRANSWARPSIGNATROFFSLO] == TRANSWARPSIGNATURELO)
           && (image[dir_entry_offset + TRANSWARPSIGNATROFFSHI] == TRANSWARPSIGNATUREHI)
           && (transwarp_dirdata_checksum(image, dir_entry_offset) == 0);
}

/* Checks if a given dir entry points to the Transwarp bootfile */
bool
is_transwarp_bootfile(const unsigned char *image, int dir_entry_offset)
{
    return memcmp(image + dir_entry_offset + FILENAMEOFFSET, TRANSWARP, strlen(TRANSWARP)) == 0;
}

/* Return Transwarp file stat */
int
transwarp_stat(image_type type, const unsigned char *image, int dir_entry_offset, int *start_track, int *end_track, int *low_track, int *high_track)
{
    *start_track = 0;
    *end_track = 0;
    *low_track = 0;
    *high_track = 0;

    int filesize = (image[dir_entry_offset + ENDADDRESSLOOFFSET]  | (image[dir_entry_offset + ENDADDRESSHIOFFSET] << 8))
                   - (image[dir_entry_offset + LOADADDRESSLOOFFSET] | (image[dir_entry_offset + LOADADDRESSHIOFFSET] << 8));
    if (filesize <= 0) {
        return 0;
    }

    *start_track = image[dir_entry_offset + TRANSWARPTRACKOFFSET];
    *end_track = *start_track;

    int size = filesize;

    while (filesize > 0) {
        filesize -= (TRANSWARPBLOCKSIZE * num_sectors(type, *end_track));
        if (filesize > 0) {
            *end_track = (*end_track < DIRTRACK_D41_D71) ? (*end_track - 1) : (*end_track + 1);
        }
    }

    *low_track = (*start_track < *end_track) ? *start_track : *end_track;
    *high_track = (*start_track > *end_track) ? *start_track : *end_track;

    return size;
}

/* Return Transwarp file stat */
int
transwarp_size(image_type type, int start_track, int end_track, int filesize,
               int *transwarp_blocks, int *nonredundant_blocks, int *redundant_blocks, int *nonredundant_blocks_on_last_track)
{
    *transwarp_blocks = 0;

    int last_track_sectors;
    if (start_track <= end_track) {
        for (int track = start_track; track <= end_track; ++track) {
            last_track_sectors = num_sectors(type, track);
            *transwarp_blocks += last_track_sectors;
        }
    } else {
        for (int track = start_track; track >= end_track; --track) {
            last_track_sectors = num_sectors(type, track);
            *transwarp_blocks += last_track_sectors;
        }
    }

    int spare_bytes = TRANSWARPBLOCKSIZE - (filesize % TRANSWARPBLOCKSIZE);
    if (spare_bytes == TRANSWARPBLOCKSIZE) {
        spare_bytes = 0;
    }

    *nonredundant_blocks = (filesize / TRANSWARPBLOCKSIZE) + ((spare_bytes == 0) ? 0 : 1);
    *redundant_blocks = *transwarp_blocks - *nonredundant_blocks;
    *nonredundant_blocks_on_last_track = last_track_sectors - *redundant_blocks;

    return spare_bytes;
}

/* Finds all filenames with the given hash */
static int
count_hashes(image_type type, const unsigned char* image, unsigned int hash, bool print)
{
    int num = 0;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }

    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET];

        if (filetype != FILETYPEDEL) {
            unsigned char *filename = (unsigned char *) image + dirblock + FILENAMEOFFSET;
            if (hash == filenamehash(filename)) {
                ++num;

                if (print) {
                    printf(" [$%04x] ", filenamehash(filename));
                    print_filename(stdout, filename);
                    printf("\n");
                }
            }
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);

    return num;
}

/* Checks if multiple filenames have the same hash */
bool
check_hashes(image_type type, const unsigned char* image)
{
    bool collision = false;
    char *blockmap = calloc(image_num_blocks(type), sizeof(char));
    if(blockmap == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    printf("\n");

    int ds = first_dir_sector(type);
    int dt = dirtrack(type);
    int offset = 0;
    do {
        int dirblock = linear_sector(type, dt, ds) * BLOCKSIZE + offset;
        int filetype = image[dirblock + FILETYPEOFFSET];

        if (filetype != FILETYPEDEL) {
            unsigned char *filename = (unsigned char *) image + dirblock + FILENAMEOFFSET;
            if (count_hashes(type, image, filenamehash(filename), false /* print */) > 1) {
                collision = 1;
                fprintf(stderr, "Hash of filename ");
                print_filename(stderr, filename);
                fprintf(stderr, " [$%04x] is not unique\n", filenamehash(filename));
                count_hashes(type, image, filenamehash(filename), true /* print */);
            }
        }
    } while (next_dir_entry(type, image, &dt, &ds, &offset, blockmap));
    free(blockmap);
    return collision;
}


typedef struct transwarp_encode_context {
    unsigned int  version;
    unsigned char previous;
    unsigned char previous1;
    unsigned char previous2;
    unsigned char accu;
    unsigned char carry;
    unsigned char recvcarry;
    unsigned char carry2;
    unsigned char sendaccu;
    unsigned char sendcarry;
} transwarp_encode_context;

static const int ENCODE[5][64] = {
    {
        0xf6, 0xee, 0xf5, 0xed, 0x9a, 0xde, 0x96, 0xda, 0xf3, 0xea, 0xf2, 0x9e, 0x93, 0xd6, 0x92, 0xd3,
        0xd2, 0xca, 0xce, 0xbe, 0xb3, 0x7e, 0xb2, 0x7d, 0xcd, 0xba, 0xcb, 0xb6, 0xae, 0x7b, 0xaa, 0x7a,
        0x76, 0x6e, 0x75, 0x6d, 0x5e, 0x5b, 0x5d, 0x5a, 0x73, 0x6b, 0x72, 0x6a, 0xdb, 0x9d, 0xeb, 0xd5,
        0x56, 0x4e, 0x55, 0x4d, 0xbd, 0xb5, 0xbb, 0xad, 0x53, 0x4b, 0x52, 0xdd, 0xab, 0x4a, 0x9b, 0x95
    }, {
        0xf6, 0xee, 0xf5, 0xed, 0x9a, 0xde, 0x96, 0xda, 0xf3, 0xea, 0xf2, 0x9e, 0x93, 0xd6, 0x92, 0xef,
        0xe7, 0x7c, 0x9f, 0x74, 0xe6, 0x6c, 0xdf, 0xa6, 0x9c, 0xba, 0x94, 0xb6, 0xae, 0x7b, 0xaa, 0x7a,
        0x76, 0x6e, 0x75, 0x6d, 0x5e, 0x5b, 0x5d, 0x5a, 0x73, 0x6b, 0x72, 0x6a, 0xdb, 0xd7, 0xeb, 0xe5,
        0x56, 0x64, 0x55, 0x5c, 0xbd, 0xb5, 0xbb, 0xad, 0x65, 0x54, 0x5f, 0xdd, 0xab, 0xa7, 0x9b, 0xa5
    }, {
        0xa5, 0xa7, 0xa9, 0xab, 0xd5, 0xd7, 0xd9, 0xdb, 0x95, 0x99, 0x9b, 0x97, 0xe5, 0x9d, 0xeb, 0xe9
    }, {
        0xf6, 0xee, 0xf5, 0xed, 0x69, 0xde, 0x59, 0xda, 0xb9, 0xea, 0xb7, 0x6f, 0x57, 0xd6, 0x4f, 0xef,
        0xe7, 0xca, 0xce, 0xbe, 0xe6, 0xbf, 0xdf, 0xa6, 0xcd, 0xba, 0xcb, 0xb6, 0xae, 0x7b, 0xaa, 0x7a,
        0x76, 0x6e, 0x75, 0x6d, 0x5e, 0x5b, 0x5d, 0x5a, 0x67, 0x6b, 0x66, 0x6a, 0xe9, 0xd7, 0xeb, 0xe5,
        0x56, 0x4e, 0x55, 0x4d, 0xbd, 0xb5, 0xbb, 0xad, 0x65, 0x4b, 0x5f, 0xdd, 0xab, 0xa7, 0xa9, 0xa5
    }, {
        0xcf, 0xaf, 0xc9, 0x79, 0x69, 0xde, 0x59, 0xda, 0xb9, 0x77, 0xb7, 0x6f, 0x57, 0xd6, 0x4f, 0xd3,
        0xd2, 0xca, 0xce, 0xbe, 0xb3, 0x7e, 0xb2, 0x7d, 0xcd, 0xba, 0xcb, 0xb6, 0xae, 0x7b, 0xaa, 0x7a,
        0x76, 0x6e, 0x75, 0x6d, 0x5e, 0x5b, 0x5d, 0x5a, 0x73, 0x6b, 0x72, 0x6a, 0xdb, 0xd7, 0xd9, 0xd5,
        0x56, 0x4e, 0x55, 0x4d, 0xbd, 0xb5, 0xbb, 0xad, 0x53, 0x4b, 0x52, 0xdd, 0xab, 0x4a, 0xa9, 0x49
    }
};

static const int DECODE[256] = {
    -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, 0x00, 0x02, 0x12,   -1, 0x14, 0x16, 0x68,
        -1,   -1, 0x18, 0x1a, 0x12, 0x1c, 0x1e, 0x6a,   -1, 0x6c, 0x24, 0x26, 0x14, 0x2c, 0x2e, 0x18,
        -1,   -1,   -1,   -1, 0x16, 0x1a, 0x38, 0x3a,   -1, 0x6e, 0x30, 0x32, 0x46, 0x34, 0x36, 0x70,
        -1,   -1, 0x38, 0x3a, 0x54, 0x3c, 0x3e, 0x72,   -1, 0x74, 0x40, 0x42, 0x56, 0x44, 0x46,   -1,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
        -1,   -1, 0x68, 0x6a, 0x58, 0x80, 0x6c, 0x8a,   -1, 0x82, 0x6e, 0x88, 0x5a, 0xa2, 0x70, 0x5c,
        -1,   -1,   -1,   -1,   -1, 0x00, 0x44, 0x02,   -1, 0x08, 0x48, 0x0a,   -1, 0x04, 0x4a, 0x76,
        -1,   -1, 0x4c, 0x4e,   -1, 0x06, 0x50, 0x78,   -1, 0x7a, 0x52, 0x0c,   -1, 0x0e, 0x54, 0x46,
        -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, 0x7c, 0x56, 0x58,   -1, 0x5a, 0x5c, 0x7e,
        -1,   -1, 0x5e, 0x60,   -1, 0x20, 0x62, 0x22,   -1, 0x28, 0x64, 0x2a,   -1, 0x10, 0x66, 0x4c,
        -1,   -1,   -1,   -1,   -1, 0xa0, 0x4e, 0x5e,   -1, 0xaa, 0x72, 0xa8,   -1, 0x74, 0x76, 0x60,
        -1,   -1, 0x78, 0x7a,   -1, 0x7c, 0x7e,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1
    };

static unsigned char
encode_read_diff(const int encode[64], unsigned char *accu, unsigned char *carry, unsigned char value)
{
    unsigned char value_to_encode;

    unsigned char target = DECODE[encode[value]] & 0x7e;
    for (value_to_encode = 0; value_to_encode < 64; ++value_to_encode) {
        unsigned char val = DECODE[encode[value_to_encode]] + *accu + *carry;
        if ((val & 0x7e) == target) {
            break;
        }
    }
    if (value_to_encode >= 64) {
        printf("Encoding error, 0x%x = 0x%x + %d + ?\n", target, *accu, *carry);
        for (value_to_encode = 0; value_to_encode < 64; ++value_to_encode) {
            unsigned char val = DECODE[encode[value_to_encode]] + *accu + *carry;
            printf("%d: 0x%x <- 0x%x\n", value_to_encode, val & 0x7e, DECODE[encode[value_to_encode]]);
        }

        fprintf(stderr, "ERROR: Transwarp encoding error\n");
        exit(-2);
    }

    int sum = DECODE[encode[value_to_encode]] + *accu + *carry;
    *accu = sum;
    *carry = sum >= 256;

    unsigned char check = DECODE[encode[value]];
    if ((*accu & 0x7e) != (check & 0x7e)) {
        fprintf(stderr, "ERROR: Transwarp encoding error, 0x%x: actual 0x%x != 0x%x expected\n", DECODE[encode[check & 0x3f]], *accu, check);
        exit(-3);
    }

    unsigned char temp = (*carry << 7) | (*accu >> 1);
    *carry = *accu & 1;
    *accu = (*carry << 7) | ((temp & 0xfb) >> 1);
    *carry = (*accu >> 6) & 1;

    return encode[value_to_encode];
}

unsigned char
encode_send_diff(unsigned char value, unsigned char *accu, unsigned char *carry)
{
    value = (value & ~((1 << 3) | (1 << 0)))
            | (((value >> 3) & 1) << 0)
            | (((value >> 0) & 1) << 3);
    value ^= 0xff;

    int diff = value - *accu - *carry;
    *carry = (diff < 0);

    *accu = value;

    *accu = (((*accu >> 7) & 1) << 7)
            | (((*accu >> 1) & 1) << 6)
            | (((*accu >> 7) & 1) << 5)
            | (*carry << 4)
            | (((*accu >> 6) & 1) << 2)
            | (((*accu >> 5) & 1) << 1);
    *carry = (*accu >> 6) & 1;

    return diff;
}

static unsigned char
encode_receive_diff(const transwarp_encode_context *ctx, unsigned char in, unsigned char *previous, unsigned char *carry)
{
    int offset = (ctx->version <= 84) ? 0 : 2;

    int out = in - *carry - offset;
    int diff = out - ((*previous & 0xc0) | (out & 0x3f));
    *carry = (diff < 0);
    out = ((out ^ *previous) & 0x3f) | diff;
    *previous = in;

    return out;
}

static unsigned char
encode_buffer_byte(const int encode[][64], unsigned char previous, unsigned char *carry, unsigned char in, unsigned char *out)
{
    unsigned char even = (previous >> 1) ^ in;
    unsigned char odd = ((*carry << 7) | (previous >> 1)) ^ in;

    *carry = previous & 1;

    out[(31 * 5) + 4] = encode[2][even_bits(even)];
    out[4] = encode[2][odd_bits(odd)];

    return in;
}

static void
encode_base_bytes(const unsigned char scramble[][256],
                  transwarp_encode_context *ctx, const unsigned char in[3], unsigned char *out)
{
    unsigned char in0 = encode_receive_diff(ctx, in[0], &(ctx->previous), &(ctx->recvcarry));
    unsigned char in1 = encode_receive_diff(ctx, in[1], &(ctx->previous), &(ctx->recvcarry));
    unsigned char in2 = encode_receive_diff(ctx, in[2], &(ctx->previous), &(ctx->recvcarry));

    in0 = scramble[0][in0];
    in1 = scramble[1][in1];
    in2 = scramble[2][in2];

    unsigned char val3 = in0 & 0x3f;
    out[0] = encode_read_diff(ENCODE[3], &(ctx->accu), &(ctx->carry), val3);

    unsigned char val4 = ((in0 >> 6)
                          | (in1 << 2)) & 0x3f;
    out[1] = encode_read_diff(ENCODE[4], &(ctx->accu), &(ctx->carry), val4);

    unsigned char val0 = ((in1 >> 4)
                          | (in2 << 4)) & 0x3f;
    out[2] = encode_read_diff(ENCODE[0], &(ctx->accu), &(ctx->carry), val0);

    unsigned char val1 = in2 >> 2;
    out[3] = encode_read_diff(ENCODE[1], &(ctx->accu), &(ctx->carry), val1);
}

unsigned char
crc8(unsigned char value)
{
    for (int i = 0; i < 8; ++i) {
        value = (value & 0x80) ? ((value << 1) ^ 0x31) : (value << 1);
    }

    return value;
}

static int
encode_transwarp_block(const unsigned char scramble[][256], const int8_t gcr_to_nibble[32],
                       transwarp_encode_context* ctx, const unsigned char *indata, int filepos, unsigned char encoded[325])
{
    const unsigned char *unencoded = indata + filepos;

    unsigned char data[TRANSWARPBLOCKSIZE];
    if (filepos < 2) {
        for (int i = filepos, j = 0; j < TRANSWARPBLOCKSIZE; ++i, ++j) {
            data[j] = (i < 0) ? ' ' : ((i < 2) ? 0 : indata[i]);
        }

        unencoded = data;
    }

    unsigned char semiencoded[TRANSWARPBUFFERBLOCKSIZE];

    for (int i = TRANSWARPBUFFERBLOCKSIZE - 1; i >= 0; --i) {
        unsigned char value = encode_receive_diff(ctx, unencoded[TRANSWARPBASEBLOCKSIZE + i], &(ctx->previous2), &(ctx->carry2));

        value = scramble[3][value];

        semiencoded[i] = encode_send_diff(value, &(ctx->sendaccu), &(ctx->sendcarry));
    }

    unsigned char buffer_previous = ctx->previous1;
    unsigned char buffer_carry = 0;
    for (int i = 0; i < TRANSWARPBUFFERBLOCKSIZE; ++i) {
        int shuffle = (TRANSWARPBUFFERBLOCKSIZE - 1) - (i / 2) - ((i & 1) ? ((TRANSWARPBUFFERBLOCKSIZE / 2) + 1) : 0);
        unsigned char value = semiencoded[shuffle];
        buffer_previous = encode_buffer_byte(ENCODE, buffer_previous, &buffer_carry, value, encoded + 3 + (5 * (TRANSWARPBUFFERBLOCKSIZE - 1 - shuffle)));
    }

    unsigned char previous = unencoded[TRANSWARPBASEBLOCKSIZE - 1];

    const int CRCSTEP = 8;

    for (int i = 0; i < TRANSWARPBASEBLOCKSIZE; i += CRCSTEP) {
        previous = crc8(previous);
        previous ^= unencoded[i];
    }
    previous = crc8(previous);

    if (filepos > ((21 * TRANSWARPBLOCKSIZE) + 2)) {
        const int BACKCHECKOFFS = (21 * TRANSWARPBLOCKSIZE) + TRANSWARPBUFFERBLOCKSIZE;
        previous ^= indata[filepos - BACKCHECKOFFS];
        previous = crc8(previous);
        previous ^= indata[filepos - BACKCHECKOFFS + TRANSWARPBUFFERBLOCKSIZE - 1];
        previous = crc8(previous);
    }

    char head_data[4] = { 7, 0, 0, 0 };
    encode_4_bytes_gcr(head_data, (char *) encoded);
    char tail_data[4] = { 0, 0, 0, 0 };
    encode_4_bytes_gcr(tail_data, (char *) encoded + 320);

    unsigned char accu = ctx->previous;
    unsigned char carry = 0;
    for (int i = 0; i < TRANSWARPBASEBLOCKSIZE; ++i) {
        encode_receive_diff(ctx, unencoded[i], &accu, &carry);
    }

    unsigned char receive_checksum = (-previous - carry);

    unsigned char checksum = receive_checksum ^ (buffer_previous >> 1);

    unsigned char odd = odd_bits(checksum);

    odd ^= (buffer_carry << 3);

    static const unsigned char ENCODE_TOP[] = {
        0x05,
        0x07,
        0x0d,
        0x0b
    };
    encoded[2] = (encoded[2] & 0xf0) | ENCODE_TOP[odd & 0x3];

    encoded[317] = ENCODE[2][even_bits(checksum)];

    encoded[322] = ENCODE[2][odd & 0xc];

    unsigned char top_2_bits = encoded[2];
    unsigned char middle_4_bits = encoded[317];
    unsigned char bottom_2_bits = encoded[322];

    ctx->recvcarry = 0;

    ctx->accu = 0;
    ctx->carry = 0;
    for (int i = 0, j = 0; i < TRANSWARPBASEBLOCKSIZE; i += 3, j += 5) {
        encode_base_bytes(scramble, ctx, unencoded + i, encoded + 3 + j);

        ctx->accu = 8;
        ctx->carry = 0;

        unsigned char target = encoded[3 + j + 4];
        unsigned char target_accu = DECODE[target];
        unsigned char target_check = ENCODE[2][odd_bits(target_accu)];

        if (target != target_check) {
            fprintf(stderr, "ERROR: Transwarp encoding error, [%d] 0x%x != 0x%x <- 0x%x\n", i, target, target_check, DECODE[target]);

            exit(-4);
        }

        unsigned char store = ctx->accu ^ target_accu;
        encoded[3 + j + 4] = ENCODE[2][odd_bits(store)];

        unsigned char stored = DECODE[encoded[3 + j + 4]];
        ctx->accu ^= stored;

        if (ctx->accu != target_accu) {
            fprintf(stderr, "ERROR: Transwarp encoding error, [%d] actual 0x%x != 0x%x expected <- 0x%x\n", i, ctx->accu, target_accu, stored);

            exit(-5);
        }
    }
    if (carry != ctx->recvcarry) {
        fprintf(stderr, "ERROR: Transwarp encoding error, carry %d != %d recvcarry\n", carry, ctx->recvcarry);

        exit(-6);
    }

    unsigned char top_fix = encoded[2] ^ (DECODE[encoded[322]] & 0xf);
    top_fix = ((top_fix >> 2) & 2) | ((top_fix >> 1) & 1);
    encoded[2] = (encoded[2] & 0xf0) | ENCODE_TOP[top_fix];
    encoded[322] = (encoded[322] & 0xf0) | 0x5;

    unsigned char block_checksum;
    block_checksum = DECODE[middle_4_bits];
    block_checksum = (block_checksum >> 1) | (buffer_carry << 7);
    block_checksum ^= DECODE[bottom_2_bits];
    block_checksum ^= (top_2_bits & 0xa);

    if (block_checksum != checksum) {
        fprintf(stderr, "ERROR: Transwarp encoding error, actual 0x%x != 0x%x expected, 0x%x -> [0x%x] -> 0x%x -> 0x%x -> [0x%x]\n", block_checksum, checksum, checksum & 0xaa, ((checksum & 0xaa) >> 1) | (checksum & 0xaa), odd, ENCODE[2][odd], DECODE[encoded[317]]);

        exit(-1);
    }

    unsigned char gcr_decoded[4];
    bool ok = decode_5_bytes_gcr(gcr_to_nibble, encoded + 320, gcr_decoded);
    int gcr_checksum = ok ? gcr_decoded[1] : -1;

    unsigned char decoded[256];
    int computed_checksum = decode_gcr_block(gcr_to_nibble, encoded, decoded);

    ok &= decode_5_bytes_gcr(gcr_to_nibble, encoded, gcr_decoded);
    gcr_decoded[1] ^= gcr_checksum ^ computed_checksum;

    encode_4_bytes_gcr((char *) gcr_decoded, (char *) encoded);

    return ok == 0;
}

static void
permute(unsigned char *key, int len, int *set)
{
    for (int i = 0; i < (len - 1); ++i) {
        int divisor = len - i;

        int remainder = 0;
        for (int i = TRANSWARPKEYSIZE - 12; i >= 0; --i) {
            int dividend = (remainder << 8) | key[i];
            remainder = dividend % divisor;
            key[i] = dividend / divisor;
        }

        int n = set[remainder];
        set[remainder] = set[divisor - 1];
        set[divisor - 1] = n;
    }
}

/* Write file to disk using Transwarp encoding */
unsigned long long
write_transwarp_file(image_type type, unsigned char *image, imagefile *file, unsigned char *filedata, int *filesize, unsigned int version, bool transwarp_bootfile_fits_on_dir_track)
{
    file->size = *filesize - 2;

    unsigned int track = DIRTRACK_D41_D71 - 1;

    if (file->min_track > 0) {
        /* for Transwarp files, a set minimum track is the file's starting track */
        track = file->min_track;
    } else {
        /* allocate */
        bool free_tracks[40];
        for (unsigned int t = 1; t <= image_num_tracks(type); ++t) {
            bool track_free = true;
            for (int sector = 0; sector < num_sectors(type, t); ++sector) {
                if (is_sector_free(type, image, t, sector, 0 /* numdirblocks */, 0 /* dir_sector_interleave */) == false) {
                    track_free = false;
                    break;
                }
            }
            free_tracks[t - 1] = track_free;
        }

        /* below dir track */
        while (track > 0) {
            if (free_tracks[track - 1]) {
                int filesize = file->size;
                int t = track;
                while ((filesize > 0)
                        && (t > 0)) {
                    if (free_tracks[t - 1] == false) {
                        break;
                    }
                    filesize -= (TRANSWARPBLOCKSIZE * num_sectors(type, t));
                    if (filesize >= 0) {
                        --t;
                    }
                }

                if (filesize <= 0) {
                    break;
                }
            }
            --track;
        }

        if (track <= 0) {
            /* above dir track */
            track = DIRTRACK_D41_D71 + (transwarp_bootfile_fits_on_dir_track ? 1 : 2);
            while (track <= image_num_tracks(type)) {
                if (free_tracks[track - 1]) {
                    int filesize = file->size;
                    int t = track;
                    while (filesize > 0) {
                        if (free_tracks[t - 1] == false) {
                            break;
                        }
                        filesize -= (TRANSWARPBLOCKSIZE * num_sectors(type, t));
                        if (filesize >= 0) {
                            ++t;
                        }
                    }

                    if (filesize <= 0) {
                        break;
                    }
                }
                ++track;
            }
        }
    }

    file->track = track;
    file->sector = 0;

    int8_t gcr_to_nibble[32];
    generate_gcr_decoding_table(NIBBLE_TO_GCR, gcr_to_nibble);

    unsigned char key[TRANSWARPKEYSIZE];
    memset(key, 0, sizeof key);

    if (file->have_key != 0) {
        if ((filedata[0] == 0x01)
                && (filedata[1] == 0x08)) {
            srand((unsigned int) time(NULL));

            unsigned int linelink = ((filedata[3] << 8) | filedata[2]) - 0x0801 + 2;
            if ((linelink > 0)
                    && ((linelink - 2) < (unsigned int) file->size)
                    && ((filedata[linelink - 1] | filedata[linelink] | filedata[linelink + 1]) == 0)) {
                while (filedata[linelink] == 0) {
                    filedata[linelink] = rand();
                }
            }

            filedata[2] = rand();
            while ((filedata[3] == 0)
                    || (filedata[3] == 8)) {
                filedata[3] = rand();
            }

            int file_size = file->size;
            int filetrack = track;
            while (file_size > 0) {
                file_size -= (TRANSWARPBLOCKSIZE * num_sectors(type, filetrack));
                if (file_size > 0) {
                    filetrack = (filetrack < DIRTRACK_D41_D71) ? (filetrack - 1) : (filetrack + 1);
                }
            }
            int spare_blocks = (0 - file_size) / TRANSWARPBLOCKSIZE;
            int spare_bytes = TRANSWARPBLOCKSIZE - (file->size % TRANSWARPBLOCKSIZE);
            spare_bytes = (spare_blocks * TRANSWARPBLOCKSIZE) + ((spare_bytes != TRANSWARPBLOCKSIZE) ? spare_bytes : 0);
            while (spare_bytes > (0x0801 - 0x0400)) {
                spare_bytes -= TRANSWARPBLOCKSIZE;
            }
            if (spare_bytes > 0) {
                int loadaddress = (filedata[1] << 8) | filedata[0];
                loadaddress -= spare_bytes;
                filedata[0] = loadaddress;
                filedata[1] = loadaddress >> 8;
                memmove(filedata + 2 + spare_bytes, filedata + 2, file->size);
                filedata[spare_bytes + 1] = 0;
                for (int i = 2; i <= spare_bytes; ++i) {
                    filedata[i] = rand();
                }
                file->size += spare_bytes;
                *filesize += spare_bytes;
            }
        }

        memcpy(key, file->key, sizeof key);

        for (int round = TRANSWARPKEYHASHROUNDS; round > 0; --round) {
            for (int i = 0; i < (TRANSWARPKEYSIZE - 1); ++i) {
                key[i] ^= key[i + 1];
            }

            for (int i = TRANSWARPKEYSIZE - 1; i >= 0; --i) {
                int product = key[i] * 0x6b;
                key[i] = product;
                int msb = product >> 8;
                for (int j = i + 1; j < TRANSWARPKEYSIZE; ++j) {
                    msb += key[j];
                    key[j] = msb;
                    msb >>= 8;
                }
            }

            int sum = round;
            for (int i = 0; i < TRANSWARPKEYSIZE; ++i) {
                sum += key[i];
                key[i] = sum;
                sum >>= 8;
            }
        }
    }

    unsigned long long dirdatakey = (key[TRANSWARPKEYSIZE - 1] * (1ULL << 56))
                                    + (key[TRANSWARPKEYSIZE - 2] * (1ULL << 48))
                                    + (key[TRANSWARPKEYSIZE - 3] * (1ULL << 40))
                                    + (key[TRANSWARPKEYSIZE - 4] * (1ULL << 32))
                                    + (key[TRANSWARPKEYSIZE - 5] * (1ULL << 24))
                                    + (key[TRANSWARPKEYSIZE - 6] * (1ULL << 16))
                                    + (key[TRANSWARPKEYSIZE - 7] * (1ULL << 8))
                                    + key[TRANSWARPKEYSIZE - 8];

    int initial_buffer_store_value = key[TRANSWARPKEYSIZE - 9];
    int initial_buffer_recvaccu_value = key[TRANSWARPKEYSIZE - 10];
    int initial_block_recvaccu_value = key[TRANSWARPKEYSIZE - 11];

    unsigned char scramble[4][256];
    for (int i = 0; i < 4; ++i) {
        int set[] = { 0, 2, 4, 1, 3, 5 };
        if (file->have_key) {
            permute(key, 6, set);
        }

        int set2[3][4];
        for (int j = 0; j < 3; ++j) {
            int set3[] = { 0, 1, 2, 3 };
            if (file->have_key) {
                permute(key, 4, set3);
            }
            for (int k = 0; k < 4; ++k) {
                for (int l = 0; l < 4; ++l) {
                    if (k == set3[l]) {
                        set2[j][k] = l;
                        break;
                    }
                }
            }
        }

        for (int j = 0; j < 256; ++j) {
            unsigned char scrambled = (set2[0][(((j >> set[0]) & 1) << 0)
                                               | (((j >> set[3]) & 1) << 1)] << 0)
                                      | (set2[1][(((j >> set[1]) & 1) << 0)
                                                 | (((j >> set[4]) & 1) << 1)] << 2)
                                      | (set2[2][(((j >> set[2]) & 1) << 0)
                                                 | (((j >> set[5]) & 1) << 1)] << 4)
                                      | (j & 0xc0);
            scramble[i][j] = scrambled;
        }
    }

    int sectors[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    if (file->have_key != 0) {
        permute(key, 17, sectors);
    }

    transwarp_encode_context ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.version = version;

    ctx.previous1 = initial_buffer_store_value;

    bool done = false;
    int block_index = 0;
    int total_blocks = 0;

    int filepos = 2;

    for (; !done; (track >= DIRTRACK_D41_D71) ? ++track : --track) {
        if ((track < 1)
                || (track > image_num_tracks(type))) {
            fprintf(stderr, "ERROR: Disk full (track %d out of range) while writing Transwarp file ", track);
            print_filename(stderr, file->pfilename);
            fprintf(stderr, "\n");
            exit(-4);
        }

        int next_track_pos = filepos + (num_sectors(type, track) * TRANSWARPBLOCKSIZE);
        bool last_track = (next_track_pos >= *filesize);
        if (last_track) {
            int sector = 0;
            for (int s = 0; s < num_sectors(type, track); ++s) {
                sectors[s] = sector;
                if ((filepos + ((sector + 1) * TRANSWARPBLOCKSIZE)) >= *filesize) {
                    sector = 0;
                } else {
                    ++sector;
                }
            }
        }
        int next_track_block_index = block_index + num_sectors(type, track);
        int trackpos = filepos;

        transwarp_encode_context trackctx = ctx;

        for (int sector = 0; sector < num_sectors(type, track); ++sector) {
            if (is_sector_free(type, image, track, sector, 0 /* numdirblocks */, 0 /* dir_sector_interleave */) == false) {
                fprintf(stderr, "ERROR: t%d/s%d not free for Transwarp file ", track, sector);
                print_filename(stderr, file->pfilename);
                fprintf(stderr, "\n");
                check_bam(type, image);

                exit(-5);
            }

            bool last_block = false;
            int pos = trackpos + (sectors[sector] * TRANSWARPBLOCKSIZE);
            if ((pos + TRANSWARPBLOCKSIZE) >= *filesize) {
                pos = *filesize - TRANSWARPBLOCKSIZE;
                last_block = true;
            }

            unsigned char encoded[320 + 5];
            unsigned char previous = block_index + sectors[sector];
            previous ^= initial_block_recvaccu_value;

            ctx.previous = previous;
            ctx.previous2 = initial_buffer_recvaccu_value;

            int error = encode_transwarp_block((const unsigned char (*)[256]) scramble, gcr_to_nibble, &ctx, filedata, pos, encoded);
            if (error) {
                fprintf(stderr, "ERROR: encoding error on t%d/s%d\n", track, sector);

                exit(-6);
            }

            unsigned char decoded[256];
            int checksum = decode_gcr_block(gcr_to_nibble, encoded, decoded);
            if (checksum < 0) {
                fprintf(stderr, "ERROR: decoding error on t%d/s%d\n", track, sector);

                exit(-7);
            }

            int offset = linear_sector(type, track, sector) * BLOCKSIZE;
            memcpy(image + offset, decoded, BLOCKSIZE);

            ++total_blocks;

            if (last_block) {
                ctx = trackctx;
                done = true;
            }

            mark_sector(type, image, track, sector, 0 /* not free */);
        }

        filepos = next_track_pos;
        block_index = next_track_block_index;
        file->last_track = track;
    }

    file->nrSectors = total_blocks;

    return dirdatakey;
}

/* Find consecutive tracks for Transwarp file, returns wasted blocks, or -1 if file does not fit */
static int
allocate_transwarp(int size, const int sectors[], int num_tracks, int *start_track, int *next_track)
{
    *start_track = *next_track;
    if(*start_track < DIRTRACK_D41_D71 - 1) {
        while(*start_track >= 0) {
            int s = size;
            int t = *start_track;
            while((t >= 0) && sectors[t]) {
                s -= sectors[t];
                if(s <= 0) {
                    *next_track = t ? t - 1 : DIRTRACK_D41_D71 + 1;
                    return -s;
                }
                t--;
            }
            *start_track = t - 1;
        }
        *start_track = DIRTRACK_D41_D71 + 1;
    }
    while(*start_track < num_tracks) {
        int s = size;
        int t = *start_track;
        while((t < num_tracks) && sectors[t]) {
            s -= sectors[t];
            if(s <= 0) {
                *next_track = t + 1;
                return -s;
            }
            t++;
        }
        *start_track = t + 1;
    }
    return -1;
}

/* Calculate wasted space for Transwarp files, returns false if files do not fit */
static bool
calculate_waste(const int size[], int num_files, const int sectors[], int num_tracks, int order[], int *waste, int start_tracks[])
{
    int next_track = DIRTRACK_D41_D71 - 2;
    *waste = 0;

    for(int f = 0; f < num_files; f++) {
        int w = allocate_transwarp(size[order[f]], sectors, num_tracks, &start_tracks[f], &next_track);
        if(w < 0) {
            return false;
        }
        *waste += w;
    }
    return true;
}

/* Find order of Transwarp files with minimal wasted space, returns wasted sectors, or MAXFREESECTORS if the files do not fit */
static int
find_best_permutation(const int size[], int num_files, const int sectors[], int num_tracks, int start_tracks[])
{
    int c[MAXPERMUTEDFILES];
    int o[MAXPERMUTEDFILES];
    int t[MAXPERMUTEDFILES];
    for(int i = 0; i < num_files; i++) {
        c[i] = 0;
        o[i] = i;
    }
    int min_waste;
    if(!calculate_waste(size, num_files, sectors, num_tracks, o, &min_waste, start_tracks)) {
        min_waste = MAXFREESECTORS; /* everything is better than this */
    }
    int original = min_waste;
    int i = 1;
    /* Heap's algorithm for permutation */
    while(i < num_files) {
        if(c[i] < i) {
            int w;
            if((i & 1) == 0) {
                int tmp = o[0];
                o[0] = o[i];
                o[i] = tmp; /* swap o[0] and o[i] */
            } else {
                int tmp = o[c[i]];
                o[c[i]] = o[i];
                o[i] = tmp; /* swap o[c[i]] and o[i] */
            }
            c[i]++;
            i = 1;
            if(calculate_waste(size, num_files, sectors, num_tracks, o, &w, t) && (w < min_waste)) {
                min_waste = w;
                for(int j = 0; j < num_files; j++) {
                    start_tracks[o[j]] = t[j];
                }
            }
        } else {
            c[i] = 0;
            i++;
        }
    }
    if(verbose) {
        if(original == MAXFREESECTORS) {
            printf("\nTranswarp reordering made files fit on disk\n");
        } else {
            printf("\nTranswarp reordering saved %d blocks\n", original - min_waste);
        }
    }
    return min_waste;
}

/* Reorder files, find best Transwarp file order and set start tracks accordingly */
void
optimize_transwarp_allocation(image_type type, unsigned char *image, imagefile *files, int num_files, int order[])
{
    /* find free tracks */
    int sectors[40]; /* size is number of tracks for extended D64 */
    unsigned int num_tracks = image_num_tracks(type);
    for (unsigned int t = 1; t <= num_tracks; ++t) {
        int ns = num_sectors(type, t);
        bool track_free = true;
        for (int sector = 0; sector < ns; ++sector) {
            if (is_sector_free(type, image, t, sector, 0 /* numdirblocks */, 0 /* dir_sector_interleave */) == false) {
                track_free = false;
                break;
            }
        }
        sectors[t-1] = track_free ? ns : 0;
    }

    /* sort files */
    int o = 0;
    for(int f = 0; f < num_files; f++) {
        /* Transwarp files with -t */
        int start_track = files[f].min_track;
        if((files[f].filetype & FILETYPETRANSWARPMASK) && (start_track > 0)) {
            start_track--; /* convert to array index */
            order[o++] = f;
            /* block tracks */
            int s = 0;
            struct stat st;
            if (stat((char*)files[f].alocalname, &st) == 0) {
                s = ((int)st.st_size - 2 - 1) / TRANSWARPBLOCKSIZE + 1; /* number of Transwarp blocks, rounded up */
            }
            unsigned int track_idx = start_track;
            while(s > 0) {
                s -= sectors[track_idx];
                sectors[track_idx] = 0;
                if(track_idx > DIRTRACK_D41_D71 - 2) {
                    track_idx++;
                    if(track_idx == num_tracks) {
                        return; /* give up, let actual file allocation deal with the error */
                    }
                } else if(track_idx == 0) {
                    track_idx = DIRTRACK_D41_D71;
                } else {
                    track_idx--;
                }
            }
        }
    }
    int first_transwarp = o;
    int sizes[MAXPERMUTEDFILES] = {0}; /* stays 0 if stat fails */
    int num_transwarp = 0;
    for(int f = 0; f < num_files; f++) {
        /* other Transwarp files */
        if((files[f].filetype & FILETYPETRANSWARPMASK) && (files[f].min_track <= 0)) {
            order[o++] = f;
            if(num_transwarp < MAXPERMUTEDFILES) {
                struct stat st;
                if (stat((char*)files[f].alocalname, &st) == 0) {
                    sizes[num_transwarp] = ((int)st.st_size - 2 - 1) / TRANSWARPBLOCKSIZE + 1; /* number of Transwarp blocks, rounded up */
                }
                num_transwarp++;
            }
        }
    }
    for(int f = 0; f < num_files; f++) {
        /* normal files */
        if(!(files[f].filetype & FILETYPETRANSWARPMASK)) {
            order[o++] = f;
        }
    }
    int start_tracks[MAXPERMUTEDFILES];
    find_best_permutation(sizes, num_transwarp, sectors, num_tracks, start_tracks);

    for(int f = 0; f < num_transwarp; f++) {
        files[order[first_transwarp + f]].min_track = start_tracks[f] + 1; /* was 0, see above */
    }
}
