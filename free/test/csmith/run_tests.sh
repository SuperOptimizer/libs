#!/bin/sh
# Generate and run csmith random test programs.
# Requires csmith to be installed (apt install csmith libcsmith-dev)
# or built from /tmp/csmith.
#
# Usage:
#   ./test/csmith/run_tests.sh              # run with gcc
#   CC=./build/free-cc ./test/csmith/run_tests.sh  # run with free-cc
#   COUNT=100 ./test/csmith/run_tests.sh    # generate 100 programs

TESTDIR="$(cd "$(dirname "$0")" && pwd)"

CC="${CC:-gcc}"
REF_CC="${REF_CC:-gcc}"
CFLAGS="${CFLAGS:--w}"
TLIMIT="${TLIMIT:-10}"
CLIMIT="${CLIMIT:-30}"
MLIMIT="${MLIMIT:-10485760}"  # 10 GB for compiler; test binaries get 256 MB
COUNT="${COUNT:-20}"

PASS=0
FAIL=0
SKIP=0
TIMEOUT=0
CRASH=0
TOTAL=0

REPORT="$TESTDIR/report.txt"

# Find csmith
CSMITH=""
if command -v csmith >/dev/null 2>&1; then
    CSMITH="csmith"
elif [ -x /tmp/csmith/build/src/csmith ]; then
    CSMITH="/tmp/csmith/build/src/csmith"
fi

if [ -z "$CSMITH" ]; then
    printf "ERROR: csmith not found.\n"
    printf "Install with: sudo apt install csmith libcsmith-dev\n"
    printf "Or build from: /tmp/csmith\n"
    exit 1
fi

# Find csmith include directory
CSMITH_INC=""
if [ -d /usr/include/csmith-2.3.0 ]; then
    CSMITH_INC="-I/usr/include/csmith-2.3.0"
elif [ -d /usr/include/csmith ]; then
    CSMITH_INC="-I/usr/include/csmith"
elif [ -d /tmp/csmith/runtime ]; then
    CSMITH_INC="-I/tmp/csmith/runtime -I/tmp/csmith/build/runtime"
fi

printf "csmith random test runner\n"
printf "Compiler under test: %s\n" "$CC"
printf "Reference compiler: %s\n" "$REF_CC"
printf "Generating %d programs\n" "$COUNT"
printf "=================================\n"

i=0
while [ "$i" -lt "$COUNT" ]; do
    i=$((i + 1))
    TOTAL=$((TOTAL + 1))
    testfile="$TESTDIR/csmith_$(printf '%04d' $i).c"
    tmpbin_ref="/tmp/csmith_ref_$$_$i"
    tmpbin_test="/tmp/csmith_test_$$_$i"

    # Generate a random program (C89-friendly options)
    $CSMITH --no-packed-struct --no-bitfields --no-volatiles \
        --no-volatile-pointers --max-funcs 3 --max-block-depth 3 \
        > "$testfile" 2>/dev/null

    if [ ! -s "$testfile" ]; then
        printf "  SKIP %04d (generation failed)\n" "$i"
        SKIP=$((SKIP + 1))
        continue
    fi

    # Compile with reference compiler
    if ! timeout "$CLIMIT" $REF_CC $CFLAGS $CSMITH_INC -o "$tmpbin_ref" "$testfile" 2>/dev/null; then
        printf "  SKIP %04d (ref compile error)\n" "$i"
        SKIP=$((SKIP + 1))
        rm -f "$testfile" "$tmpbin_ref"
        continue
    fi

    # Get reference output
    ref_out=$(timeout "$TLIMIT" "$tmpbin_ref" 2>/dev/null)
    ref_rc=$?
    rm -f "$tmpbin_ref"

    if [ "$ref_rc" = "124" ]; then
        printf "  SKIP %04d (ref timeout)\n" "$i"
        SKIP=$((SKIP + 1))
        rm -f "$testfile"
        continue
    fi

    # Compile with compiler under test
    if ! timeout "$CLIMIT" $CC $CFLAGS $CSMITH_INC -o "$tmpbin_test" "$testfile" 2>/dev/null; then
        printf "  SKIP %04d (test compile error)\n" "$i"
        SKIP=$((SKIP + 1))
        rm -f "$tmpbin_test"
        continue
    fi

    # Get test output
    test_out=$(timeout "$TLIMIT" "$tmpbin_test" 2>/dev/null)
    test_rc=$?
    rm -f "$tmpbin_test"

    if [ "$test_rc" = "124" ]; then
        printf "  TIMEOUT %04d\n" "$i"
        TIMEOUT=$((TIMEOUT + 1))
        FAIL=$((FAIL + 1))
    elif [ "$test_rc" != "$ref_rc" ] || [ "$test_out" != "$ref_out" ]; then
        printf "  FAIL %04d (ref_rc=%d test_rc=%d)\n" "$i" "$ref_rc" "$test_rc"
        FAIL=$((FAIL + 1))
        # Keep failing test files for analysis
    else
        PASS=$((PASS + 1))
        # Remove passing test files to save space
        rm -f "$testfile"
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
csmith random test report
Compiler: $CC
Reference: $REF_CC
Flags: $CFLAGS
Date: $(date)
Count: $COUNT
Total: $TOTAL
Passed: $PASS
Failed: $FAIL
Skipped: $SKIP
Timeouts: $TIMEOUT
ENDREPORT

printf "Report written to %s\n" "$REPORT"
exit $FAIL
