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
    name="$1${ALLOC:+ $ALLOC}"; img=$2; big=$3; idoff=$4; idexp=$5; bamoff=$6
    head -c $((big * 254)) /dev/urandom > big
    rm -f "$img"
    # shellcheck disable=SC2086
    if ! "$CIT" $ALLOC -n test -i "$ID" -f eins -w s1 -f zwei -w s30 -f drei -w s200 -f gross -w big "$img" > log 2>&1; then
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

# all checks once per allocation strategy
for ALLOC in "" "--alloc cc1541" "--alloc dos" "--alloc auto"; do
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
done

# --alloc dos must place every block like the drive. References: images that
# the real DOS wrote in VICE (4040, 1541, 1571, 1581, 8050, 8250) were byte
# identical to these cit images (unused tail of the last block ignored).
# Files f01..f13 of 1, 2, 3, 5, ... 233 and <big> blocks, each filled with
# one byte value (65 + n); <minus> bytes less than full blocks.
dos_ref() { # image big minus expected-cksum [strategy]
    rm -rf dosref && mkdir dosref && cd dosref || exit 2
    i=0; args=""
    for b in 1 2 3 5 8 13 21 34 55 89 144 233 "$2"; do
        i=$((i + 1)); n=$(printf "f%02d" $i)
        c=$(printf '\\%o' $((64 + i)))
        head -c $((254 * b - $3)) /dev/zero | tr '\000' "$(printf "$c")" > $n
        args="$args -f $n -w $n"
    done
    # shellcheck disable=SC2086
    "$CIT" --alloc "${5:-dos}" -n test -i ab $args "$1" > /dev/null 2>&1
    got=$(cksum < "$1" | cut -d' ' -f1)
    if [ "$got" = "$4" ]; then ok; else bad "--alloc ${5:-dos} $1: cksum $got, expected $4"; fi
    cd .. && rm -rf dosref
}
dos_ref t.d64 40 1 3056002060
dos_ref t.d71 600 1 648183459
dos_ref t.d81 2500 1 448102000
dos_ref t.d80 1300 0 1417827109
dos_ref t.d82 3000 0 3895675243

# --alloc auto has no drive to compare with; these sums only make sure that
# the planned layout does not change unnoticed (checked with -V and -X above)
dos_ref t.d64 40 1 4104602902 auto
dos_ref t.d71 600 1 756665532 auto
dos_ref t.d81 2500 1 3887907604 auto
dos_ref t.d80 1300 0 1680565862 auto
dos_ref t.d82 3000 0 1074286723 auto

# --from: image -> folder (x00) -> image of another format, contents, names,
# types and directory order must survive; source and target the same is refused
from_test() {
    rm -rf ft && mkdir ft && cd ft || exit 2
    head -c 500 /dev/urandom > a; head -c 9000 /dev/urandom > b; head -c 30000 /dev/urandom > c
    "$CIT" -n quelle -i qx -f boot -w a -f daten -T SEQ -w b -f prog -w c src.d82 > /dev/null 2>&1
    "$CIT" --from src.d82 out > /dev/null 2>&1
    if [ -f out/001_boot.p00 ] && [ -f out/002_daten.s00 ] && [ -f out/003_prog.p00 ]; then ok; else bad "--from image -> folder: x00 files missing"; fi
    "$CIT" --from out back.d64 > /dev/null 2>&1
    if "$CIT" -V back.d64 > /dev/null 2>&1; then ok; else bad "--from folder -> image: -V failed"; fi
    mkdir x && cd x && "$CIT" -X '*' ../back.d64 > /dev/null 2>&1
    if cmp -s boot.prg ../a && cmp -s daten.seq ../b && cmp -s prog.prg ../c; then ok; else bad "--from round trip: contents differ"; fi
    cd ..
    order=$("$CIT" back.d64 | grep -o '"[a-z]*"' | tr -d '"\n')
    if [ "$order" = "bootdatenprog" ]; then ok; else bad "--from round trip: directory order $order"; fi
    "$CIT" --from src.d82 conv.d80 > /dev/null 2>&1
    if "$CIT" conv.d80 | grep -q '"quelle *" qx 2c'; then ok; else bad "--from image -> image: name/id not taken over"; fi
    if "$CIT" --from src.d82 src.d82 > /dev/null 2>&1; then bad "--from with source = target must fail"; else ok; fi
    cd .. && rm -rf ft
}
from_test

echo "formats: $pass ok, $fail failed"
[ $fail -eq 0 ]
