#!/bin/sh
# testlib.sh - Shared test runner utilities for the free toolchain.
# Source this from test scripts: . "$(dirname "$0")/../testlib.sh"
#
# Provides:
#   run_with_timeout CMD [ARGS...] - run a command with timeout, sets $? to 124 on timeout
#   compile_and_test FILE         - compile a test .c file, run it, check EXPECTED exit code
#   TLIMIT                        - timeout in seconds (default 10)
#   CLIMIT                        - compilation timeout (default 30)

TLIMIT="${TLIMIT:-10}"
CLIMIT="${CLIMIT:-30}"
MLIMIT="${MLIMIT:-262144}"  # default 256 MB virtual memory (in KB for ulimit -v)
_PASS=0
_FAIL=0
_SKIP=0
_TIMEOUT=0
_OOM=0

# Apply memory limit to prevent runaway allocations from killing the system.
apply_memlimit() {
    ulimit -v "$MLIMIT" 2>/dev/null || true
}

# Run a command with timeout and memory limit. Returns 124 on timeout.
run_with_timeout() {
    (apply_memlimit; exec timeout "$TLIMIT" "$@")
}

# Compile with timeout and memory limit.
compile_with_timeout() {
    (apply_memlimit; exec timeout "$CLIMIT" "$@")
}

# Compile a test .c file, run it, check EXPECTED exit code.
# Usage: compile_and_test file.c [extra_cflags...]
# Reads /* EXPECTED: N */ from line 1.
compile_and_test() {
    _file="$1"
    shift
    _expected=$(head -1 "$_file" | sed 's|.*EXPECTED: \([0-9]*\).*|\1|')
    _name=$(basename "$_file" .c)
    _cc="${CC:-gcc}"
    _cflags="${CFLAGS:--std=c89 -pedantic -w}"
    _tmpbin="/tmp/free_test_$$_$_name"

    if compile_with_timeout $_cc $_cflags "$@" -o "$_tmpbin" "$_file" 2>/dev/null; then
        run_with_timeout "$_tmpbin" 2>/dev/null
        _got=$?
        if [ "$_got" = "124" ]; then
            printf "  TIMEOUT %s (>%ss)\n" "$_name" "$TLIMIT"
            _TIMEOUT=$((_TIMEOUT + 1))
            _FAIL=$((_FAIL + 1))
        elif [ "$_got" = "137" ] || [ "$_got" = "139" ]; then
            printf "  OOM/CRASH %s (signal %s)\n" "$_name" "$_got"
            _OOM=$((_OOM + 1))
            _FAIL=$((_FAIL + 1))
        elif [ "$_got" = "$_expected" ]; then
            _PASS=$((_PASS + 1))
        else
            printf "  FAIL %s (expected=%s got=%s)\n" "$_name" "$_expected" "$_got"
            _FAIL=$((_FAIL + 1))
        fi
        rm -f "$_tmpbin"
    else
        _rc=$?
        if [ "$_rc" = "124" ]; then
            printf "  TIMEOUT %s (compilation >%ss)\n" "$_name" "$CLIMIT"
            _TIMEOUT=$((_TIMEOUT + 1))
            _FAIL=$((_FAIL + 1))
        elif [ "$_rc" = "137" ] || [ "$_rc" = "139" ]; then
            printf "  OOM/CRASH %s (compilation signal %s)\n" "$_name" "$_rc"
            _OOM=$((_OOM + 1))
            _FAIL=$((_FAIL + 1))
        else
            printf "  SKIP %s (compile error)\n" "$_name"
            _SKIP=$((_SKIP + 1))
        fi
        rm -f "$_tmpbin"
    fi
}

# Print test summary and exit with failure count.
test_summary() {
    _total=$((_PASS + _FAIL + _SKIP))
    printf "\n%d tests: %d passed, %d failed, %d skipped" \
        "$_total" "$_PASS" "$_FAIL" "$_SKIP"
    if [ "$_TIMEOUT" -gt 0 ]; then
        printf ", %d timeouts" "$_TIMEOUT"
    fi
    if [ "$_OOM" -gt 0 ]; then
        printf ", %d oom/crash" "$_OOM"
    fi
    printf "\n"
    exit "$_FAIL"
}
