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

/* gcr.c: GCR coding of the 1541 */

#include "cit.h"

/* Performs GCR encoding on 32 bit value */

const unsigned char NIBBLE_TO_GCR[] = {
    0x0a, 0x0b, 0x12, 0x13,
    0x0e, 0x0f, 0x16, 0x17,
    0x09, 0x19, 0x1a, 0x1b,
    0x0d, 0x1d, 0x1e, 0x15
};

void
encode_4_bytes_gcr(char* in, char* out)
{
    out[0] = (NIBBLE_TO_GCR[(in[0] >> 4) & 0xf] << 3) | (NIBBLE_TO_GCR[ in[0]       & 0xf] >> 2); /* 11111222 */
    out[1] = (NIBBLE_TO_GCR[ in[0]       & 0xf] << 6) | (NIBBLE_TO_GCR[(in[1] >> 4) & 0xf] << 1) | (NIBBLE_TO_GCR[ in[1]       & 0xf] >> 4); /* 22333334 */
    out[2] = (NIBBLE_TO_GCR[ in[1]       & 0xf] << 4) | (NIBBLE_TO_GCR[(in[2] >> 4) & 0xf] >> 1); /* 44445555 */
    out[3] = (NIBBLE_TO_GCR[(in[2] >> 4) & 0xf] << 7) | (NIBBLE_TO_GCR[ in[2]       & 0xf] << 2) | (NIBBLE_TO_GCR[(in[3] >> 4) & 0xf] >> 3); /* 56666677 */
    out[4] = (NIBBLE_TO_GCR[(in[3] >> 4) & 0xf] << 5) |  NIBBLE_TO_GCR[ in[3]       & 0xf]; /* 77788888 */
}

/* Transwarp encoding utility functions */

void
generate_gcr_decoding_table(const unsigned char nibble_to_gcr[], int8_t gcr_to_nibble[])
{
    for (int i = 0; i < 32; ++i) {
        gcr_to_nibble[i] = -(i + 1);
    }

    for (int i = 0; i < 16; ++i) {
        gcr_to_nibble[nibble_to_gcr[i]] = i;
    }
}

unsigned char
even_bits(unsigned char value)
{
    return (((value >> 6) & 1) << 3)
           | (((value >> 4) & 1) << 2)
           | (((value >> 2) & 1) << 1)
           | (((value >> 0) & 1) << 0);
}

unsigned char
odd_bits(unsigned char value)
{
    return (((value >> 7) & 1) << 3)
           | (((value >> 5) & 1) << 2)
           | (((value >> 3) & 1) << 1)
           | (((value >> 1) & 1) << 0);
}
bool
decode_5_bytes_gcr(const int8_t decoding_map[], const unsigned char *in, unsigned char *out)
{
    unsigned char gcr_hi;
    unsigned char gcr_lo;

    gcr_hi = in[0] >> 3;
    gcr_lo = ((in[0] & 0x7) << 2) | (in[1] >> 6);
    int out0 = (decoding_map[gcr_hi] << 4) | decoding_map[gcr_lo];

    gcr_hi = (in[1] & 0x3e) >> 1;
    gcr_lo = ((in[1] & 0x1) << 4) | (in[2] >> 4);
    int out1 = (decoding_map[gcr_hi] << 4) | decoding_map[gcr_lo];

    gcr_hi = ((in[2] & 0xf) << 1) | (in[3] >> 7);
    gcr_lo = (in[3] & 0x7c) >> 2;
    int out2 = (decoding_map[gcr_hi] << 4) | decoding_map[gcr_lo];

    gcr_hi = ((in[3] & 0x3) << 3) | (in[4] >> 5);
    gcr_lo = in[4] & 0x1f;
    int out3 = (decoding_map[gcr_hi] << 4) | decoding_map[gcr_lo];

    out[0] = out0;
    out[1] = out1;
    out[2] = out2;
    out[3] = out3;

    return (out0 | out1 | out2 | out3) >= 0;
}

int
decode_gcr_block(const int8_t decoding_map[], const unsigned char *encoded, unsigned char decoded[])
{
    bool ok = decode_5_bytes_gcr(decoding_map, encoded, decoded);
    decoded[0] = decoded[1];
    decoded[1] = decoded[2];
    decoded[2] = decoded[3];

    int computed_checksum = decoded[0] ^ decoded[1] ^ decoded[2];

    int i = 5;
    for (int j = 3; i < 320; i += 5, j += 4) {
        ok &= decode_5_bytes_gcr(decoding_map, encoded + i, decoded + j);
        computed_checksum ^= decoded[j] ^ decoded[j + 1] ^ decoded[j + 2] ^ decoded[j + 3];
    }

    unsigned char last_byte[4];
    ok &= decode_5_bytes_gcr(decoding_map, encoded + i, last_byte);
    decoded[255] = last_byte[0];

    computed_checksum ^= last_byte[0];
    computed_checksum = ok ? computed_checksum : -1;

    return ok ? computed_checksum : -1;
}
