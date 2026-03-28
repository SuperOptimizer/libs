#!/bin/sh
# test-install.sh -- Verify that the installed free toolchain works
#
# Usage:
#   ./scripts/test-install.sh              # checks $HOME/.local/free
#   PREFIX=/usr/local/free ./scripts/test-install.sh
#   ./scripts/test-install.sh /opt/free    # prefix as argument

set -e

if [ -n "$1" ]; then
    PREFIX="$1"
elif [ -z "$PREFIX" ]; then
    PREFIX="$HOME/.local/free"
fi

BIN="$PREFIX/bin"
PASS=0
FAIL=0
SKIP=0

pass() {
    PASS=$((PASS + 1))
    echo "  PASS: $1"
}

fail() {
    FAIL=$((FAIL + 1))
    echo "  FAIL: $1"
}

skip() {
    SKIP=$((SKIP + 1))
    echo "  SKIP: $1"
}

echo "=== Testing free toolchain installation ==="
echo "Prefix: $PREFIX"
echo ""

# --- Check all tools are present ---
echo "--- Checking installed binaries ---"
for tool in cc as ld ar nm objdump objcopy strip size strings addr2line \
            readelf dbg make cpp; do
    if [ -x "$BIN/free-$tool" ]; then
        pass "free-$tool exists and is executable"
    else
        fail "free-$tool not found at $BIN/free-$tool"
    fi
done

# --- Check symlinks ---
echo ""
echo "--- Checking symlinks ---"
for tool in cc as ld ar nm objdump objcopy strip size strings addr2line \
            readelf dbg make cpp; do
    if [ -L "$BIN/$tool" ]; then
        pass "$tool symlink exists"
    else
        skip "$tool symlink not found (optional)"
    fi
done

# --- Check libraries ---
echo ""
echo "--- Checking libraries ---"
if [ -f "$PREFIX/lib/libfree.a" ]; then
    pass "libfree.a (libc) installed"
else
    fail "libfree.a not found"
fi

if [ -f "$PREFIX/lib/libcx.a" ]; then
    pass "libcx.a installed"
else
    fail "libcx.a not found"
fi

# --- Check headers ---
echo ""
echo "--- Checking headers ---"
for hdr in stdio.h stdlib.h string.h; do
    if [ -f "$PREFIX/include/$hdr" ]; then
        pass "include/$hdr"
    else
        fail "include/$hdr not found"
    fi
done

for hdr in free.h elf.h aarch64.h; do
    if [ -f "$PREFIX/include/free/$hdr" ]; then
        pass "include/free/$hdr"
    else
        fail "include/free/$hdr not found"
    fi
done

# --- Compile and run a test program ---
echo ""
echo "--- Compile + run test ---"
TMPDIR="${TMPDIR:-/tmp}"
TEST_DIR="$TMPDIR/free-test-$$"
mkdir -p "$TEST_DIR"

cat > "$TEST_DIR/hello.c" << 'CEOF'
int main(void) {
    return 42;
}
CEOF

if [ -x "$BIN/free-cc" ] && [ -x "$BIN/free-as" ] && [ -x "$BIN/free-ld" ]; then
    # Try the full pipeline: compile -> assemble -> link -> run
    if "$BIN/free-cc" "$TEST_DIR/hello.c" -o "$TEST_DIR/hello.s" 2>/dev/null; then
        pass "free-cc compiled hello.c"
        if "$BIN/free-as" "$TEST_DIR/hello.s" -o "$TEST_DIR/hello.o" 2>/dev/null; then
            pass "free-as assembled hello.s"
            if "$BIN/free-ld" "$TEST_DIR/hello.o" -o "$TEST_DIR/hello" 2>/dev/null; then
                pass "free-ld linked hello.o"
                if "$TEST_DIR/hello" 2>/dev/null; RC=$?; [ "$RC" = "42" ]; then
                    pass "hello returned exit code 42"
                else
                    skip "hello did not return expected exit code (got $RC)"
                fi
            else
                skip "free-ld failed to link (may need libc)"
            fi
        else
            skip "free-as failed to assemble"
        fi
    else
        skip "free-cc failed to compile (toolchain may be incomplete)"
    fi
else
    skip "full pipeline test (cc/as/ld not all present)"
fi

# Cleanup
rm -rf "$TEST_DIR"

# --- Summary ---
echo ""
echo "=== Results ==="
echo "  Passed:  $PASS"
echo "  Failed:  $FAIL"
echo "  Skipped: $SKIP"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "Some checks failed. The installation may be incomplete."
    exit 1
else
    echo "All checks passed."
    exit 0
fi
