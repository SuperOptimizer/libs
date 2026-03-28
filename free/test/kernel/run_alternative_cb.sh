#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
src="$script_dir/kp_alternative_cb.c"
gcc_s=$(mktemp /tmp/kp_alternative_cb_gcc.XXXXXX.s)
free_s=$(mktemp /tmp/kp_alternative_cb_free.XXXXXX.s)

trap 'rm -f "$gcc_s" "$free_s"' EXIT

if ! gcc -std=gnu89 -ffreestanding -O2 -S -o "$gcc_s" "$src" >/dev/null 2>&1; then
    echo "gcc failed to compile the repro"
    exit 1
fi
if ! "$repo_dir/build/free-cc" -std=gnu89 -ffreestanding -O2 -S -o "$free_s" "$src" >/dev/null 2>&1; then
    echo "free-cc failed to compile the repro"
    exit 1
fi

grep -q '\.hword 123' "$gcc_s"
if ! grep -q '\.hword 123' "$free_s"; then
    echo "free-cc did not fold the cpucap immediate"
    sed -n '1,120p' "$free_s"
    exit 1
fi
if grep -q '\.hword x0' "$free_s"; then
    echo "free-cc emitted a register fallback for the cpucap immediate"
    sed -n '1,120p' "$free_s"
    exit 1
fi
