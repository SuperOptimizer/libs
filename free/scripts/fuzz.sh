#!/bin/sh
# fuzz.sh - Run fuzzers for the free toolchain.
# Requires AFL++ or falls back to simple random mutation testing.
# Usage: ./scripts/fuzz.sh [lex|parse|asm|elf] [duration_seconds]
# chmod +x scripts/fuzz.sh
#
# Examples:
#   ./scripts/fuzz.sh lex          # fuzz lexer for 60 seconds (default)
#   ./scripts/fuzz.sh elf 300      # fuzz ELF parser for 5 minutes
#   ./scripts/fuzz.sh all          # fuzz all targets for 60 seconds each
#
# Pure POSIX shell. No bashisms.

set -e

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FUZZ_DIR="${PROJ_ROOT}/test/fuzz"
CORPUS_DIR="${FUZZ_DIR}/corpus"
BUILD_DIR="${PROJ_ROOT}/build/fuzz"
FINDINGS_DIR="${BUILD_DIR}/findings"

TARGET="${1:-all}"
DURATION="${2:-60}"

# ---- colors (if terminal supports it) ----
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    NC='\033[0m'
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

log_info()  { printf "${GREEN}[fuzz]${NC} %s\n" "$1"; }
log_warn()  { printf "${YELLOW}[fuzz]${NC} %s\n" "$1"; }
log_error() { printf "${RED}[fuzz]${NC} %s\n" "$1"; }

# ---- check for AFL ----
HAS_AFL=0
if command -v afl-fuzz >/dev/null 2>&1; then
    HAS_AFL=1
fi

HAS_AFL_GCC=0
if command -v afl-gcc >/dev/null 2>&1; then
    HAS_AFL_GCC=1
elif command -v afl-cc >/dev/null 2>&1; then
    HAS_AFL_GCC=1
fi

# ---- build fuzzers ----
build_fuzzer()
{
    name="$1"
    shift
    sources="$@"

    mkdir -p "${BUILD_DIR}"

    if [ "${HAS_AFL}" -eq 1 ] && [ "${HAS_AFL_GCC}" -eq 1 ]; then
        log_info "Building ${name} with AFL instrumentation"
        AFL_CC="afl-gcc"
        if ! command -v afl-gcc >/dev/null 2>&1; then
            AFL_CC="afl-cc"
        fi
        ${AFL_CC} -std=c89 -O2 \
            -I"${PROJ_ROOT}/include" \
            -I"${PROJ_ROOT}/src/ld" \
            -o "${BUILD_DIR}/${name}" \
            ${sources} 2>/dev/null || {
            log_warn "AFL build failed, falling back to gcc"
            HAS_AFL=0
        }
    fi

    if [ "${HAS_AFL}" -eq 0 ] || [ ! -f "${BUILD_DIR}/${name}" ]; then
        log_info "Building ${name} with system compiler"
        cc -std=c89 -O2 -g \
            -I"${PROJ_ROOT}/include" \
            -I"${PROJ_ROOT}/src/ld" \
            -o "${BUILD_DIR}/${name}" \
            ${sources}
    fi
}

build_fuzz_lex()
{
    build_fuzzer fuzz_lex \
        "${FUZZ_DIR}/fuzz_lex.c" \
        "${PROJ_ROOT}/src/cc/lex.c"
}

build_fuzz_parse()
{
    build_fuzzer fuzz_parse \
        "${FUZZ_DIR}/fuzz_parse.c" \
        "${PROJ_ROOT}/src/cc/lex.c" \
        "${PROJ_ROOT}/src/cc/type.c"
}

build_fuzz_asm()
{
    build_fuzzer fuzz_asm \
        "${FUZZ_DIR}/fuzz_asm.c" \
        "${PROJ_ROOT}/src/as/lex.c" \
        "${PROJ_ROOT}/src/as/encode.c"
}

build_fuzz_elf()
{
    build_fuzzer fuzz_elf \
        "${FUZZ_DIR}/fuzz_elf.c"
}

