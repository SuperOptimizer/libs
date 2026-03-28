#!/bin/sh
# Test compiling real Linux kernel source files with free-cc
#
# Usage: ./test/kernel/run_real_kernel.sh [/path/to/linux]
#
# Prerequisites:
#   git clone --depth=1 --filter=blob:none --sparse https://github.com/torvalds/linux.git /tmp/linux
#   cd /tmp/linux
#   git sparse-checkout set --skip-checks \
#     lib/ctype.c lib/string.c lib/sort.c lib/bsearch.c lib/hexdump.c \
#     lib/kasprintf.c lib/kstrtox.c lib/kstrtox.h lib/argv_split.c \
#     lib/cmdline.c lib/find_bit.c lib/bitmap.c lib/list_sort.c \
#     lib/rbtree.c lib/hweight.c lib/llist.c lib/parser.c lib/uuid.c \
#     lib/math/gcd.c lib/math/lcm.c lib/math/int_pow.c lib/math/int_sqrt.c \
#     lib/math/div64.c lib/math/rational.c lib/math/reciprocal_div.c \
#     lib/math/int_log.c lib/plist.c lib/bcd.c lib/clz_tab.c lib/bitrev.c \
#     lib/errname.c lib/clz_ctz.c lib/memweight.c lib/string_helpers.c \
#     lib/seq_buf.c lib/win_minmax.c lib/nlattr.c lib/glob.c \
#     lib/dec_and_lock.c lib/debug_locks.c lib/iomap_copy.c \
#     lib/bust_spinlocks.c lib/strnlen_user.c lib/strncpy_from_user.c \
#     lib/net_utils.c lib/dynamic_queue_limits.c lib/ratelimit.c \
#     lib/refcount.c lib/timerqueue.c lib/klist.c lib/usercopy.c \
#     lib/checksum.c lib/interval_tree.c lib/cpumask.c \
#     lib/flex_proportions.c lib/kfifo.c

set -e

CC=./build/free-cc
LINUX_DIR="${1:-/tmp/linux}"
STUB_INCLUDES=test/kernel/include
SCRIPT_DIR="$(dirname "$0")"

PASS=0
FAIL=0

