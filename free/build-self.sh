#!/bin/sh
# build-self.sh - Build free-cc with itself (self-hosting stage 1)
set -e

CC=./build/free-cc
CFLAGS="-std=gnu89 -Iinclude -Isrc/libc/include"
STAGE1=stage1

mkdir -p "$STAGE1"

# List of all compiler source files
SRCS="
src/cc/lex.c
src/cc/pp.c
src/cc/parse.c
src/cc/type.c
src/cc/gen.c
src/cc/cc.c
src/cc/opt.c
src/cc/opt_bb.c
src/cc/util.c
src/cc/c99.c
src/cc/c11.c
src/cc/c23.c
src/cc/ext_attrs.c
src/cc/ext_builtins.c
src/cc/ext_asm.c
src/cc/dwarf.c
src/cc/ir.c
src/cc/ir_codegen.c
src/cc/ir_print.c
src/cc/ir_serialize.c
src/cc/lto.c
src/cc/opt_dce.c
src/cc/opt_mem2reg.c
src/cc/opt_sccp.c
src/cc/regalloc.c
src/cc/pic.c
src/cc/gen_x86.c
src/cc/diag.c
"

FAILED=0

echo "=== Stage 1: Compiling with free-cc ==="
for f in $SRCS; do
    name=$(basename "$f" .c)
    echo "  Compiling $f..."
    if ! timeout 60 $CC $CFLAGS -c -o "$STAGE1/$name.o" "$f" 2>&1; then
        echo "  FAILED: $f"
        FAILED=$((FAILED + 1))
    fi
done

if [ $FAILED -ne 0 ]; then
    echo "=== $FAILED file(s) failed to compile ==="
    exit 1
fi

echo ""
echo "=== All files compiled. Attempting link... ==="

# Try linking with gcc (handles crt0, libc)
# -no-pie: free-cc doesn't generate PIC by default, so adrp references
# to extern symbols like stderr need static linking
if gcc -no-pie -o "$STAGE1/free-cc" "$STAGE1"/*.o -lc 2>&1; then
    echo "=== Stage 1 binary created: $STAGE1/free-cc ==="
else
    echo "=== Link failed. Dumping symbol info... ==="
    for o in "$STAGE1"/*.o; do
        echo "--- $(basename $o) ---"
        nm "$o" 2>/dev/null | grep -E ' [TtDdBb] ' | head -20
    done
    exit 1
fi

# Basic smoke test
echo ""
echo "=== Testing stage 1 binary ==="
echo 'int main(void){return 42;}' > /tmp/s1test.c
if timeout 10 "$STAGE1/free-cc" -S -o /tmp/s1test.s /tmp/s1test.c 2>&1; then
    echo "  Stage 1 can compile to assembly."
    if as -o /tmp/s1test.o /tmp/s1test.s 2>&1 && \
       gcc -o /tmp/s1test /tmp/s1test.o -lc -nostartfiles 2>&1; then
        /tmp/s1test || true
        RET=$?
        echo "  Stage 1 test program exited with: $RET"
        if [ "$RET" = "42" ]; then
            echo "=== SELF-HOSTING SUCCESS ==="
        else
            echo "=== Wrong exit code (expected 42, got $RET) ==="
        fi
    else
        echo "  Could not assemble/link stage 1 output"
    fi
else
    echo "  Stage 1 failed to compile test program"
fi
