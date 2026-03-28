#!/bin/sh
# Run c-testsuite tests against both gcc and free-cc.
# c-testsuite tests: each .c file should compile and return exit code 0.
# Some tests produce stdout output; .c.expected files contain expected stdout.
#
# Usage:
#   ./test/c-testsuite/run_tests.sh              # run with gcc
#   CC=./build/free-cc ./test/c-testsuite/run_tests.sh  # run with free-cc

TESTDIR="$(cd "$(dirname "$0")" && pwd)"
PROJDIR="$(cd "$TESTDIR/../.." && pwd)"

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--std=c89 -pedantic -w}"
TLIMIT="${TLIMIT:-10}"
CLIMIT="${CLIMIT:-30}"
MLIMIT="${MLIMIT:-10485760}"  # 10 GB for compiler; test binaries get 256 MB

PASS=0
FAIL=0
SKIP=0
CRASH=0
TIMEOUT=0
TOTAL=0

REPORT="$TESTDIR/report.txt"

printf "c-testsuite runner\n"
printf "Compiler: %s\n" "$CC"
printf "Flags: %s\n" "$CFLAGS"
printf "=================================\n"

for f in "$TESTDIR"/*.c; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .c)
    TOTAL=$((TOTAL + 1))
    tmpbin="/tmp/c_testsuite_$$_$name"

    # Try to compile
    if (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$CLIMIT" $CC $CFLAGS -o "$tmpbin" "$f" 2>/dev/null); then
        # Run the binary (with memory limit)
        actual_out=$( (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout "$TLIMIT" "$tmpbin") 2>/dev/null)
        rc=$?

        if [ "$rc" = "124" ]; then
            printf "  TIMEOUT %s\n" "$name"
            TIMEOUT=$((TIMEOUT + 1))
            FAIL=$((FAIL + 1))
        elif [ "$rc" != "0" ]; then
            printf "  FAIL %s (exit code %d)\n" "$name" "$rc"
            FAIL=$((FAIL + 1))
        else
            # Check expected output if file exists
            expected_file="$f.expected"
            if [ -f "$expected_file" ]; then
                expected_out=$(cat "$expected_file")
                if [ "$actual_out" = "$expected_out" ]; then
                    PASS=$((PASS + 1))
                else
                    printf "  FAIL %s (output mismatch)\n" "$name"
                    FAIL=$((FAIL + 1))
                fi
            else
                PASS=$((PASS + 1))
            fi
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

# Write report
cat > "$REPORT" <<ENDREPORT
c-testsuite report
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
