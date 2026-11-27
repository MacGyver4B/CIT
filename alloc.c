/*
 * alloc.c - block allocation strategies of cit (--alloc)
 *
 *   cc1541  as cc1541 (in cit.c, write_files): from track 1 upwards
 *   dos     as the drive's DOS: from the directory track outwards
 *   auto    planned: "*" file next to the directory track, large files on
 *           whole tracks from the outside, small files into the best fitting
 *           gap; both sides of a cylinder in a row on double sided formats
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#include "cit.h"

alloc_mode_t alloc_mode = ALLOC_CC1541;

/*
 * Block allocation as the Commodore DOS does it (INTTS/NXTTS in tstfnd of
 * DOS 2.1/2.6/2.7, 1571 and 1581, all the same apart from the parameters):
 *
 * First block of a file: the track nearest to the directory track with free
 * blocks, alternately below and above (dir-1, dir+1, dir-2, ...); on it the
 * first free sector from 0.
 * Next block: if the track still has free blocks, sector + increment; past
 * the end of the track the number of sectors and 1 more are subtracted; from
 * there the next free sector upwards, else from 0. A full track moves the
 * file one track further away from the directory track (the sector number is
 * kept); at the edge of the disk it continues next to the directory track on
 * the other side with sector 0. Double sided drives (1571, 8250) count the
 * tracks of both sides as one row 1..max.
 *
 * Not reproduced on purpose: when a file fills its last block exactly, the
 * 1541/4040 DOS allocates one more block and does not free it on close (a
 * lost block that only a validate frees again). cit writes the clean result.
 */
int
dos_free_blocks(image_type type, const unsigned char *image, int track)
{
    if (track == dirtrack(type) || track == fmt(type)->second_bam_track) {
        return 0;
    }
    return image[get_bam_count_offset(type, track)];
}

/* first free sector from start upwards, then from 0 (GETSEC) */
int
dos_getsec(image_type type, const unsigned char *image, int track, int start)
{
    for (int s = start; s < num_sectors(type, track); s++) {
        if (is_sector_free(type, image, track, s, 0, 0)) {
            return s;
        }
    }
    for (int s = 0; s < num_sectors(type, track); s++) {
        if (is_sector_free(type, image, track, s, 0, 0)) {
            return s;
        }
    }
    return -1;
}

bool
dos_first_block(image_type type, const unsigned char *image, int *track, int *sector)
{
    int dt = dirtrack(type);
    for (int r = 1; ; r++) {
        int t = dt - r;
        if (t > 0 && dos_free_blocks(type, image, t) > 0) {
            *track = t;
            break;
        }
        t = dt + r;
        if (t > (int)image_num_tracks(type)) {
            return false;
        }
        if (dos_free_blocks(type, image, t) > 0) {
            *track = t;
            break;
        }
    }
    *sector = dos_getsec(type, image, *track, 0);
    return *sector >= 0;
}

bool
dos_next_block(image_type type, const unsigned char *image, int *track, int *sector, int increment)
{
    int dt = dirtrack(type);
    int t = *track;
    int s = *sector;
    int passes = 3;
    while (dos_free_blocks(type, image, t) == 0) {
        if (t == dt) {
            return false;
        }
        if (t < dt) {
            if (--t == 0) {
                t = dt + 1;
                s = 0;
                if (--passes == 0) {
                    return false;
                }
            }
        } else {
            if (++t > (int)image_num_tracks(type)) {
                t = dt - 1;
                s = 0;
                if (--passes == 0) {
                    return false;
                }
            }
        }
    }
    s += increment;
    int n = num_sectors(type, t);
    if (s > n) {
        s -= n;
        if (s > 0) {
            s--;
        }
    }
    *track = t;
    *sector = dos_getsec(type, image, t, s);
    return *sector >= 0;
}

/* ------------------------------------------------------------------------ */
/* auto: plan all blocks before writing                                     */
/* ------------------------------------------------------------------------ */

/* sector after s on track t: + increment, DOS wrap rule, next free (DOS FNDNXT) */
static int
next_sector_on(image_type type, const unsigned char *image, int track, int s)
{
    s += fmt(type)->file_interleave;
    int n = num_sectors(type, track);
    if (s > n) {
        s -= n;
        if (s > 0) {
            s--;
        }
    }
    return dos_getsec(type, image, track, s);
}

/* the other side's track of the same cylinder, 0 = none */
static int
other_side(image_type type, int track)
{
    int st = side_tracks(type);
    if (fmt(type)->sides != 2) {
        return 0;
    }
    return (track <= st) ? track + st : track - st;
}

/* tracks from the outside inwards, both sides of a cylinder in a row */
static int
outer_order(image_type type, int *order)
{
    int n = 0;
    for (int c = 1; c <= side_tracks(type); c++) {
        order[n++] = c;
        if (other_side(type, c)) {
            order[n++] = other_side(type, c);
        }
    }
    return n;
}

