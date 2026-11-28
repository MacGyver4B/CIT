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

# --convert: image -> folder (x00) -> image of another format, contents, names,
# types and directory order must survive; source and target the same is refused
from_test() {
    rm -rf ft && mkdir ft && cd ft || exit 2
    head -c 500 /dev/urandom > a; head -c 9000 /dev/urandom > b; head -c 30000 /dev/urandom > c
    "$CIT" -n quelle -i qx -f boot -w a -f daten -T SEQ -w b -f prog -w c src.d82 > /dev/null 2>&1
    "$CIT" --convert src.d82 out > /dev/null 2>&1
    if [ -f out/001_boot.p00 ] && [ -f out/002_daten.s00 ] && [ -f out/003_prog.p00 ]; then ok; else bad "--convert image -> folder: x00 files missing"; fi
    "$CIT" --convert out back.d64 > /dev/null 2>&1
    if "$CIT" -V back.d64 > /dev/null 2>&1; then ok; else bad "--convert folder -> image: -V failed"; fi
    mkdir x && cd x && "$CIT" -X '*' ../back.d64 > /dev/null 2>&1
    if cmp -s boot.prg ../a && cmp -s daten.seq ../b && cmp -s prog.prg ../c; then ok; else bad "--convert round trip: contents differ"; fi
    cd ..
    order=$("$CIT" back.d64 | grep -o '"[a-z]*"' | tr -d '"\n')
    if [ "$order" = "bootdatenprog" ]; then ok; else bad "--convert round trip: directory order $order"; fi
    "$CIT" --convert src.d82 conv.d80 > /dev/null 2>&1
    if "$CIT" conv.d80 | grep -q '"quelle *" qx 2c'; then ok; else bad "--convert image -> image: name/id not taken over"; fi
    if "$CIT" --convert src.d82 src.d82 > /dev/null 2>&1; then bad "--convert with source = target must fail"; else ok; fi
    cd .. && rm -rf ft
}
from_test

# REL files: data and a new index (side sectors, super side sector on D81/D82)
# for every format and strategy; -V checks that the index lists exactly the
# data blocks. The dos layout was compared with the real drives in VICE
# (1541, 1571, 1581, 8050, 8250): byte identical.
r00() { # file name record-length bytes byte-value
    { printf 'C64File\000'; printf '%-16s' "$2" | tr ' ' '\000'; printf '\000'
      printf "\\$(printf %o "$3")"
      head -c "$4" /dev/zero | tr '\000' "$(printf "\\$(printf %o "$5")")"; } > "$1"
}
rel_test() {
    rm -rf rt && mkdir -p rt/in && cd rt || exit 2
    r00 in/001_kunden.r00 KUNDEN 64 3200 65   # x00 names are PETSCII: "KUNDEN" lists as kunden
    for ext in d64 d71 d81 d80 d82; do
        for a in auto dos; do
            rm -f "t.$ext" && rm -rf x && mkdir x
            "$CIT" --alloc $a --convert in "t.$ext" > /dev/null 2>&1
            if "$CIT" -V "t.$ext" > /dev/null 2>&1; then ok; else bad "REL $ext $a: -V failed"; fi
            (cd x && "$CIT" -X '*' "../t.$ext" > /dev/null 2>&1)
            if [ -f "x/kunden[rs64].rel" ] && [ "$(wc -c < "x/kunden[rs64].rel")" -eq 3200 ]; then ok; else bad "REL $ext $a: -X result wrong"; fi
        done
    done
    # large REL with a super side sector (more than 720 data blocks)
    r00 in/002_gross.r00 GROSS 200 200000 66
    for ext in d81 d82; do
        rm -f "t.$ext"
        "$CIT" --convert in "t.$ext" > /dev/null 2>&1
        if "$CIT" -V "t.$ext" > /dev/null 2>&1 && "$CIT" "t.$ext" | grep -q '^796 *"gross" *rel'; then ok; else bad "large REL $ext: wrong"; fi
    done
    # too large for the 8050 (no super side sector, 720 data blocks at most)
    rm -f t.d80
    if "$CIT" --convert in t.d80 > /dev/null 2>&1; then bad "large REL on D80 must fail"; else ok; fi
    cd .. && rm -rf rt
}
rel_test

