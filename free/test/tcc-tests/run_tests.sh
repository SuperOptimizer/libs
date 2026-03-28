#!/bin/sh
# Run TCC test suite tests against gcc or free-cc.
# TCC tests: each .c file has a matching .expect file with expected stdout.
#
# Usage:
#   ./test/tcc-tests/run_tests.sh              # run with gcc
#   CC=./build/free-cc ./test/tcc-tests/run_tests.sh  # run with free-cc

TESTDIR="$(cd "$(dirname "$0")" && pwd)"
PROJDIR="$(cd "$TESTDIR/../.." && pwd)"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--w}"
TLIMIT="${TLIMIT:-10}"
CLIMIT="${CLIMIT:-30}"
MLIMIT="${MLIMIT:-10485760}"  # 10 GB for compiler; test binaries get 256 MB

PASS=0
FAIL=0
SKIP=0
TIMEOUT=0
TOTAL=0

REPORT="$TESTDIR/report.txt"

case "$CC" in
    */*)
        CC_DIR=$(dirname "$CC")
        CC_BASE=$(basename "$CC")
        CC="$(cd "$CC_DIR" && pwd)/$CC_BASE"
        ;;
esac

printf "TCC test suite runner\n"
printf "Compiler: %s\n" "$CC"
printf "Flags: %s\n" "$CFLAGS"
printf "=================================\n"

for f in "$TESTDIR"/*.c; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .c)
    TOTAL=$((TOTAL + 1))
    tmpbin="/tmp/tcc_test_$$_$name"
    expect_file="$TESTDIR/$name.expect"

    # Check for .args file with test-specific arguments
    args_file="$TESTDIR/$name.args"
    ARGS=""
    if [ -f "$args_file" ]; then
        ARGS=$(cat "$args_file")
    fi

    # Try to compile
    if (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$CLIMIT" $CC $CFLAGS -o "$tmpbin" "$f" -lm 2>/dev/null); then
        # Run the binary (with memory limit)
        actual_out=$( (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$TLIMIT" "$tmpbin" $ARGS) 2>&1)
        rc=$?

        if [ "$rc" = "124" ]; then
            printf "  TIMEOUT %s\n" "$name"
            TIMEOUT=$((TIMEOUT + 1))
            FAIL=$((FAIL + 1))
        elif [ -f "$expect_file" ]; then
            expected_out=$(cat "$expect_file")
            if [ "$actual_out" = "$expected_out" ]; then
                PASS=$((PASS + 1))
            else
                printf "  FAIL %s (output mismatch)\n" "$name"
                FAIL=$((FAIL + 1))
            fi
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
TCC test suite report
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
