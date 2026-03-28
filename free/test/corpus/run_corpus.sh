#!/bin/sh
# Run all corpus tests (features, stress, conformance, pp, opt)
. "$(dirname "$0")/../testlib.sh"

for f in \
    test/corpus/features/*.c \
    test/corpus/stress/*.c \
    test/corpus/conformance/*.c \
    test/corpus/pp/*.c \
    test/corpus/opt/*.c; do
    [ -f "$f" ] && compile_and_test "$f"
done

test_summary
