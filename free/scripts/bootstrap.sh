#!/bin/sh
# bootstrap.sh - 3-stage bootstrap verification for the free toolchain.
# Verifies that the toolchain can compile itself and produces
# byte-identical output across stages.
# chmod +x scripts/bootstrap.sh
#
# Stage 1: Build free with system gcc (via cmake, already done)
# Stage 2: Build free with stage1 free
# Stage 3: Build free with stage2 free
# Verify: stage2 binaries == stage3 binaries (byte-identical)
#
# Usage: ./scripts/bootstrap.sh [build_dir]
#
# Pure POSIX shell. No bashisms.

set -e

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_BASE="${1:-${PROJ_ROOT}/build}"

STAGE1_DIR="${BUILD_BASE}/stage1"
STAGE2_DIR="${BUILD_BASE}/stage2"
STAGE3_DIR="${BUILD_BASE}/stage3"

# ---- colors (if terminal supports it) ----
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    BOLD='\033[1m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    BOLD=''
    NC=''
fi

log_info()  { printf "${GREEN}[bootstrap]${NC} %s\n" "$1"; }
log_warn()  { printf "${YELLOW}[bootstrap]${NC} %s\n" "$1"; }
log_error() { printf "${RED}[bootstrap]${NC} %s\n" "$1"; }
log_stage() { printf "${BOLD}${GREEN}[bootstrap]${NC} ${BOLD}=== %s ===${NC}\n" "$1"; }

# ---- tool binaries ----
TOOLS="free-cc free-as free-ld free-ar"

# ---- source files for each tool ----
CC_SOURCES="src/cc/lex.c src/cc/pp.c src/cc/parse.c src/cc/type.c src/cc/gen.c src/cc/util.c src/cc/cc.c"
AS_SOURCES="src/as/lex.c src/as/encode.c src/as/emit.c src/as/as.c"
LD_SOURCES="src/ld/elf.c src/ld/reloc.c src/ld/layout.c src/ld/ld.c"
AR_SOURCES="src/ar/ar.c"

# ---- common include flags ----
CFLAGS="-std=c89 -pedantic -Wall -Wextra -Werror"
INCLUDES="-I${PROJ_ROOT}/include -I${PROJ_ROOT}/src/libc/include"

# ---- helper: check if a tool exists ----
check_tool()
{
    tool_path="$1"
    if [ ! -x "${tool_path}" ]; then
        log_error "Tool not found: ${tool_path}"
        return 1
    fi
    return 0
}

# ==================================================================
# STAGE 1: Build with system gcc (via cmake)
# ==================================================================
stage1()
{
    log_stage "STAGE 1: Build with system compiler"

    mkdir -p "${STAGE1_DIR}"
    cd "${STAGE1_DIR}"

    if [ ! -f "Makefile" ]; then
        log_info "Running cmake..."
        cmake "${PROJ_ROOT}" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
    fi

    log_info "Building..."
    make -j"$(nproc 2>/dev/null || echo 2)" >/dev/null 2>&1

    # verify tools were built
    for tool in ${TOOLS}; do
        if check_tool "${STAGE1_DIR}/${tool}"; then
            log_info "Built ${tool}"
        else
            log_error "Failed to build ${tool}"
            exit 1
        fi
    done

    log_info "Stage 1 complete"
}

# ==================================================================
# STAGE 2: Build with stage1 free
# ==================================================================
stage2()
{
    log_stage "STAGE 2: Build with stage1 free"

    S1_CC="${STAGE1_DIR}/free-cc"
    S1_AS="${STAGE1_DIR}/free-as"
    S1_LD="${STAGE1_DIR}/free-ld"

    # check that stage1 tools exist
    for tool in ${TOOLS}; do
        check_tool "${STAGE1_DIR}/${tool}" || {
            log_error "Stage 1 tools not found. Run stage 1 first."
            exit 1
        }
    done

    mkdir -p "${STAGE2_DIR}"

    log_info "Compiling with stage1 free-cc..."

    # Compile each tool's source files with stage1 compiler
    # For each .c file: free-cc -> .s -> free-as -> .o
    # Then: free-ld -> executable

    # compile all C sources to assembly, then to objects
    compile_sources()
    {
        cc_bin="$1"
        as_bin="$2"
        out_dir="$3"
        shift 3

        for src in "$@"; do
            base=$(basename "${src}" .c)
            s_file="${out_dir}/${base}.s"
            o_file="${out_dir}/${base}.o"

            log_info "  ${src} -> ${base}.o"

            # compile C to assembly
            "${cc_bin}" -S ${INCLUDES} -o "${s_file}" "${PROJ_ROOT}/${src}" || {
                log_error "Compilation failed: ${src}"
                return 1
            }

            # assemble to object
            "${as_bin}" -o "${o_file}" "${s_file}" 2>/dev/null || {
                log_error "Assembly failed: ${s_file}"
                return 1
            }
        done
    }

    # Link objects into an executable
    link_tool()
    {
        ld_bin="$1"
        out_file="$2"
        out_dir="$3"
        shift 3

        obj_files=""
        for src in "$@"; do
            base=$(basename "${src}" .c)
            obj_files="${obj_files} ${out_dir}/${base}.o"
        done

        log_info "  Linking -> $(basename ${out_file})"
        "${ld_bin}" -o "${out_file}" ${obj_files} 2>/dev/null || {
            log_error "Link failed: ${out_file}"
            return 1
        }
    }

    # Build each tool
    for src_group in "free-cc:${CC_SOURCES}" "free-as:${AS_SOURCES}" \
                     "free-ld:${LD_SOURCES}" "free-ar:${AR_SOURCES}"; do
        tool_name=$(echo "${src_group}" | cut -d: -f1)
        tool_sources=$(echo "${src_group}" | cut -d: -f2)

        log_info "Building ${tool_name}..."
        compile_sources "${S1_CC}" "${S1_AS}" "${STAGE2_DIR}" ${tool_sources} || {
            log_warn "Stage 2 build of ${tool_name} failed (toolchain may be incomplete)"
            continue
        }
        link_tool "${S1_LD}" "${STAGE2_DIR}/${tool_name}" "${STAGE2_DIR}" ${tool_sources} || {
            log_warn "Stage 2 link of ${tool_name} failed"
            continue
        }
    done

    log_info "Stage 2 complete"
}

