#!/bin/bash
# run_llvm_tests.sh - Compile LLVM test suite files with both gcc and free-cc,
# compare results (exit codes and stdout output).
#
# Usage: ./run_llvm_tests.sh [path-to-free-cc]

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FREE_CC="${1:-$PROJECT_DIR/build/free-cc}"
LIBC_INC="$PROJECT_DIR/src/libc/include"
GCC="${GCC:-gcc}"
TMPDIR="${TMPDIR:-/tmp}/llvm-test-$$"
TIMEOUT=10

mkdir -p "$TMPDIR"
trap 'rm -rf "$TMPDIR"' EXIT

# Counters
total=0
gcc_compile_fail=0
free_compile_ok=0
free_compile_fail=0
free_run_ok=0
free_run_fail=0
free_output_match=0
free_output_mismatch=0
free_crash=0

# Lists for reporting
pass_list=""
fail_compile_list=""
fail_run_list=""
fail_output_list=""
crash_list=""
compile_err_summary=""

for src in "$SCRIPT_DIR"/*.c; do
    [ -f "$src" ] || continue
    base="$(basename "$src" .c)"
    total=$((total + 1))

    # Step 1: Compile with gcc (reference)
    gcc_bin="$TMPDIR/${base}_gcc"
    if ! $GCC -std=gnu89 -w -DSMALL_PROBLEM_SIZE -o "$gcc_bin" "$src" -lm 2>"$TMPDIR/${base}_gcc_err"; then
        gcc_compile_fail=$((gcc_compile_fail + 1))
        continue
    fi

    # Run gcc binary to get reference output
    gcc_out="$TMPDIR/${base}_gcc.out"
    gcc_exit=0
    timeout "$TIMEOUT" "$gcc_bin" > "$gcc_out" 2>&1 || gcc_exit=$?

    # Step 2: Compile with free-cc (single step: compile+assemble+link)
    free_bin="$TMPDIR/${base}_free"
    cc_err="$TMPDIR/${base}_cc_err"

    if ! "$FREE_CC" -I "$LIBC_INC" -DSMALL_PROBLEM_SIZE "$src" -o "$free_bin" 2>"$cc_err"; then
        free_compile_fail=$((free_compile_fail + 1))
        fail_compile_list="$fail_compile_list $base"
        err_line=$(head -1 "$cc_err" 2>/dev/null)
        compile_err_summary="$compile_err_summary
  $base: $err_line"
        continue
    fi

    free_compile_ok=$((free_compile_ok + 1))

    # Step 3: Run free binary
    free_out="$TMPDIR/${base}_free.out"
    free_exit=0
    timeout "$TIMEOUT" "$free_bin" > "$free_out" 2>&1 || free_exit=$?

    # Check for crash (signal)
    if [ "$free_exit" -gt 128 ]; then
        sig=$((free_exit - 128))
        free_crash=$((free_crash + 1))
        crash_list="$crash_list $base(sig=$sig)"
        free_run_fail=$((free_run_fail + 1))
        continue
    fi

    # Step 4: Compare exit codes
    if [ "$free_exit" -ne "$gcc_exit" ]; then
        free_run_fail=$((free_run_fail + 1))
        fail_run_list="$fail_run_list $base(gcc=$gcc_exit,free=$free_exit)"
        continue
    fi

    free_run_ok=$((free_run_ok + 1))

    # Step 5: Compare output
    if diff -q "$gcc_out" "$free_out" >/dev/null 2>&1; then
        free_output_match=$((free_output_match + 1))
        pass_list="$pass_list $base"
    else
        free_output_mismatch=$((free_output_mismatch + 1))
        fail_output_list="$fail_output_list $base"
    fi
done

echo ""
echo "=============================="
echo "LLVM Test Suite Results"
echo "=============================="
echo "Total test files:         $total"
echo "GCC compile failures:     $gcc_compile_fail (excluded from results)"
echo ""
echo "--- free-cc Results ---"
echo "Compile success:          $free_compile_ok / $((total - gcc_compile_fail))"
echo "Compile failures:         $free_compile_fail"
echo "Run success (exit match): $free_run_ok / $free_compile_ok"
echo "Run failures (exit diff): $((free_run_fail - free_crash))"
echo "Crashes (signal):         $free_crash"
echo "Output match:             $free_output_match / $free_run_ok"
echo "Output mismatch:          $free_output_mismatch"
echo ""
echo "FULL PASS (compile+run+output): $free_output_match / $((total - gcc_compile_fail))"
echo ""

if [ -n "$pass_list" ]; then
    echo "PASSED:$pass_list"
    echo ""
fi

if [ -n "$fail_compile_list" ]; then
    echo "COMPILE FAILURES:$fail_compile_list"
    if [ -n "$compile_err_summary" ]; then
        echo ""
        echo "Compile error details:$compile_err_summary"
    fi
    echo ""
fi

if [ -n "$crash_list" ]; then
    echo "CRASHES:$crash_list"
    echo ""
fi

if [ -n "$fail_run_list" ]; then
    echo "EXIT CODE MISMATCHES:$fail_run_list"
    echo ""
fi

if [ -n "$fail_output_list" ]; then
    echo "OUTPUT MISMATCHES:$fail_output_list"
    echo ""
fi

# Write machine-readable summary
cat > "$SCRIPT_DIR/results.txt" <<RESULTS
total=$total
gcc_compile_fail=$gcc_compile_fail
free_compile_ok=$free_compile_ok
free_compile_fail=$free_compile_fail
free_run_ok=$free_run_ok
free_run_fail=$free_run_fail
free_crash=$free_crash
free_output_match=$free_output_match
free_output_mismatch=$free_output_mismatch
RESULTS

exit 0
