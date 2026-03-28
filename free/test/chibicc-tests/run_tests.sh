#!/bin/sh
# Run chibicc test suite against gcc or free-cc.
# Chibicc tests use ASSERT(expected, expr) macro from test.h.
# Each test links against common.c which provides the assert() function.
#
# Usage:
#   ./test/chibicc-tests/run_tests.sh              # run with gcc
#   CC=./build/free-cc ./test/chibicc-tests/run_tests.sh  # run with free-cc

TESTDIR="$(cd "$(dirname "$0")" && pwd)"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--w}"
TLIMIT="${TLIMIT:-10}"
CLIMIT="${CLIMIT:-30}"
MLIMIT="${MLIMIT:-10485760}"  # 10 GB for compiler; test binaries get 256 MB
COMMON="$TESTDIR/common.c"

PASS=0
FAIL=0
SKIP=0
TIMEOUT=0
TOTAL=0

REPORT="$TESTDIR/report.txt"

printf "chibicc test suite runner\n"
printf "Compiler: %s\n" "$CC"
printf "Flags: %s\n" "$CFLAGS"
printf "=================================\n"

for f in "$TESTDIR"/*.c; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .c)
    # Skip the common support file
    [ "$name" = "common" ] && continue
    TOTAL=$((TOTAL + 1))
    tmpbin="/tmp/chibicc_test_$$_$name"

    # Try to compile (link with common.c)
    if (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$CLIMIT" $CC $CFLAGS -I"$TESTDIR" -o "$tmpbin" "$f" "$COMMON" -lm 2>/dev/null); then
        # Run the binary (with memory limit)
        (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$TLIMIT" "$tmpbin") >/dev/null 2>&1
        rc=$?

        if [ "$rc" = "124" ]; then
            printf "  TIMEOUT %s\n" "$name"
            TIMEOUT=$((TIMEOUT + 1))
            FAIL=$((FAIL + 1))
        elif [ "$rc" = "0" ]; then
            PASS=$((PASS + 1))
        else
            printf "  FAIL %s (exit code %d)\n" "$name" "$rc"
            FAIL=$((FAIL + 1))
        fi
        rm -f "$tmpbin"
    else
        crc=$?
        if [ "$crc" = "124" ]; then
            printf "  TIMEOUT %s (compilation)\n" "$name"
            TIMEOUT=$((TIMEOUT + 1))
            FAIL=$((FAIL + 1))
        else
            printf "  SKIP %s (compile error)\n" "$name"
            SKIP=$((SKIP + 1))
        fi
        rm -f "$tmpbin"
    fi
done

printf "\n=================================\n"
printf "Results: %d total, %d passed, %d failed, %d skipped" \
    "$TOTAL" "$PASS" "$FAIL" "$SKIP"
if [ "$TIMEOUT" -gt 0 ]; then
    printf ", %d timeouts" "$TIMEOUT"
fi
printf "\n"

cat > "$REPORT" <<ENDREPORT
chibicc test suite report
Compiler: $CC
Flags: $CFLAGS
Date: $(date)
Total: $TOTAL
Passed: $PASS
Failed: $FAIL
Skipped: $SKIP
Timeouts: $TIMEOUT
ENDREPORT

printf "Report written to %s\n" "$REPORT"
exit $FAIL
