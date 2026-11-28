# CIT - Commodore Image Tools #

CIT (program name `cit`) is going to be a tool for Commodore disk images of the IEEE drives
as well: D64 (2031/4040), D67 (2040), D80 (8050), D82 (8250/SFD-1001)
and D60/D90 (D9060/D9090 hard disks), with folder <-> image conversion
using x00 (P00/S00/U00/R00) files.

It is based on cc1541 by JackAsser, Krill, Claus and Björn Esser
(MIT license). The original Bitbucket repository of cc1541 is no longer
available; version 4.2 is preserved here unchanged as the first commit
(branch `cc1541-4.2`). The original README follows below.

Builds: GitHub Actions workflow "build" (started by hand) produces
Windows, Linux and macOS binaries; locally `make` and `make check`.
Visual Studio users can create a project from the sources; project files
are not kept in the repository.

## What CIT adds to cc1541 ##

### Image formats ###

| Format | Drive(s) | Tracks | Directory | Notes |
|---|---|---|---|---|
| D64 / D41 | 1541 (C64) | 35 (40 with -4/-5) | 18/1 | default for `.d64`, cc1541 behaviour |
| D64 / D40 | 2031, 4040 | 35 | 18/1 | `-D d40`: no Transwarp, strict disk id |
| D71 | 1571 | 70 | 18/1 | |
| D81 | 1581 | 80 | 40/3 | disk id completed to "3D" like a real 1581 |
| D80 | 8050 | 77 | 39/1 | BAM on 38/0 and 38/3 |
| D82 | 8250, SFD-1001 | 154 | 39/1 | BAM on 38/0, 38/3, 38/6, 38/9 |
| D90 (D9060) | D9060 | 153 x 4 heads | 76/10 | hard disk, DOS 3.0, see below |
| D90 (D9090) | D9090 | 153 x 6 heads | 76/10 | hard disk, DOS 3.0, see below |

The extension selects the format; `-D d40|d41|d64|d71|d81|d80|d82|d60|d90`
overrides it (case does not matter). VICE uses `.d90` for both hard disks:
an existing image tells by its size, a new `.d90` is a D9090 unless
`-D d60` (or `.d60`) is given. Empty D80/D82 images are byte
identical to images formatted by VICE.

**Disk id:** the 5 byte id field is "id A0 dos-version". The C64 D64
(D41) keeps the free 5 character id of cc1541. For all other formats
`-i` sets the 2 id characters (longer input is shortened, shorter padded
with A0) and the DOS version comes from the format.

### Hard disks D9060/D9090 ###

DOS 3.0 counts 153 cylinders (track 0-152) with 4 or 6 heads of 32
sectors; a sector number is head * 32 + sector of the head (128 or 192
"sectors" per track). Track 0 is part of the image: 0/0 points to the
directory (76/10), the header (76/20) and the BAM and holds the id; 0/1 is
the list of bad sectors. The BAM is a chain of blocks on track 1, 9, 17 ...
(D9090) or 1, 13, 25 ... (D9060), each for 8 or 12 tracks, with an entry of
5 bytes per track and head. Blocks free counts the directory track, unlike
the floppy DOS (29162 / 19441 on an empty disk).

Empty images are byte identical to VICE and, apart from the $AA $55 the
real DOS writes into free blocks, to its N command. `--alloc dos` follows
DOS 3.0: the first block of every file alternately on track 77 and 75,
then sector + 10 within a head, the other heads of the track before the
next track; byte identical to files written by the real D9060 and D9090
in VICE. REL
files get a super side sector.

### Block allocation (`--alloc`) ###

* `cc1541` - as cc1541: from track 1 upwards, sector interleave 10
  (D81: 1). Default for D41 and whenever Transwarp or a cc1541
  placement option is used.
* `dos` - exactly as the drive's DOS. Default when files are added to an
  existing image, so they end up where the drive would put them.
* `auto` - planned layout. Default for new images.

`dos` follows INTTS/NXTTS and NXDRBK in the DOS sources (tstfnd of DOS
2.1, 2.6, 2.7, the 1571 and the 1581 DOS; DOS 1 of the 2040 the same with
2 instead of 3 passes):