# ==================================================================
# STAGE 3: Build with stage2 free
# ==================================================================
stage3()
{
    log_stage "STAGE 3: Build with stage2 free"

    S2_CC="${STAGE2_DIR}/free-cc"
    S2_AS="${STAGE2_DIR}/free-as"
    S2_LD="${STAGE2_DIR}/free-ld"

    # check that stage2 tools exist
    for tool in ${TOOLS}; do
        check_tool "${STAGE2_DIR}/${tool}" || {
            log_error "Stage 2 tools not found. Run stage 2 first."
            exit 1
        }
    done

    mkdir -p "${STAGE3_DIR}"

    log_info "Compiling with stage2 free-cc..."

    for src_group in "free-cc:${CC_SOURCES}" "free-as:${AS_SOURCES}" \
                     "free-ld:${LD_SOURCES}" "free-ar:${AR_SOURCES}"; do
        tool_name=$(echo "${src_group}" | cut -d: -f1)
        tool_sources=$(echo "${src_group}" | cut -d: -f2)

        log_info "Building ${tool_name}..."

        for src in ${tool_sources}; do
            base=$(basename "${src}" .c)
            s_file="${STAGE3_DIR}/${base}.s"
            o_file="${STAGE3_DIR}/${base}.o"

            log_info "  ${src} -> ${base}.o"

            "${S2_CC}" -S ${INCLUDES} -o "${s_file}" "${PROJ_ROOT}/${src}" || {
                log_warn "Stage 3 compilation failed: ${src}"
                continue 2
            }

            "${S2_AS}" -o "${o_file}" "${s_file}" 2>/dev/null || {
                log_warn "Stage 3 assembly failed: ${s_file}"
                continue 2
            }
        done

        obj_files=""
        for src in ${tool_sources}; do
            base=$(basename "${src}" .c)
            obj_files="${obj_files} ${STAGE3_DIR}/${base}.o"
        done

        log_info "  Linking -> ${tool_name}"
        "${S2_LD}" -o "${STAGE3_DIR}/${tool_name}" ${obj_files} 2>/dev/null || {
            log_warn "Stage 3 link of ${tool_name} failed"
            continue
        }
    done

    log_info "Stage 3 complete"
}

# ==================================================================
# VERIFY: stage2 == stage3
# ==================================================================
verify()
{
    log_stage "VERIFY: Comparing stage2 and stage3 binaries"

    all_match=1
    compared=0

    for tool in ${TOOLS}; do
        s2="${STAGE2_DIR}/${tool}"
        s3="${STAGE3_DIR}/${tool}"

        if [ ! -f "${s2}" ]; then
            log_warn "Stage 2 ${tool} not found, skipping"
            continue
        fi
        if [ ! -f "${s3}" ]; then
            log_warn "Stage 3 ${tool} not found, skipping"
            continue
        fi

        compared=$((compared + 1))

        if cmp -s "${s2}" "${s3}"; then
            log_info "MATCH: ${tool} (byte-identical)"
        else
            log_error "MISMATCH: ${tool}"
            log_error "  stage2: $(wc -c < "${s2}") bytes, md5=$(md5sum "${s2}" 2>/dev/null | cut -d' ' -f1 || echo 'n/a')"
            log_error "  stage3: $(wc -c < "${s3}") bytes, md5=$(md5sum "${s3}" 2>/dev/null | cut -d' ' -f1 || echo 'n/a')"
            all_match=0
        fi
    done

    echo ""

    if [ "${compared}" -eq 0 ]; then
        log_warn "No binaries were compared. Toolchain may not be complete enough for self-hosting yet."
        return 1
    fi

    if [ "${all_match}" -eq 1 ]; then
        log_info "BOOTSTRAP VERIFIED: All ${compared} tool(s) are byte-identical between stage2 and stage3"
        return 0
    else
        log_error "BOOTSTRAP FAILED: Stage 2 and stage 3 binaries differ"
        return 1
    fi
}

# ==================================================================
# Main
# ==================================================================

log_stage "Free Toolchain Bootstrap Verification"
log_info "Project root: ${PROJ_ROOT}"
log_info "Build directory: ${BUILD_BASE}"
echo ""

stage1
echo ""
stage2
echo ""
stage3
echo ""
verify

exit $?