# directory art: --convert keeps every entry as it is (type byte, raw name,
# block count shown, entries without blocks, several entries for the same
# blocks, equal names); -V accepts entries that share blocks
art_test() {
    rm -rf at && mkdir at && cd at || exit 2
    head -c 3000 /dev/urandom > prog
    "$CIT" -n "#12demo#92" -i ab -T DEL -f "----------------" -L -f spiel -w prog \
        -T DEL -N -f "----------------" -L -B 99 -T DEL -f "#12 menue #92" -L -T 194 -f gesperrt -w prog \
        -f kopie -l spiel -T DEL -N -f "----------------" -L src.d64 > /dev/null 2>&1
    "$CIT" --convert src.d64 dst.d82 > /dev/null 2>&1
    if "$CIT" -V dst.d82 > /dev/null 2>&1; then ok; else bad "directory art: -V failed"; fi
    if [ "$("$CIT" src.d64 2>/dev/null | sed -n '4,10p')" = "$("$CIT" dst.d82 2>/dev/null | sed -n '4,10p')" ]; then ok; else bad "directory art: listing differs after --convert"; fi
    # unpacking: entries without blocks as x00 files without data, directory.pet
    # with the listing in PETSCII
    "$CIT" --convert src.d64 out > /dev/null 2>&1
    # (an x00 file without data is just its 26 byte header)
    if [ "$(wc -c < "out/001_----------------.d00")" -eq 26 ] && grep -q "99   " out/directory.pet; then ok; else bad "directory art: unpacking"; fi
    "$CIT" -m --convert out back.d64 > /dev/null 2>&1
    if [ "$("$CIT" back.d64 2>/dev/null | grep -c '"----------------" del')" -eq 3 ] && ! "$CIT" back.d64 2>/dev/null | grep -q directory; then ok; else bad "directory art: packing"; fi
    # --dirpos swaps two entries, the blocks stay
    cp src.d64 dp.d64
    "$CIT" -m --dirpos gesperrt 1 dp.d64 > /dev/null 2>&1
    if "$CIT" dp.d64 2>/dev/null | sed -n 4p | grep -q '"gesperrt"' && "$CIT" -V -m dp.d64 > /dev/null 2>&1; then ok; else bad "--dirpos"; fi
    if "$CIT" --dirpos nichtda 1 dp.d64 > /dev/null 2>&1; then bad "--dirpos with an unknown name must fail"; else ok; fi
    cd .. && rm -rf at
}
art_test

# hard disks D9060/D9090 (DOS 3.0): empty images byte identical to VICE,
# round trip with every strategy, --check clean
hd_test() {
    rm -rf hd && mkdir hd && cd hd || exit 2
    "$CIT" -n test -i ab e90.d90 > /dev/null 2>&1
    "$CIT" -D d60 -n test -i ab e60.d90 > /dev/null 2>&1
    if [ "$(cksum < e90.d90 | cut -d' ' -f1)" = 2958664800 ]; then ok; else bad "D9090: empty image differs from VICE"; fi
    if [ "$(cksum < e60.d90 | cut -d' ' -f1)" = 801741380 ]; then ok; else bad "D9060: empty image differs from VICE"; fi
    if "$CIT" e90.d90 | grep -q "^29162 blocks free"; then ok; else bad "D9090: blocks free"; fi
    head -c 3000 /dev/urandom > a; head -c 200000 /dev/urandom > b; head -c 40 /dev/urandom > c
    for img in t90.d90 t60.d60; do
        for alloc in cc1541 dos auto; do
            rm -f "$img" && rm -rf x && mkdir x
            "$CIT" --alloc $alloc -n hd -f a -w a -f b -w b -f c -w c "$img" > /dev/null 2>&1
            if "$CIT" -V "$img" > /dev/null 2>&1 && "$CIT" --check "$img" > /dev/null 2>&1; then ok; else bad "$img $alloc: -V or --check failed"; fi
            (cd x && "$CIT" -X '*' "../$img" > /dev/null 2>&1)
            if cmp -s x/a.prg a && cmp -s x/b.prg b && cmp -s x/c.prg c; then ok; else bad "$img $alloc: contents differ"; fi
        done
    done
    cd .. && rm -rf hd
}
hd_test