* first block of a file: the track nearest to the directory track with
  free blocks, alternately below and above; first free sector from 0
* next block: sector + increment; past the end of the track the number
  of sectors and 1 more are subtracted; the next free sector from there,
  else from 0
* full track: one track further away from the directory track, the
  sector number is kept; at the edge of the disk it continues next to
  the directory track on the other side with sector 0
* double sided drives (1571, 8250) count the tracks of both sides as one
  row of tracks
* sector increment for files: 1541/2031/4040 10, 1571 6, 1581 1,
  8050/8250 5 (`begin` of DOS 2.7; `init` with 10 is not built in);
  directory blocks: 3 (1581: 1)

Checked in VICE with the real drives (4040, 1541, 1571, 1581, 8050,
8250): 13 files written by the drive give byte identical images. Not
reproduced on purpose: when a file fills its last block exactly, the
1541/4040 DOS allocates one more block that it never frees (a validate
frees it again).

`auto`:

* the first directory entry of a new image (the `*` file) next to the
  directory track; on D71/D82 the other side of the directory cylinder
  comes first (head change, no seek)
* the other files by size, largest first; equal sizes keep their order,
  so the same input always gives the same image; the directory order
  is not changed
* a file that fits on one track goes into the smallest gap that holds
  it, else onto the first empty track near the directory track
* larger files take whole tracks from the outside inwards, both sides of
  a cylinder in a row on D71/D82
* the sector increment of the drive's DOS on every track

With `--alloc dos` or `auto`, the cc1541 placement options `-b -e -E -r
-c -x` are ignored with a warning; Transwarp, `-t` and `-d` change the
structure of the disk and switch to `cc1541`, also with a warning.

### Copying and converting (`--convert`) ###

The source is only read, a copy is always written; source and target the
same is refused.

* image -> new image: laid out anew with `auto`, also into another
  format; disk name and id of the source unless `-n`/`-i`
* image -> existing image: the files are added (`dos`)
* image -> folder: PC64 x00 files (P00/S00/U00/R00/D00), numbered in
  directory order (`001_name.p00`) so that the order survives; entries
  without blocks (directory art) as x00 files without data, entries for
  the same blocks as copies; `directory.pet` holds the directory listing
  as the drive sends it, PETSCII 1:1 with CR line ends, with what x00
  cannot hold (block counts shown, locked, unclosed) - for a PETSCII
  viewer, it is not read back
* folder -> image: x00 files with the name and type of their header;
  plain files as `-X` writes them (`name.prg`, `.seq`, `.usr`,
  `name[rs64].rel` with the record length); anything else as PRG named
  after the file; in file name order

A new target is a folder unless it has an image extension or `-D` is
given; an existing path tells itself.

Every directory entry is taken over as it is, so that directory art looks
the same: the type byte (DEL, locked ...), the 16 raw name bytes with
control codes, the block count shown, entries without blocks, several
entries for the same blocks (they share one copy) and equal names. Unclosed
entries keep only their directory entry. Not copied, with a warning: GEOS
files, broken block chains, used blocks that belong to no file, error
tables.

### Extracting with conversion (`-XA`) ###

`-XA pattern` extracts like `-X`, but as files to read and edit on the PC,
as far as the way back gives the very same bytes:

| type | `-X` | `-XA` |
|---|---|---|
| PRG | `name.prg` | BASIC: listing `name[la0801].bas`; else `.prg` |
| SEQ | `name.seq` | text `name.txt` if it is mostly text, else `.seq` |
| REL | `name[rs64].rel` | CSV `name[rs64].csv`, one line per record |
| USR | `name.usr` | `name.usr` |

Text: letters, digits, punctuation and space become ASCII (lowercase/
uppercase character set, `--charset upper` for the other one), CR is the
line end, every other byte is written as `{0xFF}`. PETSCII has no `{`, so
nothing gets mixed up.

