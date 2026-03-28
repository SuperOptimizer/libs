#!/bin/sh
# Run kernel pattern tests with both gcc and free-cc, compare results
. "$(dirname "$0")/../testlib.sh"

echo "=== Kernel pattern tests (gcc reference) ==="
CC="gcc" CFLAGS="-std=gnu89 -w"
for f in test/kernel/*.c; do
    [ -f "$f" ] && compile_and_test "$f"
done
echo "gcc: pass=$_PASS fail=$_FAIL"

if [ -x ./build/free-cc ]; then
    _PASS=0; _FAIL=0; _SKIP=0; _TIMEOUT=0
    echo ""
    echo "=== Kernel pattern tests (free-cc) ==="
    CC="./build/free-cc" CFLAGS=""
    for f in test/kernel/*.c; do
        [ -f "$f" ] && compile_and_test "$f"
    done
    echo "free-cc: pass=$_PASS fail=$_FAIL"
fi

test_summary
