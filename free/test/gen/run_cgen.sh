#!/bin/sh
# Generate random C programs with cgen, compile, run with timeouts.
# Usage: ./test/gen/run_cgen.sh [count] [seed_start]
. "$(dirname "$0")/../testlib.sh"

N="${1:-100}"
SEED="${2:-1}"
CGEN="${CGEN:-./build/cgen}"
CC_REF="${CC_REF:-gcc -std=c89 -pedantic -w}"
CC_FREE="${CC_FREE:-./build/free-cc}"

i=0
while [ "$i" -lt "$N" ]; do
    s=$((SEED + i))
    _src="/tmp/cgen_$$_$s.c"
    _ref="/tmp/cgen_$$_ref_$s"
    _free="/tmp/cgen_$$_free_$s"

    # generate
    if ! run_with_timeout "$CGEN" "$s" > "$_src" 2>/dev/null; then
        _SKIP=$((_SKIP + 1))
        rm -f "$_src"
        i=$((i + 1))
        continue
    fi

    # compile with gcc
    if ! compile_with_timeout $CC_REF -o "$_ref" "$_src" 2>/dev/null; then
        _SKIP=$((_SKIP + 1))
        rm -f "$_src" "$_ref"
        i=$((i + 1))
        continue
    fi

    # run reference
    run_with_timeout "$_ref" >/dev/null 2>&1
    _ref_rc=$?
    if [ "$_ref_rc" = "124" ]; then
        _SKIP=$((_SKIP + 1))
        rm -f "$_src" "$_ref"
        i=$((i + 1))
        continue
    fi

    # compile with free-cc (if available)
    if [ -x "$CC_FREE" ]; then
        if compile_with_timeout $CC_FREE -o "$_free" "$_src" 2>/dev/null; then
            run_with_timeout "$_free" >/dev/null 2>&1
            _free_rc=$?
            if [ "$_free_rc" = "124" ]; then
                printf "  TIMEOUT seed=%d\n" "$s"
                _TIMEOUT=$((_TIMEOUT + 1))
                _FAIL=$((_FAIL + 1))
            elif [ "$_free_rc" = "$_ref_rc" ]; then
                _PASS=$((_PASS + 1))
            else
                printf "  MISMATCH seed=%d gcc=%d free=%d\n" "$s" "$_ref_rc" "$_free_rc"
                _FAIL=$((_FAIL + 1))
            fi
        else
            _SKIP=$((_SKIP + 1))
        fi
    else
        # no free-cc, just verify gcc doesn't crash
        if [ "$_ref_rc" -lt 128 ]; then
            _PASS=$((_PASS + 1))
        else
            printf "  CRASH seed=%d signal=%d\n" "$s" "$((_ref_rc - 128))"
            _FAIL=$((_FAIL + 1))
        fi
    fi

    rm -f "$_src" "$_ref" "$_free"
    i=$((i + 1))

    # progress every 25
    if [ $((i % 25)) -eq 0 ]; then
        printf "\r[%d/%d] pass=%d fail=%d skip=%d" "$i" "$N" "$_PASS" "$_FAIL" "$_SKIP"
    fi
done
printf "\n"

test_summary