* BASIC: CIT lists and tokenizes itself (token.c), in the notation of
  petcat (VICE), so listings of both tools can be read by both. The
  version follows from the load address and the tokens used ($0801 C64,
  $0401 PET, $1C01 C128 ...); another one than the first try goes into
  the name, e.g. `[blre4]`. The listing must give the same bytes back,
  else the file stays a PRG with a warning. A SYS line with machine code
  behind it stays a PRG ("SYS line followed by ... bytes of machine
  code"); machine code inside REM lines is kept as `{$xx}`.
* SEQ: from 90 % text bytes it is text, from 75 % text with a note, below
  that it stays `.seq` (`seq.text`, `seq.mixed` in `cit.ini`). Control
  codes (colours, cursor) do not count as text. `--seq ascii|utf8|raw`
  decides yourself; utf8 also writes the graphic characters (Unicode
  "Symbols for Legacy Computing", needs a font like C64 Pro Mono).
* REL: the fields of a record are split at CR, as `PRINT#` writes them;
  numbers stay text as they are in the file (` 1250 ` with its spaces).
  Bytes $00 that the DOS filled in at the end are left out, an unused
  record is an empty line. Separator `;` (Excel in German), `--csv-sep ,`
  or `tab`.
* `$00` as a separator of its own: `--seq-00` / `--rel-00` with `eol`
  (`sep` for REL: instead of CR, which then becomes `{0x0D}`) or a text
  like `"|"` or `"{EOR}"`. Use the same option on the way back.
* Line end: that of the system, `text.eol` in `cit.ini`, `--crlf`, `--lf`,
  `--cr`.

The way back: `--convert folder image`, or `-w` with these names, turn
`.bas`, `.txt`, `.csv` and `name[rsNN].rel` into PRG, SEQ and REL files
again (`-T` before `-w` switches that off and writes the file as it is).
A CSV without `[rsNN]` gets the record length of its longest line, so a
table saved from Excel or Access becomes a REL file; a line of nothing
but separators, as Excel writes it, is an unused record. Characters that
PETSCII does not have (umlauts) are refused with their line.

### Directory order (`--dirpos name n`) ###

Swaps the directory entry of a file with entry n; the blocks stay where
they are. n = 1 makes it the `*` file. Works on new and existing images;
for new files with `auto`, n = 1 also places the file next to the
directory track.

Also changed from cc1541: `-V` accepts several entries for the same blocks
(the start block was overwritten while marking the chain), and the filename
hash check for Krill's loader only runs on D41 images.

### REL files ###

A REL file is one unit of data and index. The data blocks are linked like
any other file; the index is a chain of side sectors, each listing up to
120 data blocks in order, with a super side sector in front on the 8250
(D82) and the 1581 (D81), even for small files. The 8050 has none, so its
REL files hold at most 720 data blocks. The directory entry points to the
index ($15/$16) and holds the record length ($17).

The index only holds block addresses, so it is never copied or unpacked
(R00 files and `name[rs64].rel` carry the data and the record length):
cit builds it anew whenever it writes a REL file. Validate (`-V`) checks
that the index lists exactly the data blocks; overwriting frees the index
with the data; undelete recognises a deleted REL file by its index and
restores it as REL.

Layout:

* `dos` (also used for `cc1541`, which never wrote REL files) - as the
  drive expands a REL file in one go: data 1 like any first block, side
  sector 0 and the super side sector as the next blocks after data 1,
  every data block after the previous one, side sector j once data
  120 * j + 2 is taken, as the next block after data 120 * j + 1; the byte
  behind the last record is $FF. Byte identical to files written by the
  1541, 1571, 1581, 8050 and 8250 in VICE.
* `auto` - one stream: super side sector, side sector 0, its 120 data
  blocks, side sector 1, ... so every side sector lies next to the data it
  lists.

### Planned ###

* D67 (2040), DNP, CMD containers, error tables, G64/G71/NIB in raw.c
* Transwarp: `-V`/`--check` message, decoding unencrypted files back to PRG
* `.zip`/`.7z` archives through 7-Zip
* PETSCII viewer and converter: screens with colour and cursor codes as
  ANSI/VT100 for the console, DOS ANSI (`.ans`) or HTML (one way only)
* CSV and text as UTF-8 with graphic characters for REL as well

### Looking into images ###

These only read the image (`--poke` changes it); without anything else to
do, cit stops after them.

* `--sums` - size and checksums of every file in directory order (REL:
  the data), entries without blocks, links and broken chains marked; PRG
  files with their load address. `cit --sums file...` takes any files, not
  only images: PRGs, ROM dumps, images as a whole (an image also lists its
  files). Every file gets all checksums:
  * CRC32 (zip, MAME and ROM lists), MD5, SHA-1
  * CRC-16/XMODEM ($1021, start 0), CRC-16/CCITT-FALSE ($1021, start
    $FFFF, often just "CRC16 CCITT") and CRC-16/ARC ($8005, ARC/LHA)
  * xor - the 1 byte checksum of CBM tape blocks and disk data blocks
  * cbm - the CBM ROM checksum: every byte added with the carry of the
    addition before, the last carry added at the end. A CBM ROM chip gives
    the high byte of its start address ($E0 for the C64 kernal, $C0 and $E0
    for the two 1541 ROMs, $D0 $E0 $F0 for the 4040). A sum that fits is
    marked "(ROM at $D000)"; a dump of several chips also shows the sum of
    every 2K, 4K and 8K part
  * sum32, sum16, sum - plain sum in 32, 16 and 8 bit
* `--layout name` - the blocks of the matching files (wildcards as `-X`) in
  chain order; REL files with super side sector, side sectors and whether
  the index matches
* `--block t/s` - hex dump of a block; `--poke t/s:offset=value[,...]` sets
  bytes (numbers decimal, or hex with `0x` or `$`), for test cases and
  patches; several are done in the order given
* `--diff other` - compares with another image part by part: header,
  directory entries, file contents (CRC32) and layout (first differing
  block), BAM per track, used blocks; free blocks and the buffer rest
  behind a file end are ignored. Exit code 1 if they differ.
* `--check` - everything that is wrong, sorted: problems (broken or looping
  chains, blocks of two files, REL index, directory chain), repairable
  (BAM counters, used blocks of no file, blocks in use but free in the BAM,
  unclosed files) and notes (directory art links, Transwarp data, block
  counts shown). Exit code 0 clean, 1 repairable, 2 problems. `-V` still
  only guards against changing a broken image.
* `--json` - format, header, blocks free, every entry (type, flags, size,
  crc32, blocks) and free blocks per track, for scripts or a GUI

### BASIC listings (`--basic`) ###

A replacement for petcat, the same code as `-XA` uses:

```
cit --basic list prog.prg [version]             listing to stdout
cit --basic crunch prog.bas out.prg [0801] [version]
cit --basic versions
```

Versions: `1p` (PET BASIC 1), `2` (C64, VIC-20, PET), `40` (PET BASIC 4),
`3` (C16, plus/4), `70`/`71` (C128), `lre2`/`lre4` (PET 2001 LRE: BASIC
2 or 4 with the graphics BASIC tokens $DB-$F2: init, map, pspace, plot,
dplot, chplot, chrori, chrsiz, lintyp, cscr, display, idplot, iplot,
cursor, mode, hcopy, tstp, tstb, icrcl, arcus, font, gfxbasic, caps,
drive). Without a version `list` takes the one that fits the load address
and knows every token. Checked against petcat with thousands of random
programs: the same bytes, the same listing. Differences on purpose: bytes
outside strings that would turn into a keyword when read back, and spaces
at the end of a line, are written as `{$xx}`, so nothing gets lost;
BASIC 7.0 has no BASIC 10 keywords behind SLOW, BASIC 1 has no GO.

More versions come from a file, `basic.tokens = file` in `cit.ini`:

```
[simons]          ; id, as [bsimons] in file names
base = 2          ; a built-in version to start from
load = 0801       ; load address it is tried for (optional)
cc = fred         ; token = keyword
```

### Images of SD cards and USB sticks ###

Images of FAT file systems up to 32 GB, as SD2IEC, petSD, Kung Fu Flash or
Pi1541 use them. cit handles the file system itself (FatFs, see
`fatfs/LICENSE.txt`): nothing is mounted, no administrator rights.

```
cit --sd-new   card.img 2G "GAMES"     new image, formatted
cit --sd-info  card.img                file system, size, free space
cit --sd-dir   card.img /games         list a folder
cit --sd-put   card.img --to /games giana.d64 demos   files and folders in
cit --sd-get   card.img --to backup /games            files and folders out
cit --sd-mkdir card.img /games/pet
cit --sd-del   card.img /games/old.d64 /games/old     files, empty folders
```

New images have no partition table: the file system starts at sector 0,
as on most USB sticks. FAT32 from 64 MB on, smaller ones FAT12/FAT16; sizes
as `64M` or `2G`. Images with a partition table are read and changed too
(the first partition). Names may be long and hold umlauts.

Copying the files of an image onto a card in use (it keeps its size and
format): `cit --sd-get card.img --to E:\ /` copies everything and
overwrites files of the same name. `--clean` deletes everything in the
target folder first, after showing how much and asking (`--yes` skips the
question). It only works on removable drives; `--force` allows other
folders, but never the root of the system drive or the home folder.

Reading and writing the card itself:

```
cit --sd-list                      removable drives: number, size, name, letters
cit --sd-read  E: card.img         the whole card into an image
cit --sd-write card.img E:         the image onto the card, then compared
```

Only removable drives (USB, SD, MMC) up to 32 GB are offered, the drive
the system runs from never. The drive can be given as `E:`, `1` or
`PhysicalDrive1` (Windows) or `/dev/sdb` (Linux). Before writing cit
shows the card and asks; `--yes` skips that for a frontend that asks
itself. After writing the card is read back and compared. Progress goes
to stdout, one line per percent (`Writing 1250/7600 MB (16%)`). Windows
locks and dismounts the volumes of the card while cit works on it, which
needs a command prompt "Run as administrator"; Linux needs root and
refuses a mounted card for writing.

Windows cannot mount such an image by itself (only CD images); OSFMount
(PassMark, free) can, and cit calls it:

```
cit --sd-mount   card.img        as S: (sd.letter), read/write
cit --sd-mount   card.img J:     this one
cit --sd-unmount J:
```

Without a letter cit takes `sd.letter` from `cit.ini`, else S:; if that is
in use, the first free letter from D: on. This runs `OSFMount.com -a -t file -f card.img -m J: -o rw,rem,logical`: a
removable drive, which is why Windows takes FAT without partition table
(an image with partition table is mounted with `-v 1`). OSFMount needs
administrator rights. Its path comes from `osfmount` in `cit.ini`, else
`C:\Program Files\OSFMount\OSFMount.com`, else PATH. Do not change a
mounted image with `--sd-put` and the like: two writers break the file
system. In the OSFMount window choose "Disk" instead of a RAM drive and
switch off "read-only", or the changes only stay in its cache.

### Settings (`cit.ini`) ###

A few settings that would otherwise have to be given every time. The first
file found is used: `--config file`, `cit.ini` next to the program
(portable use), `%APPDATA%\cit\cit.ini` (Windows) or
`~/.config/cit/cit.ini` (Linux, macOS). The command line always wins.

```
; tools
basic.tokens = C:\CBM\tokens.txt   ; more BASIC versions (--basic, -XA)
7z     = C:\Program Files\7-Zip\7z.exe   ; for .zip/.7z archives (planned)
osfmount = C:\Program Files\OSFMount\OSFMount.com   ; for --sd-mount;
                 ; this is the default, else searched in PATH
sd.letter = S                    ; drive letter for --sd-mount (default S)

; defaults
alloc.new      = auto            ; strategy for a new image
alloc.existing = dos             ; strategy when adding files
disk.name      = cit             ; disk name without -n
text.eol       = crlf            ; line end of text from -XA (default: system)
text.charset   = lower           ; lower or upper character set
csv.sep        = ;               ; CSV separator (or tab)
seq.text       = 90              ; SEQ is text from this percentage
seq.mixed      = 75              ; ... text with a note from this one
; seq.00       = eol             ; $00 in SEQ: eol or a text like | (default {0x00})
; rel.00       = sep             ; $00 in REL: sep or a text (default {0x00})
```

Unknown keys are reported with their line. CIT does not remember paths by
itself: the same command gives the same result everywhere.

### Tests ###

* `make check` - the cc1541 tests plus `tests/formats.sh`: round trip of
  every format and strategy (write, `-V`, `-X`, compare), disk id rule,
  a wrong BAM must be detected, reference sums for `dos` (confirmed by
  the real drives in VICE) and `auto`, `--convert` round trips, REL files
  in every format and strategy
* `make compare REF=<git ref>` - byte by byte comparison of 29 cases
  with another version, e.g. `cc1541-4.2`

---

# README #

Note: You can find a more extensive html documentation here:
https://acoustic-velocity.com/cc1541

This is cc1541, a tool for creating Commodore 1541 Floppy disk
images in D64, D71 or D81 format with custom sector interleaving
etc.  Also supports extended tracks 35-40 using either SPEED DOS or
DOLPHIN DOS BAM-formatting. Furthermore supports writing Transwarp
disk images for the fantastically fast loader by Krill.

Originally written by JackAsser, with improvements and fixes by
Krill, some more improvements by Claus and further improvements by
Björn Esser.

The program is provided under the terms of the MIT license, please
refer to the included LICENSE.txt file for its terms and conditions.

The public source code repository can be found here:
https://bitbucket.org/PTV_Claus/cc1541/src/master/

## Usage examples ##

* "cc1541 -h" shows the command line help
* "cc1541 image.d64" lists the content of image.d64
* "cc1541 -f program -w program.prg image.d64" adds the file
  program.prg to image.d64 (or creates the image if it does not
  exist yet) using the name "program"
* "cc1541 -f program1 -w program1.prg -f program2 -w program2.prg
  image.d64" adds two files under the names program1 and program2
* "cc1541 -s 4 -f program -w program.prg image.d64" writes a file
  with a dedicated sector interleave for a fastloader (the best
  value depends on the used fastloader and its configuration)
* "cc1541 -f game -W game.prg -f 'transwarp v0.86' -w transwarp.prg
  Transwarp-Game.d64" creates a Transwarp disk image with the
  required boot file.
* "cc1541 -T DEL -f ---------------- -L image.d64" creates a DEL
  entry as separator.

## Version history ##

v4.2

* -X switch added to allow extraction of files from images. For
  Transwarp files, the transwarp boot loader will be extracted instead.
* -p switch added to write arbitrary data directly at a certain place
  on the disk.
* -N now implies -m to avoid duplicate hash errors per default.
* -l now allows to use wildcards * and ?.
* Per default, complete track 53 is reserved for D71 now, just as in
  the original format routine. This can be overriden by -t during the
  creation of the D71 image. Thanks to Martin Thierer for the help!
* Hex escapes now allow both upper case and lower case.
* In verbose mode, filenames are printed with escape characters now.
* -a switch avoids hex escapes now where possible.
* Bugfix: The display of free blocks was utterly wrong for D71, thanks
  to Martin Thierer for the fix!
* Bugfix: Validate did not work for D71, thanks to Martin Thierer for
  the fix! It also works for D81 now.
* Bugfix: -c resulted in "disk full" error for non-D71, it is now
  rejected instead.
* Bugfix: validation would complain about looped t/s chains when it
  was actually a collision between different t/s chains.
* Bugfix: the default disk id "00 2a" now has a shift-space in the
  middle, just as in the original format routine.

v4.1

* Per default, Transwarp files will now be allocated such that
  less disk space is wasted. Old behaviour can still be enabled
  with -A 0.
* Commandline options -s and -b are now rejected for Transwarp
  files instead of being silently ignored.
* Unicode printout can now be configured with environment variable
  CC1541UNICODE, using same values as for -U switch.
* Bugfix: the last allocated track for Transwarp files was not
  checked to be free, sometimes resulting in unexpected write
  errors.
* Bugfix: mixing Transwarp and normal files would often leave
  parts of the disk unallocated, so that less data than expected
  would fit onto a disk.
* Bugfix: verbose file allocation printout would sometimes print
  garbage for filenames or block numbers

v4.0

* The default handling for large tail gaps has been removed, as it
  does not provide any advantage and was only there to be as close
  as possible to CBM DOS behaviour. In fact, it actually slows down
  fast loaders. This warrants a major version number increase.
* -H switch added to set BAM messages at BAM offset $ab
* -F with negative values now specifies a track skew
* -B cannot be used with Transwarp files anymore, as the loader
  relies on correct block sizes in the directory
* Bugfix: the BAM allocation for SPEED DOS and DOLPHIN DOS was
  wrong (mixed up between the two and also shifted by 4 bytes)
* Bugfix: Data from unused blocks could leak into the last block
  of a new file behind the actual file data

v3.4

* Support for Transwarp 0.86 and later
* Commandline option -R added for restoring deleted or formatted
  files
* -L switch added to create directory entries without writing a
  file
* -a switch added to print a cc1541 command line that re-creates
  the directory art of the given disk image
* -T now also allows a number as parameter
* Files with file type DEL will now actually be written, use
  -L if you do not want that
* Improved verbose allocation printout
* More robustness against invalid t/s links
* Added support for underscore in PETSCII filenames
* Bugfix: no endless loops for cyclic directory chains anymore

v3.3

* Transwarp encoding added
* Verbose mode displays file allocation on unmodified images
* Optimised non-standard interleave scheme
* Added unicode printout option for PETSCII, special thanks to
  Jonas Minnberg for helping with Linux support and inverse
  characters!
* Output file is not touched when no change is requested
* Bugfix: Fixed crash on write protected output file
* Bugfix: Spacing in directory printout was not correct when
  file size is more than 999 blocks

v3.2

* -N switch added to allow duplicate file names
* Trying to create loop file with the same name as the referenced
  file now throws an error, if not -N is also specified
* Changed default next file first sector on new track to 0 to align
  with Commodore DOS
* Interleave violations are now shown in block allocation printout
* Block allocation for loop files is not displayed anymore when
  referenced file was also written
* Bugfix: Fixed file index printout for block allocation
* Bugfix: Correct interleave is now displayed for loop files when
  referenced file was also written

v3.1

* Bugfix: overwriting files did not work correctly when there was a
  free slot in the directory before the file to overwrite
* Bugfix: Filetype, open and protected flags were not reset after
  writing a file
* Bugfix: default disk name and id were wrong in command line help

v3.0

* ASCII to PETSCII conversion added, this breaks backward
  compatibility (and therefore warrants a major version increase)!
* Support for D81 images
* Default printout is now a full directory similar to how it would be
  displayed on a Commodore machine
* -v switch added for verbose output of file and block allocation
* -M switch added to specify number of characters for filename hash
  calculation for latest Krill loader
* -m switch added to skip filename collision check
* -B switch added to allow setting the displayed file size
* -o switch added to prevent overwriting of existing files on an image
* -V switch added to validate images before editing them
* -T switch added to allow setting the file type
* -O switch added to allow setting the open flag
* -P switch added to allow setting the protected flag
* Hex escapes are now also allowed for disk name and ID
* When no disk file name is provided, only the base name of the input
  file is used as disk file name instead of the full path
* Bugfix: fixed memory access issue for filenames with 16 letters
* Bugfix: fixed endless loop when reaching track 53 on a D71
* Bugfix: fixed that (shadow) directory track would be used for data
  when the shadow directory is a neighbour of the actual directory
* Bugfix: G64 output is now an optional additional output using -g,
  avoiding the utterly broken reading of G64 files
* Bugfix: loop files have actual file size per default instead of 0
* Bugfix: printouts to stderr and stdout are more consistent now

v2.0

* The first version with a release number
* All existing modifications consolidated (hopefully)
* G64 output dependent on output file name instead of a source code
  define
* Converted to ANSI C99
* MSVC build files added
* getopt removed
* Simple test suite added
* Bugfix: hex escape was not considered for file overwrite detection
* Bugfix: first sector per track was ignored for track 1
* Bugfix: default sector interleave was ignored for first file