# the tools: --sums, --layout, --block/--poke, --diff, --check, --json
tools_test() {
    rm -rf tt && mkdir tt && cd tt || exit 2
    head -c 3000 /dev/zero | tr '\000' 'A' > a
    "$CIT" -q -n t -f a -w a -f b -w a k.d64 > /dev/null 2>&1
    if "$CIT" --sums k.d64 | grep -A1 '"a" .* 3000' | grep -q 'crc32 93aaf669'; then ok; else bad "--sums"; fi
    # standard check values of "123456789" and the CBM ROM sum with carry
    printf '123456789' > check.bin
    S=$("$CIT" --sums check.bin)
    for v in "crc32 cbf43926" "crc16/xmodem 31c3" "crc16/arc bb3d" "crc16/ccitt 29b1" "sum32 000001dd" "sum16 01dd" "xor \$31" "cbm \$de" "sum \$dd" \
             "md5   25f9e794323b453885f5181f1b624d0b" "sha1 f7c3bc1d808e04732adf679965ccc34ca7ae3441"; do
        if echo "$S" | grep -q "$v"; then ok; else bad "--sums $v"; fi
    done
    # a 4K part summing up to $d0 fits a ROM at $D000
    head -c 4095 /dev/zero > rom.bin; printf '\320' >> rom.bin; cat rom.bin rom.bin > rom2.bin
    if "$CIT" --sums rom.bin | grep -q 'cbm \$d0 (ROM at \$D000)'; then ok; else bad "--sums ROM hint"; fi
    if "$CIT" --sums rom2.bin | grep -q 'cbm per 4K: \$d0 (ROM at \$D000) \$d0'; then ok; else bad "--sums per 4K"; fi
    printf '\001\010\000\000' > t.prg
    if "$CIT" --sums t.prg | grep -q 'load \$0801'; then ok; else bad "--sums load address"; fi
    if "$CIT" --layout a k.d64 | grep -q '(12)'; then ok; else bad "--layout"; fi
    if "$CIT" --diff k.d64 k.d64 > /dev/null; then ok; else bad "--diff of an image with itself"; fi
    if "$CIT" --check k.d64 > /dev/null; then ok; else bad "--check of a clean image"; fi
    cp k.d64 bad.d64
    "$CIT" -q --poke 1/10:0=99,1 bad.d64
    if "$CIT" --block 1/10 bad.d64 | grep -q "^00: 63 01"; then ok; else bad "--poke/--block"; fi
    "$CIT" --check bad.d64 > out 2>&1
    if [ $? -eq 2 ] && grep -q "invalid block 99/1" out; then ok; else bad "--check of a broken chain"; fi
    if "$CIT" --diff k.d64 bad.d64 > out 2>&1; then bad "--diff must report the change"; else grep -q "block chain broken" out && ok || bad "--diff output"; fi
    if "$CIT" --json k.d64 | grep -q '"crc32": "93aaf669"'; then ok; else bad "--json"; fi
    cd .. && rm -rf tt
}
tools_test

# images of SD cards: format, copy in and out, folders, delete; fsck.fat
# checks the file system when it is installed
sd_test() {
    rm -rf sdt && mkdir sdt && cd sdt || return
    for size in 1M 32M 64M 2G; do
        "$CIT" --sd-new n$size.img $size "CIT TEST" > /dev/null || { bad "--sd-new $size"; continue; }
        if command -v fsck.fat > /dev/null; then
            fsck.fat -n n$size.img > /dev/null 2>&1 && ok || bad "fsck.fat after --sd-new $size"
        fi
    done
    "$CIT" --sd-info n2G.img | grep -q "FAT32" && ok || bad "--sd-new 2G must be FAT32"
    "$CIT" --sd-info n2G.img | grep -q "no partition table" && ok || bad "--sd-new without partition table"
    if "$CIT" --sd-new big.img 33G 2> /dev/null; then bad "--sd-new over 32 GB"; else ok; fi
    mkdir -p src/games/pet; head -c 174848 /dev/urandom > src/games/a.d64
    head -c 3000000 /dev/urandom > "src/games/pet/b c.prg"; : > src/games/empty.seq
    "$CIT" --sd-put n64M.img src/games > /dev/null && ok || bad "--sd-put"
    "$CIT" --sd-mkdir n64M.img /x /x/y > /dev/null && ok || bad "--sd-mkdir"
    "$CIT" --sd-put n64M.img --to /x/y src/games/a.d64 > /dev/null && ok || bad "--sd-put --to"
    "$CIT" --sd-dir n64M.img /games/pet | grep -q "3000000  b c.prg" && ok || bad "--sd-dir"
    mkdir out
    "$CIT" --sd-get n64M.img --to out /games /x/y/a.d64 > /dev/null && ok || bad "--sd-get"
    diff -r src/games out/games > /dev/null && cmp -s src/games/a.d64 out/a.d64 && ok || bad "--sd-put/--sd-get round trip"
    if "$CIT" --sd-del n64M.img /x 2> /dev/null; then bad "--sd-del of a folder that is not empty"; else ok; fi
    "$CIT" --sd-del n64M.img /x/y/a.d64 /x/y /x > /dev/null && ok || bad "--sd-del"
    if "$CIT" --sd-get n64M.img --to out /x 2> /dev/null; then bad "--sd-get of a deleted folder"; else ok; fi
    if command -v fsck.fat > /dev/null; then
        fsck.fat -n n64M.img > /dev/null 2>&1 && ok || bad "fsck.fat after copying and deleting"
    fi
    cd .. && rm -rf sdt
}
sd_test

echo "formats: $pass ok, $fail failed"
[ $fail -eq 0 ]