# ---- run with AFL ----
run_afl()
{
    name="$1"
    corpus="$2"
    duration="$3"

    mkdir -p "${FINDINGS_DIR}/${name}"

    log_info "Running AFL on ${name} for ${duration}s"
    log_info "Corpus: ${corpus}"
    log_info "Findings: ${FINDINGS_DIR}/${name}"

    timeout "${duration}" afl-fuzz \
        -i "${corpus}" \
        -o "${FINDINGS_DIR}/${name}" \
        -V "${duration}" \
        -- "${BUILD_DIR}/${name}" @@ \
        2>/dev/null || true

    # check for crashes
    crash_count=0
    if [ -d "${FINDINGS_DIR}/${name}/default/crashes" ]; then
        crash_count=$(ls -1 "${FINDINGS_DIR}/${name}/default/crashes" 2>/dev/null | grep -v README | wc -l)
    fi

    if [ "${crash_count}" -gt 0 ]; then
        log_error "CRASHES FOUND: ${crash_count} in ${name}"
        ls -la "${FINDINGS_DIR}/${name}/default/crashes/"
        return 1
    else
        log_info "No crashes found in ${name}"
        return 0
    fi
}

# ---- simple random mutation fuzzing (fallback) ----
random_mutate()
{
    input_file="$1"
    output_file="$2"
    file_size=$(wc -c < "${input_file}")

    if [ "${file_size}" -eq 0 ]; then
        cp "${input_file}" "${output_file}"
        return
    fi

    # copy original
    cp "${input_file}" "${output_file}"

    # number of mutations: 1 to 10
    num_mutations=$(( (RANDOM % 10) + 1 )) 2>/dev/null || num_mutations=3

    i=0
    while [ "${i}" -lt "${num_mutations}" ]; do
        # pick a random position
        pos=$(od -A n -t u4 -N 4 /dev/urandom 2>/dev/null | tr -d ' ') || pos=0
        pos=$(( pos % (file_size + 1) ))

        # pick a random byte
        byte=$(od -A n -t u1 -N 1 /dev/urandom 2>/dev/null | tr -d ' ') || byte=65

        # apply mutation using dd
        printf "\\$(printf '%o' "${byte}")" | \
            dd of="${output_file}" bs=1 seek="${pos}" count=1 conv=notrunc 2>/dev/null

        i=$((i + 1))
    done
}

run_simple_fuzz()
{
    name="$1"
    corpus_pattern="$2"
    duration="$3"

    log_info "Running simple mutation fuzzing on ${name} for ${duration}s"
    log_warn "(Install AFL++ for better fuzzing: apt install afl++)"

    mkdir -p "${FINDINGS_DIR}/${name}/crashes"

    # collect corpus files
    corpus_files=""
    for f in ${corpus_pattern}; do
        if [ -f "${f}" ]; then
            corpus_files="${corpus_files} ${f}"
        fi
    done

    if [ -z "${corpus_files}" ]; then
        log_error "No corpus files found matching: ${corpus_pattern}"
        return 1
    fi

    start_time=$(date +%s)
    end_time=$((start_time + duration))
    iterations=0
    crashes=0
    tmp_file="${BUILD_DIR}/${name}_tmp_input"

    while [ "$(date +%s)" -lt "${end_time}" ]; do
        for seed in ${corpus_files}; do
            if [ "$(date +%s)" -ge "${end_time}" ]; then
                break
            fi

            # mutate seed
            random_mutate "${seed}" "${tmp_file}"

            # run fuzzer, check exit code
            if "${BUILD_DIR}/${name}" "${tmp_file}" >/dev/null 2>&1; then
                : # success
            else
                exit_code=$?
                # exit codes > 128 indicate signal (crash)
                if [ "${exit_code}" -gt 128 ]; then
                    crashes=$((crashes + 1))
                    crash_file="${FINDINGS_DIR}/${name}/crashes/crash_${iterations}"
                    cp "${tmp_file}" "${crash_file}"
                    signal=$((exit_code - 128))
                    log_error "CRASH (signal ${signal}) on iteration ${iterations}, saved to ${crash_file}"
                fi
            fi

            iterations=$((iterations + 1))
        done
    done

    rm -f "${tmp_file}"

    elapsed=$(($(date +%s) - start_time))
    log_info "${name}: ${iterations} iterations in ${elapsed}s, ${crashes} crashes"

    if [ "${crashes}" -gt 0 ]; then
        log_error "Crashes saved in ${FINDINGS_DIR}/${name}/crashes/"
        return 1
    else
        log_info "No crashes found"
        return 0
    fi
}

