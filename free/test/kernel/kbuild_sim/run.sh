#!/bin/sh
# Kbuild simulation test runner
# Simulates the kernel build process using the free toolchain
# Reports pass/fail at each step

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOPDIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILDDIR="$TOPDIR/build"

CC="$BUILDDIR/free-cc"
AS="$BUILDDIR/free-as"
LD="$BUILDDIR/free-ld"
AR="$BUILDDIR/free-ar"
NM="$BUILDDIR/free-nm"
OBJCOPY="$BUILDDIR/free-objcopy"
STRIP="$BUILDDIR/free-strip"

# Kernel-like flags
CFLAGS="-std=gnu89 -nostdinc -ffreestanding -D__KERNEL__ -ffunction-sections -fdata-sections -mgeneral-regs-only -c"
INCLUDES="-I$SCRIPT_DIR"

cd "$SCRIPT_DIR"

# Clean previous artifacts
rm -f *.o built-in.a vmlinux Image vmlinux.sym vmlinux.stripped

PASS=0
FAIL=0
TOTAL=0

step() {
    TOTAL=$((TOTAL + 1))
    DESC="$1"
    shift
    printf "  [%2d] %-50s " "$TOTAL" "$DESC"
    if OUTPUT=$("$@" 2>&1); then
        printf "PASS\n"
        PASS=$((PASS + 1))
        return 0
    else
        printf "FAIL\n"
        if [ -n "$OUTPUT" ]; then
            printf "       %s\n" "$OUTPUT" | head -10
        fi
        FAIL=$((FAIL + 1))
        return 1
    fi
}

echo "============================================"
echo "  Kbuild Simulation Test"
echo "  free toolchain integration test"
echo "============================================"
echo ""

# Verify tools exist
echo "--- Tool check ---"
for tool in "$CC" "$AS" "$LD" "$AR" "$NM" "$OBJCOPY" "$STRIP"; do
    if [ ! -x "$tool" ]; then
        echo "FATAL: $tool not found or not executable"
        exit 1
    fi
done
echo "  All tools found."
echo ""

# Step 1: Compile C files with kernel flags
echo "--- Step 1: Compile C files ---"
step "Compile fs.c (designated init, bitfields, fptrs)" \
    "$CC" $CFLAGS $INCLUDES -o fs.o "$SCRIPT_DIR/fs.c"

step "Compile list.c (container_of, list_head)" \
    "$CC" $CFLAGS $INCLUDES -o list.o "$SCRIPT_DIR/list.c"

step "Compile printk.c (variadic functions)" \
    "$CC" $CFLAGS $INCLUDES -o printk.o "$SCRIPT_DIR/printk.c"

step "Compile main.c (inline asm, barriers, sections)" \
    "$CC" $CFLAGS $INCLUDES -o main.o "$SCRIPT_DIR/main.c"

echo ""

# Step 2: Assemble .S file
echo "--- Step 2: Assemble ---"
step "Assemble head.S (macros, sysreg, barriers)" \
    "$AS" "$SCRIPT_DIR/head.S" -o head.o

echo ""

# Step 3: Create archive
echo "--- Step 3: Archive ---"
C_OBJS=""
for f in fs.o list.o printk.o main.o; do
    [ -f "$f" ] && C_OBJS="$C_OBJS $f"
done
if [ -n "$C_OBJS" ]; then
    step "Create built-in.a from .o files" \
        "$AR" rcs built-in.a $C_OBJS
else
    echo "  SKIP: No .o files to archive"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Step 4: Link with linker script
echo "--- Step 4: Link ---"
if [ -f "head.o" ] && [ -f "built-in.a" ]; then
    step "Link vmlinux with linker script" \
        "$LD" -T "$SCRIPT_DIR/vmlinux.lds" -o vmlinux \
              head.o --whole-archive built-in.a --no-whole-archive
else
    echo "  SKIP: Missing objects for linking"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Step 5: objcopy to raw binary
echo "--- Step 5: objcopy ---"
if [ -f "vmlinux" ]; then
    step "Create raw Image (objcopy -O binary)" \
        "$OBJCOPY" -O binary vmlinux Image
else
    echo "  SKIP: vmlinux not available"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Step 6: nm symbol listing
echo "--- Step 6: nm ---"
if [ -f "vmlinux" ]; then
    step "List symbols with nm" \
        sh -c "$NM -n vmlinux > vmlinux.sym"
else
    echo "  SKIP: vmlinux not available"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Step 7: strip
echo "--- Step 7: strip ---"
if [ -f "vmlinux" ]; then
    step "Strip debug symbols" \
        "$STRIP" --strip-debug -o vmlinux.stripped vmlinux
else
    echo "  SKIP: vmlinux not available"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Step 8: Run the linked binary (if possible)
echo "--- Step 8: Execute vmlinux ---"
if [ -f "vmlinux" ]; then
    step "Run vmlinux (kernel_main tests)" \
        ./vmlinux
else
    echo "  SKIP: vmlinux not available"
    TOTAL=$((TOTAL + 1))
    FAIL=$((FAIL + 1))
fi

echo ""

# Verify outputs
echo "--- Output verification ---"
if [ -f "vmlinux" ]; then
    VMLINUX_SIZE=$(wc -c < vmlinux)
    printf "  vmlinux:          %d bytes\n" "$VMLINUX_SIZE"
fi
if [ -f "Image" ]; then
    IMAGE_SIZE=$(wc -c < Image)
    printf "  Image:            %d bytes\n" "$IMAGE_SIZE"
fi
if [ -f "vmlinux.sym" ]; then
    SYM_COUNT=$(wc -l < vmlinux.sym)
    printf "  vmlinux.sym:      %d symbols\n" "$SYM_COUNT"
fi
if [ -f "vmlinux.stripped" ]; then
    STRIP_SIZE=$(wc -c < vmlinux.stripped)
    printf "  vmlinux.stripped: %d bytes\n" "$STRIP_SIZE"
fi
if [ -f "built-in.a" ]; then
    AR_SIZE=$(wc -c < built-in.a)
    printf "  built-in.a:       %d bytes\n" "$AR_SIZE"
fi

echo ""
echo "============================================"
echo "  Results: $PASS/$TOTAL passed, $FAIL failed"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
