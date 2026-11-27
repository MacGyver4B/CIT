#!/bin/sh
# formats.sh - quick round trip test per image format (part of "make check")
#
#   tests/formats.sh [cit]
#
# For every format: write files into a new image, validate it (-V), extract
# all files again (-X) and compare them byte by byte. A counter check flips
# one BAM bit (sector 0 of a track); -V must then fail.
# The same test was done once with the real drive DOS in VICE (8050/8250:
# validate ok, image unchanged, checksums ok), so cit's -V and -X stand in
# for the drive here.
# Also checks the disk id rule: D41 keeps up to 5 characters, all other
# formats take 2 characters plus A0 and the DOS version of the format.

CIT=${1:-./cit}
case "$CIT" in /*) ;; *) CIT="$(pwd)/$CIT" ;; esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 2

pass=0; fail=0
ok()  { pass=$((pass + 1)); }
bad() { echo "FAIL: $*"; fail=$((fail + 1)); }

head -c 254 /dev/urandom > s1
head -c $((30 * 254)) /dev/urandom > s30
head -c $((200 * 254)) /dev/urandom > s200

# offset of a block: blocks of all tracks before + sector, times 256
offset() { # track sector sectors-per-track-of-zones...  (zone list: last:sectors ...)
    t=$1; s=$2; shift 2; n=0; tr=1
    for z in "$@"; do
        last=${z%:*}; spt=${z#*:}
        while [ $tr -le "$last" ] && [ $tr -lt "$t" ]; do n=$((n + spt)); tr=$((tr + 1)); done
    done
    echo $(( (n + s) * 256 ))
}

# name image big-blocks id-offset id-expected bam-bitmap-offset
check_format() {
    name=$1; img=$2; big=$3; idoff=$4; idexp=$5; bamoff=$6
    head -c $((big * 254)) /dev/urandom > big
    rm -f "$img"
    if ! "$CIT" -n test -i "$ID" -f eins -w s1 -f zwei -w s30 -f drei -w s200 -f gross -w big "$img" > log 2>&1; then
        bad "$name: writing failed"; cat log; return
    fi
    if "$CIT" -V "$img" > log 2>&1; then ok; else bad "$name: -V failed"; cat log; fi
    mkdir x && cd x || exit 2
    "$CIT" -X '*' "../$img" > /dev/null 2>&1
    for p in "eins s1" "zwei s30" "drei s200" "gross big"; do
        set -- $p
        if cmp -s "$1"* "../$2"; then ok; else bad "$name: extracted $1 differs"; fi
    done
    cd .. && rm -rf x
    got=$(od -An -tx1 -j "$idoff" -N5 "$img" | tr -d ' \n')
    if [ "$got" = "$idexp" ]; then ok; else bad "$name: disk id $got, expected $idexp"; fi
    cp "$img" broken
    v=$(od -An -tu1 -j "$bamoff" -N1 broken | tr -d ' ')
    printf "\\$(printf %o $((v ^ 1)))" | dd of=broken bs=1 seek="$bamoff" conv=notrunc 2>/dev/null
    if "$CIT" -V broken > /dev/null 2>&1; then bad "$name: -V did not detect a wrong BAM"; else ok; fi
}

D64Z="17:21 24:19 30:18 35:17"
D80Z="39:29 53:27 64:25 77:23"
D82Z="$D80Z 116:29 130:27 141:25 154:23"

# ids are given in ASCII, cit stores PETSCII ("ab" = 41 42)
# D41: 5 character id kept; BAM 18/0, track 35 bitmap
ID=abcde check_format d41 t.d64 400 $(( $(offset 18 0 $D64Z) + 0xa2 )) 4142434445 \
    $(( $(offset 18 0 $D64Z) + 35 * 4 + 1 ))
# D71: id shortened to 2 characters; track 70 bitmap in 53/0
ID=abcde check_format d71 t.d71 1000 $(( $(offset 18 0 $D64Z) + 0xa2 )) 4142a03241 \
    $(( $(offset 53 0 $D64Z 52:21 59:19 65:18 70:17) + 34 * 3 ))
# D81: "3D"; track 80 bitmap in 40/2
ID=ab check_format d81 t.d81 2500 $(( $(offset 40 0 80:40) + 0x16 )) 4142a03344 \
    $(( $(offset 40 2 80:40) + 0x10 + 39 * 6 + 1 ))
# D80: track 77 bitmap in 38/3
ID=ab check_format d80 t.d80 1700 $(( $(offset 39 0 $D80Z) + 0x18 )) 4142a03243 \
    $(( $(offset 38 3 $D80Z) + 6 + 26 * 5 + 1 ))
# D82: big file reaches side 2; track 154 bitmap in 38/9
ID=ab check_format d82 t.d82 2200 $(( $(offset 39 0 $D82Z) + 0x18 )) 4142a03243 \
    $(( $(offset 38 9 $D82Z) + 6 + 3 * 5 + 1 ))

echo "formats: $pass ok, $fail failed"
[ $fail -eq 0 ]
