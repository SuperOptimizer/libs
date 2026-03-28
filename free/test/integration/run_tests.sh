#!/bin/sh
# Run all integration test cases through the free toolchain pipeline.
. "$(dirname "$0")/../testlib.sh"

CC="${CC:-./build/free-cc}"

for f in test/integration/cases/*.c; do
    [ -f "$f" ] && compile_and_test "$f"
done

test_summary
