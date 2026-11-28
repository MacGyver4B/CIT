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
    return bam_track_free(type, image, track);
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

/*
 * DOS 3.0 (D9060/D9090, tstfnd and frets): a sector number is head * 32 +
 * sector of the head, the BAM counts free blocks per head.
 *   NXTDS: if the current head of the track has free blocks, FNDNXT on it;
 *          else the next heads of the same track (in a circle); when all are
 *          full, one track further away from the directory track (the sector
 *          is kept; up from the directory track itself), at the edge the
 *          other side next to the directory track with sector 0. After 3
 *          passes the directory track is the last resort.
 *   FNDNXT: sector + increment; from 32 on, 32 and (unless that gives 0)
 *          1 more are subtracted; the next free sector of the head from
 *          there, else from 0.
 *   INTTS: the first block of every file alternately on directory track
 *          + 1 and - 1 (INTFLG is flipped on each call), sector 0, head 0.
 */
static int
dos3_head_free(image_type type, const unsigned char *image, int track, int head)
{
    int n = 0;
    for (int s = 0; s < 32; s++) {
        n += is_sector_free(type, image, track, head * 32 + s, 0, 0) ? 1 : 0;
    }
    return n;
}

/* GETSEC on one head: first free sector from start, else from 0; -1 = none */
static int
dos3_getsec(image_type type, const unsigned char *image, int track, int head, int start)
{
    for (int s = start; s < 32; s++) {
        if (is_sector_free(type, image, track, head * 32 + s, 0, 0)) {
            return head * 32 + s;
        }
    }
    for (int s = 0; s < 32; s++) {
        if (is_sector_free(type, image, track, head * 32 + s, 0, 0)) {
            return head * 32 + s;
        }
    }
    return -1;
}

static int
dos3_fndnxt(image_type type, const unsigned char *image, int track, int head, int sector, int increment)
{
    sector += increment;
    if (sector >= 32) {
        sector -= 32;
        if (sector != 0) {
            sector--;
        }
    }
    return dos3_getsec(type, image, track, head, sector);
}

static bool
dos3_nxtds(image_type type, const unsigned char *image, int *track, int *sector, int increment)
{
    int dt = dirtrack(type);
    int maxtrk = image_num_tracks(type) + 1;
    int heads = fmt(type)->heads;
    int t = *track;
    int head = *sector >> 5;
    int sec = *sector & 31;
    int tmphed = head;
    int passes = 3;
    for (;;) {
        if (dos3_head_free(type, image, t, head) > 0) {
            *track = t;
            *sector = dos3_fndnxt(type, image, t, head, sec, increment);
            return *sector >= 0;
        }
        head = (head + 1) % heads;
        if (head != tmphed) {
            continue;
        }
        if (t >= dt) {
            if (++t == maxtrk) {
                t = dt - 1;
                sec = 0;
                if (--passes == 0) {
                    break;
                }
            }
        } else {
            if (--t == 0) {
                t = dt + 1;
                sec = 0;
                if (--passes == 0) {
                    break;
                }
            }
        }
    }
    /* NXTERR: the directory track */
    for (head = 0; head < heads; head++) {
        if (dos3_head_free(type, image, dt, head) > 0) {
            *track = dt;
            *sector = dos3_fndnxt(type, image, dt, head, sec, increment);
            return *sector >= 0;
        }
    }
    return false;
}

static int dos3_intflg = 0;

/* the next free sector after sector on the same track, DOS 3.0 style
   (heads in a circle), -1 if the track is full */
int
dos3_next_on_track(image_type type, const unsigned char *image, int track, int sector, int increment)
{
    int head = sector >> 5;
    for (int i = 0; i < fmt(type)->heads; i++) {
        int h = (head + i) % fmt(type)->heads;
        if (dos3_head_free(type, image, track, h) > 0) {
            return dos3_fndnxt(type, image, track, h, sector & 31, increment);
        }
    }
    return -1;
}

bool
dos_first_block(image_type type, const unsigned char *image, int *track, int *sector)
{
    if (fmt(type)->heads) {
        dos3_intflg ^= 0xff;
        *track = dirtrack(type) + (dos3_intflg ? 1 : -1);
        *sector = 0;
        return dos3_nxtds(type, image, track, sector, fmt(type)->file_interleave);
    }

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
    if (fmt(type)->heads) {
        return dos3_nxtds(type, image, track, sector, increment);
    }
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
    if (fmt(type)->heads) {
        return dos3_next_on_track(type, image, track, s, fmt(type)->file_interleave);
    }
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
