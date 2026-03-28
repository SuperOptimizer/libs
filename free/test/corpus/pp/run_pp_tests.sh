#!/bin/sh
# Run preprocessor corpus tests with timeouts.
. "$(dirname "$0")/../../testlib.sh"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CC="${1:-${CC:-gcc}}"
CFLAGS="-std=c89 -pedantic -w"

for f in "$SCRIPT_DIR"/*.c; do
    [ -f "$f" ] && compile_and_test "$f" -I"$SCRIPT_DIR"
done

test_summary