/* tracks from the directory track outwards (like the DOS), both sides of a
   cylinder in a row; starts with the other side of the directory cylinder */
static int
near_order(image_type type, int *order)
{
    int dt = dirtrack(type);
    int n = 0;
    if (other_side(type, dt)) {
        order[n++] = other_side(type, dt);
    }
    for (int r = 1; r < side_tracks(type); r++) {
        int c[2] = { dt - r, dt + r };
        for (int k = 0; k < 2; k++) {
            if (c[k] >= 1 && c[k] <= side_tracks(type)) {
                order[n++] = c[k];
                if (other_side(type, c[k])) {
                    order[n++] = other_side(type, c[k]);
                }
            }
        }
    }
    return n;
}

/* fill blocks along a track order, using every free sector of a track */
static bool
fill_along(image_type type, unsigned char *work, const int *order, int norder,
           bool only_empty_tracks, ts_block *blocks, int *done, int count)
{
    int s = -1;
    for (int i = 0; i < norder && *done < count; i++) {
        int t = order[i];
        int free_blocks = dos_free_blocks(type, work, t);
        if (free_blocks == 0 || (only_empty_tracks && free_blocks < num_sectors(type, t))) {
            continue;
        }
        s = (s < 0) ? dos_getsec(type, work, t, 0) : next_sector_on(type, work, t, s);
        while (s >= 0 && *done < count) {
            blocks[*done].track = t;
            blocks[*done].sector = s;
            (*done)++;
            mark_sector(type, work, t, s, 0 /* not free */);
            if (dos_free_blocks(type, work, t) == 0) {
                break;
            }
            s = next_sector_on(type, work, t, s);
        }
    }
    return *done == count;
}

ts_block **
auto_plan(image_type type, const unsigned char *image, imagefile *files, int num_files, const int *num_blocks, int star_file)
{
    ts_block **plan = calloc(num_files, sizeof(ts_block *));
    unsigned char *work = malloc(image_size(type));
    int *order = malloc(sizeof(int) * (image_num_tracks(type) + 1));
    int *byorder = malloc(sizeof(int) * num_files);
    if (plan == NULL || work == NULL || order == NULL || byorder == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    memcpy(work, image, image_size(type));

    /* files in placing order: the "*" file first, then by size, largest
       first; equal sizes keep their order (insertion sort is stable) */
    int n = 0;
    for (int i = 0; i < num_files; i++) {
        if (num_blocks[i] <= 0) {
            continue;
        }
        if (i == star_file) {
            memmove(byorder + 1, byorder, n * sizeof(int));
            byorder[0] = i;
            n++;
            continue;
        }
        int k = n;
        while (k > 0 && byorder[k - 1] != star_file && num_blocks[byorder[k - 1]] < num_blocks[i]) {
            byorder[k] = byorder[k - 1];
            k--;
        }
        byorder[k] = i;
        n++;
    }

    for (int j = 0; j < n; j++) {
        int f = byorder[j];
        int count = num_blocks[f];
        int done = 0;
        plan[f] = malloc(sizeof(ts_block) * count);
        if (plan[f] == NULL) {
            fprintf(stderr, "ERROR: Memory allocation error\n");
            exit(-1);
        }
        bool ok;
        if (f == star_file) {
            /* next to the directory track, where the head is after reading it */
            int no = near_order(type, order);
            ok = fill_along(type, work, order, no, false, plan[f], &done, count);
        } else {
            /* 1. the smallest gap (partly used track) that holds the whole file,
               2. else the first empty track near the directory that holds it,
               3. else whole tracks from the outside, then whatever is left */
            int no = near_order(type, order);
            int gap = 0;
            int gap_free = 0;
            int empty = 0;
            for (int i = 0; i < no; i++) {
                int t = order[i];
                int fb = dos_free_blocks(type, work, t);
                if (fb < count) {
                    continue;
                }
                if (fb < num_sectors(type, t)) {
                    if (gap == 0 || fb < gap_free) {
                        gap = t;
                        gap_free = fb;
                    }
                } else if (empty == 0) {
                    empty = t;
                }
            }
            if (gap != 0) {
                ok = fill_along(type, work, &gap, 1, false, plan[f], &done, count);
            } else if (empty != 0) {
                ok = fill_along(type, work, &empty, 1, false, plan[f], &done, count);
            } else {
                int oo = outer_order(type, order);
                ok = fill_along(type, work, order, oo, true, plan[f], &done, count)
                     || fill_along(type, work, order, oo, false, plan[f], &done, count);
            }
        }
        if (!ok) {
            fprintf(stderr, "ERROR: Disk full, file %s (", files[f].alocalname);
            print_filename(stderr, files[f].pfilename);
            fprintf(stderr, ")\n");
            exit(-1);
        }
    }
    free(byorder);
    free(order);
    free(work);
    return plan;
}