compile_kernel_file() {
    src="$1"
    extra_includes="$2"
    out="/tmp/kernel_$(basename "$src" .c).o"
    name="$src"

    printf "  %-50s ... " "$name"
    cmd="$CC -nostdinc -std=gnu89"
    cmd="$cmd -I$STUB_INCLUDES"
    if [ -n "$extra_includes" ]; then
        cmd="$cmd -I$extra_includes"
    fi
    cmd="$cmd -ffreestanding -D__KERNEL__ -DCONFIG_64BIT -D__NO_FORTIFY"
    cmd="$cmd -c -o $out $src"

    if eval $cmd 2>/dev/null; then
        printf "PASS (%s bytes)\n" "$(wc -c < "$out")"
        PASS=$((PASS + 1))
    else
        printf "FAIL\n"
        eval $cmd 2>&1 | head -5 | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

compile_pattern_file() {
    src="$1"
    out="/tmp/kp_$(basename "$src" .c).o"
    name="$(basename "$src")"

    printf "  %-50s ... " "$name"
    cmd="$CC -nostdinc -std=gnu89"
    cmd="$cmd -I$STUB_INCLUDES"
    cmd="$cmd -ffreestanding -D__KERNEL__ -DCONFIG_64BIT -D__NO_FORTIFY"
    cmd="$cmd -c -o $out $src"

    if eval $cmd 2>/dev/null; then
        printf "PASS (%s bytes)\n" "$(wc -c < "$out")"
        PASS=$((PASS + 1))
    else
        printf "FAIL\n"
        eval $cmd 2>&1 | head -5 | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Compiling Linux kernel source files with free-cc ==="
echo "  Compiler: $CC"
echo "  Kernel:   $LINUX_DIR"
echo ""

if [ ! -d "$LINUX_DIR/lib" ]; then
    echo "ERROR: Linux source not found at $LINUX_DIR"
    echo "Clone with:"
    echo "  git clone --depth=1 --filter=blob:none --sparse \\"
    echo "    https://github.com/torvalds/linux.git $LINUX_DIR"
    exit 1
fi

echo "--- lib/ core (7) ---"
compile_kernel_file "$LINUX_DIR/lib/ctype.c"
compile_kernel_file "$LINUX_DIR/lib/string.c"
compile_kernel_file "$LINUX_DIR/lib/sort.c"
compile_kernel_file "$LINUX_DIR/lib/bsearch.c"
compile_kernel_file "$LINUX_DIR/lib/hexdump.c"
compile_kernel_file "$LINUX_DIR/lib/kasprintf.c"
compile_kernel_file "$LINUX_DIR/lib/hweight.c"

echo ""
echo "--- lib/ data structures (10) ---"
compile_kernel_file "$LINUX_DIR/lib/list_sort.c"
compile_kernel_file "$LINUX_DIR/lib/rbtree.c"
compile_kernel_file "$LINUX_DIR/lib/llist.c"
compile_kernel_file "$LINUX_DIR/lib/find_bit.c"
compile_kernel_file "$LINUX_DIR/lib/bitmap.c"
compile_kernel_file "$LINUX_DIR/lib/plist.c"
compile_kernel_file "$LINUX_DIR/lib/timerqueue.c"
compile_kernel_file "$LINUX_DIR/lib/klist.c"
compile_kernel_file "$LINUX_DIR/lib/interval_tree.c"
compile_kernel_file "$LINUX_DIR/lib/kfifo.c"

echo ""
echo "--- lib/ parsing (6) ---"
compile_kernel_file "$LINUX_DIR/lib/kstrtox.c"   "$LINUX_DIR/lib"
compile_kernel_file "$LINUX_DIR/lib/cmdline.c"
compile_kernel_file "$LINUX_DIR/lib/argv_split.c"
compile_kernel_file "$LINUX_DIR/lib/parser.c"
compile_kernel_file "$LINUX_DIR/lib/uuid.c"
compile_kernel_file "$LINUX_DIR/lib/glob.c"

echo ""
echo "--- lib/math/ (8) ---"
compile_kernel_file "$LINUX_DIR/lib/math/gcd.c"
compile_kernel_file "$LINUX_DIR/lib/math/lcm.c"
compile_kernel_file "$LINUX_DIR/lib/math/int_pow.c"
compile_kernel_file "$LINUX_DIR/lib/math/int_sqrt.c"
compile_kernel_file "$LINUX_DIR/lib/math/div64.c"
compile_kernel_file "$LINUX_DIR/lib/math/rational.c"
compile_kernel_file "$LINUX_DIR/lib/math/reciprocal_div.c"
compile_kernel_file "$LINUX_DIR/lib/math/int_log.c"

echo ""
echo "--- lib/ string & formatting (4) ---"
compile_kernel_file "$LINUX_DIR/lib/string_helpers.c"
compile_kernel_file "$LINUX_DIR/lib/seq_buf.c"
compile_kernel_file "$LINUX_DIR/lib/errname.c"
compile_kernel_file "$LINUX_DIR/lib/net_utils.c"

echo ""
echo "--- lib/ bit & byte manipulation (5) ---"
compile_kernel_file "$LINUX_DIR/lib/bcd.c"
compile_kernel_file "$LINUX_DIR/lib/bitrev.c"
compile_kernel_file "$LINUX_DIR/lib/clz_tab.c"
compile_kernel_file "$LINUX_DIR/lib/clz_ctz.c"
compile_kernel_file "$LINUX_DIR/lib/memweight.c"

echo ""
echo "--- lib/ synchronization & locking (4) ---"
compile_kernel_file "$LINUX_DIR/lib/dec_and_lock.c"
compile_kernel_file "$LINUX_DIR/lib/debug_locks.c"
compile_kernel_file "$LINUX_DIR/lib/ratelimit.c"
compile_kernel_file "$LINUX_DIR/lib/refcount.c"

echo ""
echo "--- lib/ networking (4) ---"
compile_kernel_file "$LINUX_DIR/lib/nlattr.c"
compile_kernel_file "$LINUX_DIR/lib/win_minmax.c"
compile_kernel_file "$LINUX_DIR/lib/dynamic_queue_limits.c"
compile_kernel_file "$LINUX_DIR/lib/checksum.c"

echo ""
echo "--- lib/ I/O & user space (5) ---"
compile_kernel_file "$LINUX_DIR/lib/iomap_copy.c"
compile_kernel_file "$LINUX_DIR/lib/bust_spinlocks.c"
compile_kernel_file "$LINUX_DIR/lib/strnlen_user.c"
compile_kernel_file "$LINUX_DIR/lib/strncpy_from_user.c"
compile_kernel_file "$LINUX_DIR/lib/usercopy.c"

echo ""
echo "--- lib/ misc (2) ---"
compile_kernel_file "$LINUX_DIR/lib/cpumask.c"
compile_kernel_file "$LINUX_DIR/lib/flex_proportions.c"

REAL_PASS=$PASS
REAL_FAIL=$FAIL

echo ""
echo "--- kernel-pattern test files ---"
for f in "$SCRIPT_DIR"/kp_*.c; do
    [ -f "$f" ] && compile_pattern_file "$f"
done

echo ""
echo "--- kernel_test files ---"
for f in "$SCRIPT_DIR"/kernel_test*.c; do
    [ -f "$f" ] && compile_pattern_file "$f"
done

echo ""
echo "=========================================="
echo "=== Real kernel files: $REAL_PASS passed, $REAL_FAIL failed ==="
echo "=== Total:             $PASS passed, $FAIL failed out of $((PASS + FAIL)) ==="
echo "=========================================="
exit $FAIL
