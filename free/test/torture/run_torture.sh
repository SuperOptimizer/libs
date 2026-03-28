#!/bin/sh
# Run GCC C torture tests against free-cc
# Usage: ./test/torture/run_torture.sh [--verbose]

PROJ="/home/forrest/CLionProjects/free"
CC="${CC:-$PROJ/build/free-cc}"
CFLAGS_FREE="-I $PROJ/src/libc/include"
CC_REF="${CC_REF:-gcc}"
CFLAGS_REF="-std=gnu89 -w"
MLIMIT="${MLIMIT:-10485760}"  # 10 GB - compiler arena mmaps 8 GB virtual
PASS=0
FAIL=0
SKIP=0
CRASH=0
MISMATCH=0
VERBOSE=0
CRASH_LOG="/tmp/torture_crashes.log"
MISMATCH_LOG="/tmp/torture_mismatches.log"

if [ "$1" = "--verbose" ]; then
    VERBOSE=1
fi

: > "$CRASH_LOG"
: > "$MISMATCH_LOG"

for f in "$PROJ"/test/torture/*.c; do
    name=$(basename "$f" .c)

    # Compile with gcc first (reference)
    timeout 10 $CC_REF $CFLAGS_REF -o /tmp/tort_ref_"$name" "$f" 2>/dev/null
    if [ $? -ne 0 ]; then
        SKIP=$((SKIP+1))
        [ $VERBOSE -eq 1 ] && echo "SKIP $name (gcc compile fail)"
        continue
    fi

    # Run reference
    timeout 5 /tmp/tort_ref_"$name" 2>/dev/null
    ref_rc=$?
    if [ $ref_rc -ge 124 ]; then
        SKIP=$((SKIP+1))
        rm -f /tmp/tort_ref_"$name"
        [ $VERBOSE -eq 1 ] && echo "SKIP $name (gcc timeout/signal)"
        continue
    fi

    # Compile with free-cc (full pipeline: cc -> as -> ld)
    # Use subshell with memory limit for compilation
    (ulimit -v "$MLIMIT" 2>/dev/null; exec timeout 30 $CC $CFLAGS_FREE -o /tmp/tort_free_"$name" "$f" 2>/tmp/tort_err_"$name")
    cc_rc=$?
    if [ $cc_rc -ne 0 ]; then
        err=$(head -3 /tmp/tort_err_"$name" 2>/dev/null)
        # Categorize the error
        category="UNKNOWN"
        case "$err" in
            *"parse"*|*"expected"*|*"undeclared"*|*"syntax"*|*"token"*)
                category="PARSE" ;;
            *"codegen"*|*"not implemented"*|*"unsupported"*|*"TODO"*)
                category="CODEGEN" ;;
            *"assemble"*|*"encode"*|*"unknown instruction"*|*"unknown mnemonic"*)
                category="ASM" ;;
            *"link"*|*"undefined symbol"*|*"reloc"*)
                category="LINK" ;;
            *)
                category="OTHER" ;;
        esac
        echo "${category}_CRASH $name: $err" >> "$CRASH_LOG"
        CRASH=$((CRASH+1))
        rm -f /tmp/tort_ref_"$name" /tmp/tort_err_"$name"
        [ $VERBOSE -eq 1 ] && echo "CRASH $name ($category): $err"
        continue
    fi

    # Run free-cc version (with 256 MB memory limit for test binary)
    (ulimit -v 262144 2>/dev/null; exec timeout 5 /tmp/tort_free_"$name" 2>/dev/null)
    free_rc=$?

    if [ "$ref_rc" = "$free_rc" ]; then
        PASS=$((PASS+1))
        [ $VERBOSE -eq 1 ] && echo "PASS $name"
    else
        echo "MISMATCH $name: gcc=$ref_rc free=$free_rc"
        echo "$name gcc=$ref_rc free=$free_rc" >> "$MISMATCH_LOG"
        MISMATCH=$((MISMATCH+1))
    fi

    rm -f /tmp/tort_ref_"$name" /tmp/tort_free_"$name" /tmp/tort_err_"$name"
done

echo ""
echo "=== GCC Torture Test Results ==="
echo "Pass:     $PASS"
echo "Mismatch: $MISMATCH"
echo "Crash:    $CRASH (free-cc compile failure)"
echo "Skip:     $SKIP (gcc compile failure or timeout)"
echo "Total:    $((PASS+MISMATCH+CRASH+SKIP))"
echo ""
echo "Crash log:    $CRASH_LOG"
echo "Mismatch log: $MISMATCH_LOG"

# Categorize crashes
if [ -s "$CRASH_LOG" ]; then
    echo ""
    echo "=== Crash Categories ==="
    for cat in PARSE CODEGEN ASM LINK OTHER UNKNOWN; do
        count=$(grep -c "^${cat}_CRASH" "$CRASH_LOG" 2>/dev/null || true)
        [ "$count" -gt 0 ] 2>/dev/null && echo "  $cat: $count"
    done
fi
