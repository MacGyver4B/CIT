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

/* raw.c: track level images (GCR bit stream): G64 output today, planned:
   G71, reading G64/G71/NIB, G80/G82 for the IEEE drives */

#include "cit.h"

static const int
sectors_per_track[] = {
    /*  1-17 */ 21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    /* 18-24 */ 19,19,19,19,19,19,19,
    /* 25-30 */ 18,18,18,18,18,18,
    /* 31-35 */ 17,17,17,17,17,
    /* 36-52 */ 21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    /* 53-59 */ 19,19,19,19,19,19,19,
    /* 60-65 */ 18,18,18,18,18,18,
    /* 66-70 */ 17,17,17,17,17
};

/* Writes 16 bit value to file */
static size_t
write16(unsigned int value, FILE* f)
{
    char byte = value & 0xff;
    size_t bytes_written = fwrite(&byte, 1, 1, f);

    byte = (value >> 8) & 0xff;
    bytes_written += fwrite(&byte, 1, 1, f);

    return bytes_written;
}

/* Writes 32 bit value to file */
static size_t
write32(unsigned int value, FILE* f)
{
    size_t bytes_written = write16(value, f);
    bytes_written += write16(value >> 16, f);

    return bytes_written;
}

/* Writes image as G64 file */
int
generate_uniformat_g64(unsigned char* image, const char *imagepath)
{
    FILE* f = fopen(imagepath, "wb");

    size_t filepos = 0;

    static const char signature[] = "GCR-1541";
    filepos += fwrite(signature, 1, sizeof signature - 1, f);

    const char version = 0;
    filepos += fwrite(&version, 1, 1, f);

    const char num_tracks = 35 * 2;
    filepos += fwrite(&num_tracks, 1, 1, f);

    const unsigned int track_size = 7692; /* = track_bytes on tracks 1..17 */
    filepos += write16(track_size, f);

    const unsigned int table_size = num_tracks * 4;
    const unsigned int tracks_offset = (int)filepos + (table_size * 2);

    for (int track = 0; track < num_tracks; ++track) {
        unsigned int track_offset = 0;

        if ((track & 1) == 0) {
            track_offset = tracks_offset + ((track >> 1) * (2 + track_size));
        }

        filepos += write32(track_offset, f);
    }

    for (int track = 0; track < num_tracks; ++track) {
        unsigned int bit_rate = 0;

        if ((track & 1) == 0) {
            switch (sectors_per_track[track >> 1]) {
            case 21:
                bit_rate = 3;
                break;
            case 19:
                bit_rate = 2;
                break;
            case 18:
                bit_rate = 1;
                break;
            case 17:
                bit_rate = 0;
                break;
            }
        }

        filepos += write32(bit_rate, f);
    }

    const unsigned char sync[] = { 0xff, 0xff, 0xff, 0xff, 0xff };
    const char gap[] = { 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55 };
    char header_gcr[10];

    const unsigned int block_size =
        (sizeof sync)
        + (sizeof header_gcr)
        + (sizeof gap)
        + (sizeof sync)
        + 325; /* data */

    const char id[3] = { '2', 'A', '\0' };

    bool is_uniform = true;

    for (int track = 0; track < (num_tracks >> 1); ++track) {
        int track_bytes = 0;
        int num_sectors = sectors_per_track[track];
        switch (num_sectors) {
        case 21:
            track_bytes = 7692;
            break; /* = track_size */
        case 19:
            track_bytes = 7142;
            break;
        case 18:
            track_bytes = 6666;
            break;
        case 17:
            track_bytes = 6250;
            break;
        }

        filepos += write16(track_bytes, f);
        size_t track_begin = filepos;

        int data_bytes = num_sectors * block_size;
        int gap_size = (track_bytes - data_bytes) / num_sectors;
        if (gap_size < 0) {
            printf("\nERROR: Track too small for G64 output\n");
            fclose(f);

            return -1;
        }

        float average_gap_remainder = (((float) (track_bytes - data_bytes)) / num_sectors) - gap_size;
        if (average_gap_remainder >= 1.0f) {
            average_gap_remainder = 0.0f; /* 0..1 */
        }

        float remainder = 0.0f;
        for (int sector = 0; sector < num_sectors; ++sector) {
            unsigned int gap_bytes = gap_size;
            remainder += average_gap_remainder;
            if (remainder >= 0.5f) {
                remainder -= 1.0f;
                ++gap_bytes;
            }

            filepos += fwrite(sync, 1, sizeof sync, f);
            char header[8] = {
                0x08, /* header ID */
                (char) (sector ^ (track + 1) ^ id[1] ^ id[0]), /* checksum */
                (char) sector,
                (char) (track + 1),
                id[1],
                id[0],
                0x0f, 0x0f
            };

            encode_4_bytes_gcr(header, header_gcr);
            encode_4_bytes_gcr(header + 4, header_gcr + 5);

            filepos += fwrite(header_gcr, 1, sizeof header_gcr, f);
            filepos += fwrite(gap, 1, sizeof gap, f);

            filepos += fwrite(sync, 1, sizeof sync, f);

            char group[5];

            char checksum = image[0] ^ image[1] ^ image[2];
            char data[4] = { 0x07, (char) image[0], (char) image[1], (char) image[2] };
            encode_4_bytes_gcr(data, group);
            filepos += fwrite(group, 1, sizeof group, f);
            for (int i = 0; i < 0x3f; ++i) {
                data[0] = image[(i * 4) + 3];
                data[1] = image[(i * 4) + 4];
                data[2] = image[(i * 4) + 5];
                data[3] = image[(i * 4) + 6];
                encode_4_bytes_gcr(data, group);
                filepos += fwrite(group, 1, sizeof group, f);
                checksum ^= (data[0] ^ data[1] ^ data[2] ^ data[3]);
            }
            data[0] = image[0xff];
            data[1] = data[0] ^ checksum;
            data[2] = 0;
            data[3] = 0;
            encode_4_bytes_gcr(data, group);
            filepos += fwrite(group, 1, sizeof group, f);

            for (int i = gap_bytes; i > 0; --i) {
                filepos += fwrite(gap, 1, 1, f);
            }

            image += 0x0100;
        } /* for each sector */

        size_t tail_gap = track_bytes - filepos + track_begin;
        if (tail_gap > 0) {
            for (size_t i = tail_gap; i > 0; --i) {
                filepos += fwrite(gap, 1, 1, f);
            }

            is_uniform = false;
        }

        for (int i = (track_size - track_bytes); i > 0; --i) {
            filepos += fwrite(sync, 1, 1, f);
        }
    } /* for each track */

    fclose(f);

    if (!is_uniform) {
        printf("\nWARNING: \"%s\" is not UniFormAt'ed\n", imagepath);
    }

    return 0;
}
