cmake_minimum_required(VERSION 3.10)
project(free C ASM)

# Read version from VERSION file
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION FREE_VERSION LIMIT_COUNT 1)
message(STATUS "free toolchain version: ${FREE_VERSION}")

# ---------------------------------------------------------------------------
# Compiler settings — C89, strict warnings, bootstrap on system libc
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(FREE_C_FLAGS
    -std=c89 -pedantic
    -Wall -Wextra -Werror
    -Wshadow -Wstrict-prototypes -Wold-style-definition
)

# Toolchain-wide include path
set(FREE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# C compiler — free-cc
# ---------------------------------------------------------------------------
add_executable(free-cc
    src/cc/lex.c
    src/cc/pp.c
    src/cc/parse.c
    src/cc/type.c
    src/cc/gen.c
    src/cc/gen_x86.c
    src/cc/opt.c
    src/cc/pic.c
    src/cc/util.c
    src/cc/cc.c
    src/cc/c99.c
    src/cc/c11.c
    src/cc/c23.c
    src/cc/ir.c
    src/cc/ir_print.c
    src/cc/ir_serialize.c
    src/cc/lto.c
    src/cc/dwarf.c
    src/cc/ext_builtins.c
    src/cc/ext_attrs.c
    src/cc/ext_asm.c
    src/cc/diag.c
    src/cc/opt_bb.c
    src/cc/opt_mem2reg.c
    src/cc/opt_sccp.c
    src/cc/opt_dce.c
    src/cc/opt_inline.c
    src/cc/regalloc.c
    src/cc/ir_codegen.c
)
target_include_directories(free-cc PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-cc PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Assembler — free-as
# ---------------------------------------------------------------------------
add_executable(free-as
    src/as/lex.c
    src/as/encode.c
    src/as/encode_x86.c
    src/as/emit.c
    src/as/as.c
)
target_include_directories(free-as PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-as PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Linker — free-ld
# ---------------------------------------------------------------------------
add_executable(free-ld
    src/ld/elf.c
    src/ld/reloc.c
    src/ld/layout.c
    src/ld/dynamic.c
    src/ld/script.c
    src/cc/ir_serialize.c
    src/cc/lto.c
    src/cc/util.c
    src/ld/ld.c
)
target_include_directories(free-ld PRIVATE ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/ld ${CMAKE_SOURCE_DIR}/src/cc)
target_compile_options(free-ld PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Archiver — free-ar
# ---------------------------------------------------------------------------
add_executable(free-ar
    src/ar/ar.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-ar PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-ar PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-ar PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# ELF inspector — free-objdump
# ---------------------------------------------------------------------------
add_executable(free-objdump
    src/objdump/objdump.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-objdump PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-objdump PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-objdump PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Symbol listing — free-nm
# ---------------------------------------------------------------------------
add_executable(free-nm
    src/nm/nm.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-nm PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-nm PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-nm PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Binary manipulation — free-objcopy
# ---------------------------------------------------------------------------
add_executable(free-objcopy
    src/objcopy/objcopy.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-objcopy PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-objcopy PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-objcopy PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Symbol stripping — free-strip
# ---------------------------------------------------------------------------
add_executable(free-strip
    src/strip/strip.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-strip PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-strip PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-strip PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Debugger — free-dbg
# ---------------------------------------------------------------------------
add_executable(free-dbg
    src/dbg/dbg.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-dbg PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-dbg PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-dbg PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Build system — free-make
# ---------------------------------------------------------------------------
add_executable(free-make
    src/make/make.c
)
target_include_directories(free-make PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-make PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Standalone preprocessor — free-cpp
# ---------------------------------------------------------------------------
set(CC_SRC_DIR ${CMAKE_SOURCE_DIR}/src/cc)
add_executable(free-cpp
    src/cpp/cpp.c
    ${CC_SRC_DIR}/lex.c
    ${CC_SRC_DIR}/pp.c
    ${CC_SRC_DIR}/ext_builtins.c
    ${CC_SRC_DIR}/diag.c
    ${CC_SRC_DIR}/util.c
)
target_include_directories(free-cpp PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-cpp PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# String extractor — free-strings
# ---------------------------------------------------------------------------
add_executable(free-strings
    src/strings/strings.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-strings PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-strings PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-strings PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Address to line mapper — free-addr2line
# ---------------------------------------------------------------------------
add_executable(free-addr2line
    src/addr2line/addr2line.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-addr2line PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-addr2line PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-addr2line PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Section size reporter — free-size
# ---------------------------------------------------------------------------
add_executable(free-size
    src/size/size.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-size PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-size PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-size PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# ELF inspector — free-readelf
# ---------------------------------------------------------------------------
add_executable(free-readelf
    src/readelf/readelf.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-readelf PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-readelf PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-readelf PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Exception table sorter — free-sortextable
# ---------------------------------------------------------------------------
add_executable(free-sortextable
    src/depmod/sortextable.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-sortextable PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-sortextable PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-sortextable PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Struct layout inspector — free-pahole
# ---------------------------------------------------------------------------
add_executable(free-pahole
    src/pahole/pahole.c
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
)
target_include_directories(free-pahole PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-pahole PRIVATE ${FREE_C_FLAGS} -fno-stack-protector -ffreestanding)
target_link_options(free-pahole PRIVATE -nostdlib -nostartfiles -static)

# ---------------------------------------------------------------------------
# Token dump tool — free-lex
# ---------------------------------------------------------------------------
add_executable(free-lex
    src/lex/lex_tool.c
    ${CC_SRC_DIR}/lex.c
    ${CC_SRC_DIR}/pp.c
    ${CC_SRC_DIR}/ext_builtins.c
    ${CC_SRC_DIR}/diag.c
    ${CC_SRC_DIR}/util.c
)
target_include_directories(free-lex PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-lex PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# AST dump tool — free-ast
# ---------------------------------------------------------------------------
add_executable(free-ast
    src/ast/ast_tool.c
    ${CC_SRC_DIR}/lex.c
    ${CC_SRC_DIR}/pp.c
    ${CC_SRC_DIR}/parse.c
    ${CC_SRC_DIR}/type.c
    ${CC_SRC_DIR}/ext_builtins.c
    ${CC_SRC_DIR}/ext_attrs.c
    ${CC_SRC_DIR}/ext_asm.c
    ${CC_SRC_DIR}/c99.c
    ${CC_SRC_DIR}/c11.c
    ${CC_SRC_DIR}/c23.c
    ${CC_SRC_DIR}/diag.c
    ${CC_SRC_DIR}/util.c
)
target_include_directories(free-ast PRIVATE ${FREE_INCLUDE_DIR})
target_compile_options(free-ast PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# C library — libfree.a (static)
# ---------------------------------------------------------------------------
set(LIBC_C_SOURCES
    src/libc/string.c
    src/libc/stdio.c
    src/libc/stdlib.c
    src/libc/ctype.c
    src/libc/errno.c
    src/libc/time.c
    src/libc/math.c
    src/libc/dirent.c
    src/libc/mman.c
)

set(LIBC_ASM_SOURCES
    src/libc/arch/aarch64/crt.S
    src/libc/arch/aarch64/syscall.S
    src/libc/arch/aarch64/setjmp.S
)

# Assembly sources need special handling — no C89 flags, explicit language
set_source_files_properties(${LIBC_ASM_SOURCES} PROPERTIES
    LANGUAGE ASM
)

add_library(free-libc STATIC
    ${LIBC_C_SOURCES}
    ${LIBC_ASM_SOURCES}
)
set_target_properties(free-libc PROPERTIES OUTPUT_NAME free)
target_include_directories(free-libc
    PUBLIC  ${CMAKE_SOURCE_DIR}/src/libc/include
    PRIVATE ${FREE_INCLUDE_DIR}
)
target_compile_options(free-libc PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${FREE_C_FLAGS} -fno-builtin>
)

# ---------------------------------------------------------------------------
# Extended utility library — libcx.a
# ---------------------------------------------------------------------------
set(LIBCX_SOURCES
    src/libcx/arena.c
    src/libcx/str.c
    src/libcx/vec.c
    src/libcx/map.c
    src/libcx/path.c
    src/libcx/utf8.c
    src/libcx/log.c
    src/libcx/base64.c
    src/libcx/hash.c
    src/libcx/argparse.c
)

add_library(free-libcx STATIC ${LIBCX_SOURCES})
set_target_properties(free-libcx PROPERTIES OUTPUT_NAME cx)
target_include_directories(free-libcx
    PUBLIC  ${CMAKE_SOURCE_DIR}/src/libcx/include
    PRIVATE ${FREE_INCLUDE_DIR}
)
target_compile_options(free-libcx PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Random C89 program generator — cgen (for testing)
# ---------------------------------------------------------------------------
add_executable(cgen
    test/gen/cgen.c
)
target_compile_options(cgen PRIVATE ${FREE_C_FLAGS})

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
enable_testing()

set(FREE_TEST_DIR ${CMAKE_SOURCE_DIR}/test)

# ---------------------------------------------------------------------------
# Test resource limits — prevent runaway loops/allocations from killing system
# ---------------------------------------------------------------------------
# 30s timeout per test, 256 MB virtual memory limit
set(FREE_TEST_TIMEOUT 30)
set(FREE_TEST_MEMLIMIT_KB 262144)

# Wrapper script that applies ulimit before running the test binary
file(WRITE ${CMAKE_BINARY_DIR}/run_test.sh
    "#!/bin/sh\nulimit -v ${FREE_TEST_MEMLIMIT_KB} 2>/dev/null; exec \"$@\"\n")
file(CHMOD ${CMAKE_BINARY_DIR}/run_test.sh
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

# Helper: add a test executable that links against libfree.a
function(free_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_include_directories(${TEST_NAME} PRIVATE
        ${FREE_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/libc/include
        ${FREE_TEST_DIR}
    )
    target_compile_options(${TEST_NAME} PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(${TEST_NAME} PRIVATE free-libc)
    add_test(NAME ${TEST_NAME}
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:${TEST_NAME}>)
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT ${FREE_TEST_TIMEOUT})
endfunction()

# --- libcx tests ---
file(GLOB LIBCX_TEST_SOURCES ${FREE_TEST_DIR}/libcx/*.c)
foreach(src ${LIBCX_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(tname test-libcx-${name})
    add_executable(${tname} ${src})
    target_include_directories(${tname} PRIVATE
        ${FREE_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/libcx/include
        ${CMAKE_SOURCE_DIR}/src/libc/include
        ${FREE_TEST_DIR}
    )
    target_compile_options(${tname} PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(${tname} PRIVATE free-libcx free-libc)
    add_test(NAME ${tname}
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:${tname}>)
    set_tests_properties(${tname} PROPERTIES
        LABELS "libcx"
        TIMEOUT ${FREE_TEST_TIMEOUT})
endforeach()

# --- libc tests ---
file(GLOB LIBC_TEST_SOURCES ${FREE_TEST_DIR}/libc/*.c)
foreach(src ${LIBC_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(tname test-libc-${name})
    free_add_test(${tname} ${src})
    set_tests_properties(${tname} PROPERTIES LABELS "libc")
endforeach()

# --- assembler tests (need encode.c for instruction encoder) ---
file(GLOB AS_TEST_SOURCES ${FREE_TEST_DIR}/as/*.c)
foreach(src ${AS_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(tname test-as-${name})
    # x86 encoder tests link encode_x86.c; directive tests need full assembler
    if(name MATCHES "_x86")
        add_executable(${tname} ${src} ${CMAKE_SOURCE_DIR}/src/as/encode_x86.c)
    elseif(name MATCHES "_directives")
        add_executable(${tname} ${src}
            ${CMAKE_SOURCE_DIR}/src/as/emit.c
            ${CMAKE_SOURCE_DIR}/src/as/lex.c
            ${CMAKE_SOURCE_DIR}/src/as/encode.c)
    else()
        add_executable(${tname} ${src} ${CMAKE_SOURCE_DIR}/src/as/encode.c)
    endif()
    target_include_directories(${tname} PRIVATE
        ${FREE_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/libc/include
        ${FREE_TEST_DIR}
    )
    target_compile_options(${tname} PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(${tname} PRIVATE free-libc)
    add_test(NAME ${tname}
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:${tname}>)
    set_tests_properties(${tname} PROPERTIES
        LABELS "as"
        TIMEOUT ${FREE_TEST_TIMEOUT})
endforeach()

# --- linker tests ---
file(GLOB LD_TEST_SOURCES ${FREE_TEST_DIR}/ld/*.c)
foreach(src ${LD_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(tname test-ld-${name})
    free_add_test(${tname} ${src})
    set_tests_properties(${tname} PROPERTIES LABELS "ld")
endforeach()

# --- compiler tests (each test links the cc sources it needs) ---
set(CC_SRC ${CMAKE_SOURCE_DIR}/src/cc)

# Gather existing cc source files for conditional linking
set(CC_LEX_SRC "")
set(CC_TYPE_SRC "")
set(CC_PARSE_SRC "")
set(CC_PP_SRC "")
set(CC_GEN_SRC "")
set(CC_DIAG_SRC "")
if(EXISTS ${CC_SRC}/lex.c)
    set(CC_LEX_SRC ${CC_SRC}/lex.c)
endif()
if(EXISTS ${CC_SRC}/type.c)
    set(CC_TYPE_SRC ${CC_SRC}/type.c)
endif()
if(EXISTS ${CC_SRC}/parse.c)
    set(CC_PARSE_SRC ${CC_SRC}/parse.c)
endif()
if(EXISTS ${CC_SRC}/pp.c)
    set(CC_PP_SRC ${CC_SRC}/pp.c)
endif()
if(EXISTS ${CC_SRC}/gen.c)
    set(CC_GEN_SRC ${CC_SRC}/gen.c)
endif()
if(EXISTS ${CC_SRC}/diag.c)
    set(CC_DIAG_SRC ${CC_SRC}/diag.c)
endif()
set(CC_OPT_SRC "")
if(EXISTS ${CC_SRC}/opt.c)
    set(CC_OPT_SRC ${CC_SRC}/opt.c)
endif()

# test_lex needs lex.c
if(CC_LEX_SRC AND EXISTS ${FREE_TEST_DIR}/cc/test_lex.c)
    add_executable(test-cc-test_lex
        ${FREE_TEST_DIR}/cc/test_lex.c ${CC_LEX_SRC})
    target_include_directories(test-cc-test_lex PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_lex PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_lex PRIVATE free-libc)
    add_test(NAME test-cc-test_lex
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_lex>)
    set_tests_properties(test-cc-test_lex PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_type needs type.c
if(CC_TYPE_SRC AND EXISTS ${FREE_TEST_DIR}/cc/test_type.c)
    add_executable(test-cc-test_type
        ${FREE_TEST_DIR}/cc/test_type.c ${CC_TYPE_SRC})
    target_include_directories(test-cc-test_type PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_type PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_type PRIVATE free-libc)
    add_test(NAME test-cc-test_type
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_type>)
    set_tests_properties(test-cc-test_type PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_pp needs lex.c and pp.c
if(CC_LEX_SRC AND CC_PP_SRC AND EXISTS ${FREE_TEST_DIR}/cc/test_pp.c)
    add_executable(test-cc-test_pp
        ${FREE_TEST_DIR}/cc/test_pp.c ${CC_LEX_SRC} ${CC_PP_SRC})
    target_include_directories(test-cc-test_pp PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_pp PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_pp PRIVATE free-libc)
    add_test(NAME test-cc-test_pp
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_pp>)
    set_tests_properties(test-cc-test_pp PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_parse needs lex.c, pp.c, type.c, and parse.c
if(CC_LEX_SRC AND CC_PP_SRC AND CC_TYPE_SRC AND CC_PARSE_SRC
        AND EXISTS ${FREE_TEST_DIR}/cc/test_parse.c)
    add_executable(test-cc-test_parse
        ${FREE_TEST_DIR}/cc/test_parse.c
        ${CC_LEX_SRC} ${CC_PP_SRC} ${CC_TYPE_SRC} ${CC_PARSE_SRC} ${CC_DIAG_SRC}
        ${CC_SRC}/ext_asm.c ${CC_SRC}/c11.c)
    target_include_directories(test-cc-test_parse PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_parse PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_parse PRIVATE free-libc)
    add_test(NAME test-cc-test_parse
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_parse>)
    set_tests_properties(test-cc-test_parse PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_kernel_features needs lex.c, pp.c, type.c, parse.c, ext_*.c, c*.c
if(CC_LEX_SRC AND CC_PP_SRC AND CC_TYPE_SRC AND CC_PARSE_SRC
        AND EXISTS ${FREE_TEST_DIR}/cc/test_kernel_features.c)
    add_executable(test-cc-test_kernel_features
        ${FREE_TEST_DIR}/cc/test_kernel_features.c
        ${CC_LEX_SRC} ${CC_PP_SRC} ${CC_TYPE_SRC} ${CC_PARSE_SRC}
        ${CC_SRC}/ext_attrs.c ${CC_SRC}/ext_asm.c ${CC_SRC}/ext_builtins.c
        ${CC_SRC}/c99.c ${CC_SRC}/c11.c ${CC_SRC}/c23.c ${CC_SRC}/util.c
        ${CC_DIAG_SRC})
    target_include_directories(test-cc-test_kernel_features PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_kernel_features PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_kernel_features PRIVATE free-libc)
    add_test(NAME test-cc-test_kernel_features
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_kernel_features>)
    set_tests_properties(test-cc-test_kernel_features PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_gen needs lex.c, pp.c, type.c, parse.c, and gen.c
if(CC_LEX_SRC AND CC_PP_SRC AND CC_TYPE_SRC AND CC_PARSE_SRC AND CC_GEN_SRC
        AND EXISTS ${FREE_TEST_DIR}/cc/test_gen.c)
    add_executable(test-cc-test_gen
        ${FREE_TEST_DIR}/cc/test_gen.c
        ${CC_LEX_SRC} ${CC_PP_SRC} ${CC_TYPE_SRC} ${CC_PARSE_SRC} ${CC_GEN_SRC}
        ${CC_SRC}/ext_builtins.c ${CC_SRC}/ext_asm.c ${CC_SRC}/c11.c
        ${CC_DIAG_SRC})
    target_include_directories(test-cc-test_gen PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_gen PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_gen PRIVATE free-libc)
    add_test(NAME test-cc-test_gen
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_gen>)
    set_tests_properties(test-cc-test_gen PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# test_opt needs opt.c (peephole optimizer)
set(CC_OPT_SRC "")
if(EXISTS ${CC_SRC}/opt.c)
    set(CC_OPT_SRC ${CC_SRC}/opt.c)
endif()

if(CC_OPT_SRC AND EXISTS ${FREE_TEST_DIR}/cc/test_opt.c)
    add_executable(test-cc-test_opt
        ${FREE_TEST_DIR}/cc/test_opt.c ${CC_OPT_SRC})
    target_include_directories(test-cc-test_opt PRIVATE
        ${FREE_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/src/libc/include ${FREE_TEST_DIR})
    target_compile_options(test-cc-test_opt PRIVATE ${FREE_C_FLAGS})
    target_link_libraries(test-cc-test_opt PRIVATE free-libc)
    add_test(NAME test-cc-test_opt
        COMMAND ${CMAKE_BINARY_DIR}/run_test.sh $<TARGET_FILE:test-cc-test_opt>)
    set_tests_properties(test-cc-test_opt PROPERTIES
        LABELS "cc" TIMEOUT ${FREE_TEST_TIMEOUT})
endif()

# --- integration tests ---
file(GLOB INTEGRATION_TEST_SOURCES ${FREE_TEST_DIR}/integration/*.c)
foreach(src ${INTEGRATION_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(tname test-integration-${name})
    free_add_test(${tname} ${src})
    set_tests_properties(${tname} PROPERTIES LABELS "integration")
endforeach()

# ---------------------------------------------------------------------------
# Convenience targets: run tests by category
# ---------------------------------------------------------------------------
# "make test" is provided by CTest (runs all tests)

add_custom_target(test-libc
    COMMAND ${CMAKE_CTEST_COMMAND} -L libc --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running libc tests"
)

add_custom_target(test-as
    COMMAND ${CMAKE_CTEST_COMMAND} -L as --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running assembler tests"
)

add_custom_target(test-ld
    COMMAND ${CMAKE_CTEST_COMMAND} -L ld --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running linker tests"
)

add_custom_target(test-cc
    COMMAND ${CMAKE_CTEST_COMMAND} -L cc --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running compiler tests"
)

add_custom_target(test-libcx
    COMMAND ${CMAKE_CTEST_COMMAND} -L libcx --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running libcx tests"
)

add_custom_target(test-integration
    COMMAND ${CMAKE_CTEST_COMMAND} -L integration --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running integration tests"
)

# ---------------------------------------------------------------------------
# Install targets
# ---------------------------------------------------------------------------
install(TARGETS
    free-cc free-as free-ld free-ar free-nm free-objdump
    free-objcopy free-strip free-size free-strings free-addr2line
    free-readelf free-dbg free-make free-cpp
    free-sortextable free-pahole free-lex free-ast
    RUNTIME DESTINATION bin
)
install(TARGETS free-libc ARCHIVE DESTINATION lib)
install(TARGETS free-libcx ARCHIVE DESTINATION lib)
install(DIRECTORY src/libc/include/ DESTINATION include)
install(DIRECTORY src/libcx/include/ DESTINATION include/cx)
install(FILES include/free.h include/elf.h include/aarch64.h
              include/dwarf.h include/ir.h include/x86_64.h
        DESTINATION include/free)
install(FILES VERSION DESTINATION share/free)