# ---- run a single fuzzer ----
run_fuzzer()
{
    name="$1"
    corpus_pattern="$2"
    duration="$3"

    if [ "${HAS_AFL}" -eq 1 ] && [ -d "$(echo ${corpus_pattern} | sed 's|/[^/]*$||')" ]; then
        corpus_dir=$(echo "${corpus_pattern}" | sed 's|/[^/]*$||')
        run_afl "${name}" "${corpus_dir}" "${duration}"
    else
        run_simple_fuzz "${name}" "${corpus_pattern}" "${duration}"
    fi
}

# ---- generate ELF corpus if needed ----
generate_elf_corpus()
{
    if [ ! -f "${CORPUS_DIR}/elf_minimal.bin" ]; then
        if [ -f "${CORPUS_DIR}/gen_elf_minimal.sh" ]; then
            log_info "Generating ELF corpus..."
            sh "${CORPUS_DIR}/gen_elf_minimal.sh"
        fi
    fi
}

# ---- main ----

total_crashes=0

case "${TARGET}" in
    lex)
        build_fuzz_lex
        run_fuzzer fuzz_lex "${CORPUS_DIR}/lex_*.c" "${DURATION}" || total_crashes=$((total_crashes + 1))
        ;;
    parse)
        build_fuzz_parse
        run_fuzzer fuzz_parse "${CORPUS_DIR}/lex_*.c" "${DURATION}" || total_crashes=$((total_crashes + 1))
        ;;
    asm)
        build_fuzz_asm
        run_fuzzer fuzz_asm "${CORPUS_DIR}/asm_*.s" "${DURATION}" || total_crashes=$((total_crashes + 1))
        ;;
    elf)
        generate_elf_corpus
        build_fuzz_elf
        run_fuzzer fuzz_elf "${CORPUS_DIR}/elf_*.bin" "${DURATION}" || total_crashes=$((total_crashes + 1))
        ;;
    all)
        log_info "Fuzzing all targets for ${DURATION}s each"

        build_fuzz_lex
        run_fuzzer fuzz_lex "${CORPUS_DIR}/lex_*.c" "${DURATION}" || total_crashes=$((total_crashes + 1))

        build_fuzz_parse
        run_fuzzer fuzz_parse "${CORPUS_DIR}/lex_*.c" "${DURATION}" || total_crashes=$((total_crashes + 1))

        build_fuzz_asm
        run_fuzzer fuzz_asm "${CORPUS_DIR}/asm_*.s" "${DURATION}" || total_crashes=$((total_crashes + 1))

        generate_elf_corpus
        build_fuzz_elf
        run_fuzzer fuzz_elf "${CORPUS_DIR}/elf_*.bin" "${DURATION}" || total_crashes=$((total_crashes + 1))
        ;;
    *)
        echo "Usage: $0 [lex|parse|asm|elf|all] [duration_seconds]"
        echo ""
        echo "Targets:"
        echo "  lex    - Fuzz the C lexer"
        echo "  parse  - Fuzz the C parser"
        echo "  asm    - Fuzz the assembly lexer and encoder"
        echo "  elf    - Fuzz the ELF parser"
        echo "  all    - Fuzz all targets"
        echo ""
        echo "Duration defaults to 60 seconds per target."
        exit 1
        ;;
esac

echo ""
if [ "${total_crashes}" -gt 0 ]; then
    log_error "TOTAL: ${total_crashes} target(s) had crashes"
    exit 1
else
    log_info "All fuzz targets passed without crashes"
    exit 0
fi
