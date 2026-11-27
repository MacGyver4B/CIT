#!/bin/sh
# Compares this build of cit with an older state (any git ref) byte for byte.
#
#   tests/compare.sh [ref] [binary]      default ref: cc1541-4.2, binary: ./cit
#   make compare REF=<ref>
#
# The reference is built in a temporary directory (git worktree). Every case
# runs once with each program in its own directory with the same input files;
# then the exit codes, the console output and all files in the directory
# (images, G64, extracted files) must be identical.
#
# KEEP=1 keeps the temporary directory for a closer look.
#
# Intended differences have to be kept out of the cases: e.g. the default
# disk name changed from "cc1541" to "cit", so every case sets -n.

REF=${1:-cc1541-4.2}
NEW=$(cd "$(dirname "${2:-./cit}")" && pwd)/$(basename "${2:-./cit}")
TOP=$(git rev-parse --show-toplevel) || exit 2
TMP=$(mktemp -d) || exit 2
[ -n "$KEEP" ] && echo "kept: $TMP" || trap 'git -C "$TOP" worktree remove --force "$TMP/ref" >/dev/null 2>&1; rm -rf "$TMP"' EXIT

# --- reference binary (cc1541 or cit, depending on the state)
git -C "$TOP" worktree add --detach "$TMP/ref" "$REF" >/dev/null 2>&1 || { echo "cannot check out $REF"; exit 2; }
if [ -f "$TMP/ref/cit.c" ]; then RB=cit; else RB=cc1541; fi
make -s -C "$TMP/ref" "$RB" >/dev/null 2>&1 || { echo "cannot build $REF"; exit 2; }
OLD="$TMP/ref/$RB"
[ -x "$NEW" ] || { echo "no binary $NEW"; exit 2; }

# --- input files: fixed content (the Transwarp boot file is part of the repo)
IN="$TMP/in"; mkdir -p "$IN"
TW="$TOP/transwarp v0.86.prg"
cp "$TW" "$IN/tw.prg"
head -c 3000 "$TW" > "$IN/small.prg"
cat "$TW" "$TW" "$TW" > "$IN/mid.prg"                       # 24 KB
cat "$TW" "$TW" "$TW" "$TW" "$TW" "$TW" "$TW" "$TW" > "$IN/big.prg"  # 65 KB
printf '\000' > "$IN/zero.bin"

# --- cases: P is replaced by the program; several commands separated by ;
N='-n TESTDISK -i 12#a02a'
CASES="
P $N -f AAA -w mid.prg -f BBB -w small.prg x.d64
P $N -4 -f AAA -w big.prg -f BBB -w big.prg -f CCC -w big.prg x.d64
P $N -5 -f AAA -w big.prg -f BBB -w big.prg -f CCC -w big.prg x.d64
P $N -S 7 -f AAA -w mid.prg -s 3 -f BBB -w mid.prg x.d64
P $N -F 3 -f AAA -w mid.prg -F -2 -f BBB -w mid.prg x.d64
P $N -b 5 -f AAA -w mid.prg -r 25 -f BBB -w mid.prg x.d64
P $N -e -f AAA -w small.prg -E -f BBB -w mid.prg x.d64
P $N -x -f AAA -w big.prg -f BBB -w big.prg -f CCC -w big.prg x.d64
P $N -t -u 3 -f AAA -w big.prg -f BBB -w big.prg -f CCC -w big.prg x.d64
P $N -d 19 -f AAA -w mid.prg x.d64
P $N -H HIDDEN#20MESSAGE -f AAA -w small.prg x.d64
P $N -T SEQ -f AAA -w small.prg -T USR -P -f BBB -w small.prg -O -f CCC -w small.prg x.d64
P $N -B 99 -f AAA -w small.prg -T DEL -f ---------------- -L -f LOOP -l AAA x.d64
P $N -f AAA -w small.prg x.d64; P -N -f AAA -w mid.prg x.d64; P -o -f AAA -w mid.prg x.d64
P $N -M 8 -f LOADERFILE1 -w small.prg -f LOADERFILE2 -w small.prg x.d64
P $N -m -M 4 -f ABCD1 -w small.prg -f ABCD2 -w small.prg x.d64
P $N -f GAME -W mid.prg -f MORE -W small.prg -f 'transwarp v0.86' -w tw.prg x.d64
P $N -4 -A 0 -K SECRET -f GAME -W big.prg -f 'transwarp v0.86' -w tw.prg x.d64
P $N -g out.g64 -f AAA -w mid.prg -f BBB -w small.prg x.d64
P $N -f AAA -w big.prg -f BBB -w big.prg x.d71
P $N -c -f AAA -w big.prg -c -f BBB -w big.prg x.d71
P $N -f AAA -w big.prg -f BBB -w small.prg x.d81
P $N -f AAA -w mid.prg -f BBB -w small.prg x.d64; P -V x.d64; P -v x.d64; P -a x.d64
P $N -f AAA -w mid.prg -f BBB -w small.prg x.d64; P -X AAA x.d64; P -X 'B*' x.d64
P $N -f AAA -w mid.prg -f BBB -w small.prg x.d64; P -p 18,1,2,zero.bin x.d64; P -R 1 x.d64
P $N -f AAA -w mid.prg -f BBB -w small.prg x.d81; P -p 40,3,2,zero.bin x.d81; P -R 3 x.d81
P $N -U 1 -f ABC#c1#c2 -w small.prg x.d64
P $N -f AAA -w nonexistent.prg x.d64
P $N -4 -f AAA -w small.prg x.d71
"

run() { # $1 program, $2 directory, $3 commands
    prog=$1
    mkdir -p "$2"; cp "$IN"/* "$2"/
    ( cd "$2" || exit 2
      echo "$3" | tr ';' '\n' | while read -r line; do
          [ -n "$line" ] || continue
          eval "set -- $(echo "$line" | sed 's/^ *P//')"
          "$prog" "$@" >> out.txt 2>&1
          echo "rc=$?" >> out.txt
      done )
}

n=0; f=0
echo "$CASES" | { while IFS= read -r c; do
    [ -n "$c" ] || continue
    n=$((n + 1))
    rm -rf "$TMP/a" "$TMP/b"
    run "$OLD" "$TMP/a" "$c"
    run "$NEW" "$TMP/b" "$c"
    if grep -q "not found" "$TMP/a/out.txt"; then echo "SCRIPT ERROR in case $n"; cat "$TMP/a/out.txt"; exit 2; fi
    if diff -r "$TMP/a" "$TMP/b" >/dev/null; then
        :
    else
        f=$((f + 1))
        echo "DIFF case $n: $c"
        diff -r "$TMP/a" "$TMP/b" | head -5
    fi
done
echo "compare with $REF: $n cases, $f different"
[ "$f" -eq 0 ]; }
