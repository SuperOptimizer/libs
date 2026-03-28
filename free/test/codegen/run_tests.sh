#!/bin/sh
# Run all codegen correctness tests through both gcc and free-cc.
. "$(dirname "$0")/../testlib.sh"

CC="${CC:-gcc}"

for f in test/codegen/codegen_*.c; do
    [ -f "$f" ] && compile_and_test "$f"
done

test_summary
