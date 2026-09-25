#!/bin/sh
set -e
cd "$(dirname "$0")" || exit 1

pick_cc() {
    probe=$(mktemp /tmp/zzdfcc.XXXXXX.c)
    probeo="$probe.out"
    printf '#include <stdio.h>\nint main(void){return 0;}\n' > "$probe"
    for cand in "$CC" /usr/bin/gcc /usr/bin/cc /mingw64/bin/gcc gcc cc clang; do
        [ -n "$cand" ] || continue
        if "$cand" -o "$probeo" "$probe" >/dev/null 2>&1; then
            rm -f "$probe" "$probeo"
            echo "$cand"
            return 0
        fi
    done
    rm -f "$probe" "$probeo"
    return 1
}

CC_USE=$(pick_cc) || { echo "No usable host compiler found."; exit 1; }
echo "host compiler: $CC_USE"

$CC_USE -O1 -DZZDF_TEST_BUILD -o test_memfs   test_memfs.c
$CC_USE -O1 -DZZDF_TEST_BUILD -o test_preload test_preload.c
$CC_USE -O1 -DZZDF_TEST_BUILD -o test_audio   test_audio.c
$CC_USE -O1 -DZZDF_TEST_BUILD -o test_midi    test_midi.c

fail=0
m=$(./test_memfs   | tail -1) || fail=1
p=$(./test_preload | tail -1) || fail=1
a=$(./test_audio   | tail -1) || fail=1
d=$(./test_midi    | tail -1) || fail=1
echo "MemFS     $m"
echo "Preload   $p"
echo "PCM ring  $a"
echo "MIDI ring $d"
if [ "$fail" != "0" ]; then echo "HOST TESTS FAILED"; exit 1; fi
echo "ALL HOST TESTS PASSED"
