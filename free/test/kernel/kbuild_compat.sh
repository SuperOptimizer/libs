#!/bin/sh
# Test Kbuild compatibility - verify free-cc responds correctly to
# the capability probes that Kbuild uses to configure the build.

CC="${CC:-./build/free-cc}"
PASS=0
FAIL=0

check() {
    desc="$1"
    shift
    if eval "$@" >/dev/null 2>&1; then
        echo "  PASS $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $desc"
        FAIL=$((FAIL + 1))
    fi
}

check_fail() {
    desc="$1"
    shift
    if eval "$@" >/dev/null 2>&1; then
        echo "  FAIL $desc (should have failed)"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS $desc (correctly rejected)"
        PASS=$((PASS + 1))
    fi
}

echo "=== Kbuild Compatibility Tests ==="
echo "CC=$CC"
echo ""

# Basic compilation
echo "--- Basic ---"
echo 'int main(void){return 0;}' > /tmp/kb_test.c
check "compile simple program" "timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "compile to assembly" "timeout 10 $CC -S -o /tmp/kb_test.s /tmp/kb_test.c"
check "compile and link" "timeout 10 $CC -o /tmp/kb_test /tmp/kb_test.c"

# Flags Kbuild uses
echo ""
echo "--- Flags ---"
check "-Wall" "timeout 10 $CC -Wall -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-Wextra" "timeout 10 $CC -Wextra -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-O2" "timeout 10 $CC -O2 -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-g" "timeout 10 $CC -g -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-c" "timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-S" "timeout 10 $CC -S -o /tmp/kb_test.s /tmp/kb_test.c"
check "-I/tmp" "timeout 10 $CC -I/tmp -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-DFOO=1" "echo 'int main(void){return FOO;}' > /tmp/kb_test2.c && timeout 10 $CC -DFOO=1 -c -o /tmp/kb_test.o /tmp/kb_test2.c"

# Preprocessor
echo ""
echo "--- Preprocessor ---"
check "-E preprocess only" "timeout 10 $CC -E /tmp/kb_test.c"
check "-D define" "echo '#ifdef FOO\nint x=1;\n#endif\nint main(void){return 0;}' > /tmp/kb_pp.c && timeout 10 $CC -DFOO -c -o /tmp/kb_test.o /tmp/kb_pp.c"

# Kernel-specific flags
echo ""
echo "--- Kernel flags ---"
echo 'int main(void){return 0;}' > /tmp/kb_test.c
check "-ffreestanding" "timeout 10 $CC -ffreestanding -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-nostdinc" "timeout 10 $CC -nostdinc -Isrc/libc/include -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-fno-strict-aliasing" "timeout 10 $CC -fno-strict-aliasing -c -o /tmp/kb_test.o /tmp/kb_test.c"
check "-fno-common" "timeout 10 $CC -fno-common -c -o /tmp/kb_test.o /tmp/kb_test.c"

# GNU extensions
echo ""
echo "--- GNU C extensions ---"
check "statement expr" "echo 'int main(void){return ({int x=42;x;});}' > /tmp/kb_gnu.c && timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_gnu.c"
check "typeof" "echo 'int main(void){int x=5;typeof(x) y=x;return y;}' > /tmp/kb_gnu.c && timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_gnu.c"
check "__attribute__((unused))" "echo 'int __attribute__((unused)) x; int main(void){return 0;}' > /tmp/kb_gnu.c && timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_gnu.c"
check "__builtin_expect" "echo 'int main(void){return __builtin_expect(1,1);}' > /tmp/kb_gnu.c && timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_gnu.c"
check "__builtin_offsetof" "echo 'struct S{int a;int b;}; int main(void){return __builtin_offsetof(struct S,b);}' > /tmp/kb_gnu.c && timeout 10 $CC -c -o /tmp/kb_test.o /tmp/kb_gnu.c"

rm -f /tmp/kb_test.c /tmp/kb_test.o /tmp/kb_test.s /tmp/kb_test /tmp/kb_test2.c /tmp/kb_pp.c /tmp/kb_gnu.c

echo ""
echo "$((PASS + FAIL)) tests: $PASS passed, $FAIL failed"
exit $FAIL
