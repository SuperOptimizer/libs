#!/bin/sh
# Benchmark compilation and execution speed
CC_REF="${CC_REF:-gcc -std=c89 -O2}"
CC_FREE="${CC_FREE:-./build/free-cc}"

echo "=== Compilation Speed ==="
for f in test/corpus/perf/compile_*.c; do
    name=$(basename "$f" .c)
    t_ref=$(command time -f "%e" $CC_REF -o /tmp/perf_ref "$f" 2>&1 | tail -1)
    t_free=$(command time -f "%e" $CC_FREE -o /tmp/perf_free "$f" 2>&1 | tail -1)
    echo "  $name: gcc=${t_ref}s free=${t_free}s"
    rm -f /tmp/perf_ref /tmp/perf_free
done

echo ""
echo "=== Runtime Performance ==="
for f in test/corpus/perf/perf_*.c; do
    name=$(basename "$f" .c)
    expected=$(head -1 "$f" | sed 's|.*EXPECTED: \([0-9]*\).*|\1|')

    $CC_REF -o /tmp/perf_ref "$f" 2>/dev/null
    t_ref=$(command time -f "%e" /tmp/perf_ref 2>&1 | tail -1)
    ref_exit=$?

    if $CC_FREE -o /tmp/perf_free "$f" 2>/dev/null; then
        t_free=$(command time -f "%e" /tmp/perf_free 2>&1 | tail -1)
        free_exit=$?
        ratio=$(echo "scale=1; $t_free / $t_ref" | bc 2>/dev/null || echo "?")
        echo "  $name: gcc=${t_ref}s free=${t_free}s (${ratio}x)"
    else
        echo "  $name: gcc=${t_ref}s free=COMPILE_FAIL"
    fi
    rm -f /tmp/perf_ref /tmp/perf_free
done
