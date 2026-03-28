#!/bin/sh
# Run optimizer quality tests with timeouts.
. "$(dirname "$0")/../../testlib.sh"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Correctness ==="
for f in "$SCRIPT_DIR"/*.c; do
    [ -f "$f" ] && compile_and_test "$f"
done

echo ""
echo "=== Optimization effectiveness ==="
for f in "$SCRIPT_DIR"/*.c; do
    [ -f "$f" ] || continue
    _name=$(basename "$f" .c)
    compile_with_timeout gcc -std=c89 -w -O0 -S -o "/tmp/opt_noopt_$_name.s" "$f" 2>/dev/null
    compile_with_timeout gcc -std=c89 -w -O2 -S -o "/tmp/opt_opt_$_name.s" "$f" 2>/dev/null
    if [ -f "/tmp/opt_noopt_$_name.s" ] && [ -f "/tmp/opt_opt_$_name.s" ]; then
        _n=$(wc -l < "/tmp/opt_noopt_$_name.s")
        _o=$(wc -l < "/tmp/opt_opt_$_name.s")
        printf "  %s: %d -> %d lines\n" "$_name" "$_n" "$_o"
    fi
    rm -f "/tmp/opt_noopt_$_name.s" "/tmp/opt_opt_$_name.s"
done

test_summary
