/*
 * dbg.c - Standalone C debugger for the free toolchain
 * Usage: free-dbg <executable> [args...]
 * Mini-GDB using ptrace, reads DWARF debug info from ELF.
 * Pure C89, freestanding with OS syscalls.
 */

#include "../../include/free.h"
#include "../../include/elf.h"

/* ---- syscall wrappers (via __syscall from syscall.S) ---- */

extern long __syscall(long, long, long, long, long, long, long);

#define SYS_OPENAT     56
#define SYS_CLOSE      57
#define SYS_READ       63
#define SYS_WRITE      64
#define SYS_EXIT_GROUP 94
#define SYS_CLONE      220
#define SYS_EXECVE     221
#define SYS_WAIT4      260
#define SYS_PTRACE     117
#define SYS_KILL       129

/* ptrace requests */
#define PTRACE_TRACEME       0
#define PTRACE_PEEKDATA      2
#define PTRACE_POKEDATA      5
#define PTRACE_CONT          7
#define PTRACE_SINGLESTEP    9
#define PTRACE_GETREGSET     0x4204
#define PTRACE_SETREGSET     0x4205

/* NT_PRSTATUS for GETREGSET */
#define NT_PRSTATUS  1

/* waitpid flags */
#define WUNTRACED    2

/* signals */
#define SIGSTOP  19
#define SIGTRAP  5
#define SIGSEGV  11
#define SIGBUS   7
#define SIGILL   4
#define SIGKILL  9

/* clone flags */
#define SIGCHLD  17

/* open flags */
#define O_RDONLY   0
#define AT_FDCWD   -100

/* BRK instruction for breakpoints */
#define BRK_INSN     0xD4200000u

/* Mask for upper 32 bits of a 64-bit word */
#define UPPER32_MASK  (~(u64)0xFFFFFFFF)

/* ---- iovec for ptrace GETREGSET ---- */
struct iovec {
    void  *iov_base;
    u64    iov_len;
};

/* ---- aarch64 user registers (34 x 64-bit: x0-x30, sp, pc, pstate) ---- */
struct user_regs {
    u64 regs[31];   /* x0-x30 */
    u64 sp;
    u64 pc;
    u64 pstate;
};

/* ---- syscall wrappers ---- */

static long sys_openat(int dirfd, const char *path, int flags, int mode)
{
    return __syscall(SYS_OPENAT, (long)dirfd, (long)path,
                     (long)flags, (long)mode, 0, 0);
}

static long sys_read(int fd, void *buf, long count)
{
    return __syscall(SYS_READ, (long)fd, (long)buf, count, 0, 0, 0);
}

static long sys_write(int fd, const void *buf, long count)
{
    return __syscall(SYS_WRITE, (long)fd, (long)buf, count, 0, 0, 0);
}

static long sys_close(int fd)
{
    return __syscall(SYS_CLOSE, (long)fd, 0, 0, 0, 0, 0);
}

static void sys_exit(int code)
{
    __syscall(SYS_EXIT_GROUP, (long)code, 0, 0, 0, 0, 0);
    for (;;) {}
}

static long sys_ptrace(long request, long pid, long addr, long data)
{
    return __syscall(SYS_PTRACE, request, pid, addr, data, 0, 0);
}

static long sys_wait4(long pid, int *status, int options, void *rusage)
{
    return __syscall(SYS_WAIT4, pid, (long)status, (long)options,
                     (long)rusage, 0, 0);
}

static long sys_clone(unsigned long flags, void *stack, void *ptid,
                      void *tls, void *ctid)
{
    return __syscall(SYS_CLONE, (long)flags, (long)stack, (long)ptid,
                     (long)tls, (long)ctid, 0);
}

static long sys_execve(const char *path, char *const argv[],
                       char *const envp[])
{
    return __syscall(SYS_EXECVE, (long)path, (long)argv, (long)envp,
                     0, 0, 0);
}

static long sys_kill(long pid, int sig)
{
    return __syscall(SYS_KILL, pid, (long)sig, 0, 0, 0, 0);
}

/* ---- wait status macros ---- */
#define WIFEXITED(s)    (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define WIFSTOPPED(s)   (((s) & 0xff) == 0x7f)
#define WSTOPSIG(s)     (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)     ((s) & 0x7f)

/* ---- constants ---- */
#define MAX_BREAKPOINTS   64
#define MAX_LINE_ENTRIES  8192
#define MAX_INPUT         512
#define FILE_BUF_SIZE     (16 * 1024 * 1024)
#define SRC_BUF_SIZE      (1 * 1024 * 1024)

/* ---- utility functions ---- */

static void write_str(int fd, const char *s)
{
    long n;
    const char *p;

    n = 0;
    p = s;
    while (*p) {
        p++;
        n++;
    }
    sys_write(fd, s, n);
}

static void out(const char *s)
{
    write_str(1, s);
}

static void die(const char *msg)
{
    write_str(2, "free-dbg: ");
    write_str(2, msg);
    write_str(2, "\n");
    sys_exit(1);
}

static int slen(const char *s)
{
    int n;

    n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int seq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static void mem_copy(void *dst, const void *src, long n)
{
    char *d;
    const char *s;
    long i;

    d = (char *)dst;
    s = (const char *)src;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void mem_set(void *dst, int val, long n)
{
    char *d;
    long i;

    d = (char *)dst;
    for (i = 0; i < n; i++) {
        d[i] = (char)val;
    }
}

static char hex_char(int v)
{
    if (v < 10) {
        return '0' + (char)v;
    }
    return 'a' + (char)(v - 10);
}

static void print_hex(u64 val, int width)
{
    char buf[17];
    int i;

    for (i = width - 1; i >= 0; i--) {
        buf[i] = hex_char((int)(val & 0xf));
        val >>= 4;
    }
    buf[width] = '\0';
    out(buf);
}

static void print_dec(u64 val)
{
    char buf[24];
    int pos;

    pos = 23;
    buf[pos] = '\0';
    if (val == 0) {
        buf[--pos] = '0';
    } else {
        while (val > 0) {
            buf[--pos] = '0' + (char)(val % 10);
            val /= 10;
        }
    }
    out(&buf[pos]);
}

static void print_sdec(i64 val)
{
    if (val < 0) {
        out("-");
        print_dec((u64)(-val));
    } else {
        print_dec((u64)val);
    }
}

static long parse_number(const char *s)
{
    long val;

    val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s) {
            if (*s >= '0' && *s <= '9') {
                val = (val << 4) | (*s - '0');
            } else if (*s >= 'a' && *s <= 'f') {
                val = (val << 4) | (*s - 'a' + 10);
            } else if (*s >= 'A' && *s <= 'F') {
                val = (val << 4) | (*s - 'A' + 10);
            } else {
                break;
            }
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
            s++;
        }
    }
    return val;
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static const char *next_token(const char *s, char *buf, int bufsize)
{
    int i;

    s = skip_ws(s);
    i = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && i < bufsize - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* ---- ELF parsing ---- */

static char file_buf[FILE_BUF_SIZE];
static long file_size;

static const Elf64_Ehdr *elf_ehdr;
static const Elf64_Shdr *elf_shdrs;
static const char *elf_shstrtab;
static u16 elf_shnum;

static long read_file(const char *path, char *buf, long bufsize)
{
    int fd;
    long total;
    long n;

    fd = (int)sys_openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        write_str(2, "free-dbg: cannot open ");
        write_str(2, path);
        write_str(2, "\n");
        sys_exit(1);
    }
    total = 0;
    while (total < bufsize) {
        n = sys_read(fd, buf + total, bufsize - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    sys_close(fd);
    return total;
}

static void parse_elf(void)
{
    elf_ehdr = (const Elf64_Ehdr *)file_buf;
    if (file_size < (long)sizeof(Elf64_Ehdr)) {
        die("file too small");
    }
    if (elf_ehdr->e_ident[0] != ELFMAG0 || elf_ehdr->e_ident[1] != ELFMAG1 ||
        elf_ehdr->e_ident[2] != ELFMAG2 || elf_ehdr->e_ident[3] != ELFMAG3) {
        die("not an ELF file");
    }
    if (elf_ehdr->e_ident[4] != ELFCLASS64) {
        die("not 64-bit ELF");
    }
    elf_shnum = elf_ehdr->e_shnum;
    elf_shdrs = (const Elf64_Shdr *)(file_buf + elf_ehdr->e_shoff);
    elf_shstrtab = file_buf + elf_shdrs[elf_ehdr->e_shstrndx].sh_offset;
}

static const Elf64_Shdr *find_section(const char *name)
{
    int i;
    const char *sn;

    for (i = 0; i < elf_shnum; i++) {
        sn = elf_shstrtab + elf_shdrs[i].sh_name;
        if (seq(sn, name)) {
            return &elf_shdrs[i];
        }
    }
    return NULL;
}

/* ---- Symbol table ---- */

static const Elf64_Sym *symtab;
static int symtab_count;
static const char *strtab;

static void load_symtab(void)
{
    const Elf64_Shdr *sh;

    sh = find_section(".symtab");
    if (!sh) {
        symtab = NULL;
        symtab_count = 0;
        return;
    }
    symtab = (const Elf64_Sym *)(file_buf + sh->sh_offset);
    symtab_count = (int)(sh->sh_size / sizeof(Elf64_Sym));

    sh = find_section(".strtab");
    if (sh) {
        strtab = file_buf + sh->sh_offset;
    } else {
        strtab = NULL;
    }
}

static const char *sym_name(const Elf64_Sym *s)
{
    if (!strtab || s->st_name == 0) {
        return "";
    }
    return strtab + s->st_name;
}

static u64 lookup_symbol(const char *name)
{
    int i;

    for (i = 0; i < symtab_count; i++) {
        if (seq(sym_name(&symtab[i]), name)) {
            return symtab[i].st_value;
        }
    }
    return 0;
}

static const Elf64_Sym *find_func_at(u64 addr)
{
    int i;
    const Elf64_Sym *s;

    for (i = 0; i < symtab_count; i++) {
        s = &symtab[i];
        if (ELF64_ST_TYPE(s->st_info) == STT_FUNC && s->st_size > 0) {
            if (addr >= s->st_value && addr < s->st_value + s->st_size) {
                return s;
            }
        }
    }
    return NULL;
}

/* ---- DWARF .debug_line parser (simplified) ---- */

struct line_entry {
    u64 addr;
    int file_idx;
    int line;
};

static struct line_entry line_table[MAX_LINE_ENTRIES];
static int line_table_count;

#define MAX_DEBUG_FILES 256
static const char *debug_files[MAX_DEBUG_FILES];
static int debug_file_count;

static u64 read_uleb128(const u8 **pp)
{
    u64 result;
    int shift;
    u8 b;

    result = 0;
    shift = 0;
    do {
        b = **pp;
        (*pp)++;
        result |= (u64)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    return result;
}

static i64 read_sleb128(const u8 **pp)
{
    i64 result;
    int shift;
    u8 b;

    result = 0;
    shift = 0;
    do {
        b = **pp;
        (*pp)++;
        result |= (i64)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    if (shift < 64 && (b & 0x40)) {
        result |= -(((i64)1) << shift);
    }
    return result;
}

/* DWARF line number opcodes */
#define DW_LNS_copy              1
#define DW_LNS_advance_pc        2
#define DW_LNS_advance_line      3
#define DW_LNS_set_file          4
#define DW_LNS_set_column        5
#define DW_LNS_negate_stmt       6
#define DW_LNS_set_basic_block   7
#define DW_LNS_const_add_pc      8
#define DW_LNS_fixed_advance_pc  9
#define DW_LNS_set_prologue_end  10
#define DW_LNS_set_epilogue_begin 11
#define DW_LNS_set_isa           12

#define DW_LNE_end_sequence      1
#define DW_LNE_set_address       2
#define DW_LNE_define_file       3

static void add_line_entry(u64 addr, int fidx, int ln)
{
    if (line_table_count >= MAX_LINE_ENTRIES) {
        return;
    }
    line_table[line_table_count].addr = addr;
    line_table[line_table_count].file_idx = fidx;
    line_table[line_table_count].line = ln;
    line_table_count++;
}

static void parse_debug_line(void)
{
    const Elf64_Shdr *sh;
    const u8 *data;
    const u8 *end;
    u32 unit_length;
    u16 version;
    u32 header_length;
    u8 min_inst_len;
    i8 line_base;
    u8 line_range;
    u8 opcode_base;
    u8 std_opcode_lengths[256];
    int i;
    const u8 *prog_start;
    const u8 *unit_end;
    u64 address;
    int file_idx;
    int line;
    u8 op;
    u64 adv;
    int adj_op;
    u8 default_is_stmt;

    sh = find_section(".debug_line");
    if (!sh) {
        return;
    }

    data = (const u8 *)(file_buf + sh->sh_offset);
    end = data + sh->sh_size;
    debug_file_count = 0;
    line_table_count = 0;

    while (data < end) {
        mem_copy(&unit_length, data, 4);
        data += 4;
        unit_end = data + unit_length;

        mem_copy(&version, data, 2);
        data += 2;

        if (version >= 5) {
            data += 2; /* address_size + segment_selector_size */
        }

        mem_copy(&header_length, data, 4);
        data += 4;
        prog_start = data + header_length;

        min_inst_len = *data++;
        if (version >= 4) {
            data++; /* max_ops_per_instruction, skip */
        }
        default_is_stmt = *data++;
        line_base = (i8)*data++;
        line_range = *data++;
        opcode_base = *data++;

        for (i = 1; i < opcode_base; i++) {
            std_opcode_lengths[i] = *data++;
        }

        if (version >= 5) {
            /* DWARF 5 has different directory/file format; skip to program */
            data = prog_start;
        } else {
            /* DWARF 2-4: skip include directories */
            while (*data) {
                while (*data) {
                    data++;
                }
                data++;
            }
            data++;

            /* File name entries */
            while (*data) {
                if (debug_file_count < MAX_DEBUG_FILES) {
                    debug_files[debug_file_count++] = (const char *)data;
                }
                while (*data) {
                    data++;
                }
                data++;
                read_uleb128(&data); /* dir index */
                read_uleb128(&data); /* time */
                read_uleb128(&data); /* size */
            }
            data++;
        }

        /* Run line number state machine */
        data = prog_start;
        address = 0;
        file_idx = 1;
        line = 1;
        (void)default_is_stmt;

        while (data < unit_end) {
            op = *data++;
            if (op == 0) {
                /* Extended opcode */
                u64 ext_len;
                u8 ext_op;
                const u8 *ext_end;

                ext_len = read_uleb128(&data);
                ext_end = data + ext_len;
                ext_op = *data++;

                switch (ext_op) {
                case DW_LNE_end_sequence:
                    add_line_entry(address, file_idx, line);
                    address = 0;
                    file_idx = 1;
                    line = 1;
                    break;
                case DW_LNE_set_address:
                    mem_copy(&address, data, 8);
                    break;
                case DW_LNE_define_file:
                    break;
                }
                data = ext_end;
            } else if (op < opcode_base) {
                switch (op) {
                case DW_LNS_copy:
                    add_line_entry(address, file_idx, line);
                    break;
                case DW_LNS_advance_pc:
                    adv = read_uleb128(&data);
                    address += adv * min_inst_len;
                    break;
                case DW_LNS_advance_line:
                    line += (int)read_sleb128(&data);
                    break;
                case DW_LNS_set_file:
                    file_idx = (int)read_uleb128(&data);
                    break;
                case DW_LNS_set_column:
                    read_uleb128(&data);
                    break;
                case DW_LNS_negate_stmt:
                    break;
                case DW_LNS_set_basic_block:
                    break;
                case DW_LNS_const_add_pc:
                    adj_op = 255 - opcode_base;
                    address += (u64)(adj_op / line_range) * min_inst_len;
                    break;
                case DW_LNS_fixed_advance_pc:
                    {
                        u16 fadv;
                        mem_copy(&fadv, data, 2);
                        data += 2;
                        address += fadv;
                    }
                    break;
                case DW_LNS_set_prologue_end:
                case DW_LNS_set_epilogue_begin:
                    break;
                case DW_LNS_set_isa:
                    read_uleb128(&data);
                    break;
                default:
                    for (i = 0; i < std_opcode_lengths[op]; i++) {
                        read_uleb128(&data);
                    }
                    break;
                }
            } else {
                /* Special opcode */
                adj_op = op - opcode_base;
                address += (u64)(adj_op / line_range) * min_inst_len;
                line += line_base + (adj_op % line_range);
                add_line_entry(address, file_idx, line);
            }
        }

        data = unit_end;
    }
}

static int addr_to_line(u64 addr, const char **out_file, int *out_line)
{
    int i;
    int best;
    u64 best_addr;

    best = -1;
    best_addr = 0;
    for (i = 0; i < line_table_count; i++) {
        if (line_table[i].addr <= addr && line_table[i].addr > best_addr) {
            best_addr = line_table[i].addr;
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    *out_line = line_table[best].line;
    if (line_table[best].file_idx >= 1 &&
        line_table[best].file_idx <= debug_file_count) {
        *out_file = debug_files[line_table[best].file_idx - 1];
    } else {
        *out_file = "??";
    }
    return 1;
}

static u64 line_to_addr(const char *file, int target_line)
{
    int i;
    int flen;
    const char *fn;
    int fnlen;

    flen = slen(file);

    for (i = 0; i < line_table_count; i++) {
        if (line_table[i].line == target_line) {
            if (line_table[i].file_idx >= 1 &&
                line_table[i].file_idx <= debug_file_count) {
                fn = debug_files[line_table[i].file_idx - 1];
                fnlen = slen(fn);
                if (seq(fn, file) ||
                    (fnlen >= flen &&
                     seq(fn + fnlen - flen, file) &&
                     (fnlen == flen || fn[fnlen - flen - 1] == '/'))) {
                    return line_table[i].addr;
                }
            }
        }
    }
    return 0;
}

/* ---- Breakpoints ---- */

struct breakpoint {
    u64 addr;
    u32 orig_insn;
    int enabled;
    int id;
};

static struct breakpoint breakpoints[MAX_BREAKPOINTS];
static int bp_count;
static int bp_next_id = 1;

static long child_pid;
static int child_running;
static int child_started;
static struct user_regs regs;

static int get_regs(void)
{
    struct iovec iov;

    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);
    return (int)sys_ptrace(PTRACE_GETREGSET, child_pid,
                           NT_PRSTATUS, (long)&iov);
}

static u64 peek_data(u64 addr)
{
    long val;

    val = sys_ptrace(PTRACE_PEEKDATA, child_pid, (long)addr, 0);
    return (u64)val;
}

static int poke_data(u64 addr, u64 val)
{
    return (int)sys_ptrace(PTRACE_POKEDATA, child_pid,
                           (long)addr, (long)val);
}

static int insert_breakpoint(u64 addr)
{
    struct breakpoint *bp;
    u64 word;

    if (bp_count >= MAX_BREAKPOINTS) {
        out("Too many breakpoints\n");
        return -1;
    }

    bp = &breakpoints[bp_count];
    bp->addr = addr;
    bp->id = bp_next_id++;
    bp->enabled = 1;

    if (child_started) {
        word = peek_data(addr);
        bp->orig_insn = (u32)(word & 0xFFFFFFFFu);
        word = (word & UPPER32_MASK) | BRK_INSN;
        poke_data(addr, word);
    }

    bp_count++;
    return bp->id;
}

static struct breakpoint *find_bp_at(u64 addr)
{
    int i;

    for (i = 0; i < bp_count; i++) {
        if (breakpoints[i].addr == addr && breakpoints[i].enabled) {
            return &breakpoints[i];
        }
    }
    return NULL;
}

static void install_all_breakpoints(void)
{
    int i;
    u64 word;

    for (i = 0; i < bp_count; i++) {
        if (breakpoints[i].enabled) {
            word = peek_data(breakpoints[i].addr);
            breakpoints[i].orig_insn = (u32)(word & 0xFFFFFFFFu);
            word = (word & UPPER32_MASK) | BRK_INSN;
            poke_data(breakpoints[i].addr, word);
        }
    }
}

static void restore_bp_insn(struct breakpoint *bp)
{
    u64 word;

    word = peek_data(bp->addr);
    word = (word & UPPER32_MASK) | (u64)bp->orig_insn;
    poke_data(bp->addr, word);
}

/* ---- Process control ---- */

static const char *target_path;
static char **target_argv;

static void start_child(void)
{
    long pid;
    int status;
    char *envp[1];

    envp[0] = NULL;

    pid = sys_clone(SIGCHLD, NULL, NULL, NULL, NULL);
    if (pid < 0) {
        die("clone failed");
    }

    if (pid == 0) {
        /* Child: request tracing then exec. PTRACE_TRACEME causes the
           child to stop at the next exec with a SIGTRAP. */
        sys_ptrace(PTRACE_TRACEME, 0, 0, 0);
        sys_execve(target_path, target_argv, envp);
        write_str(2, "free-dbg: execve failed\n");
        sys_exit(127);
    }

    child_pid = pid;
    child_started = 1;

    /* Wait for child to stop after exec (SIGTRAP from PTRACE_TRACEME) */
    sys_wait4(child_pid, &status, WUNTRACED, NULL);

    if (WIFEXITED(status)) {
        out("Program exited with code ");
        print_dec((u64)WEXITSTATUS(status));
        out("\n");
        child_started = 0;
        child_running = 0;
        return;
    }

    if (!WIFSTOPPED(status)) {
        die("child did not stop after exec");
    }

    /* Install breakpoints now that the program image is loaded */
    install_all_breakpoints();

    child_running = 0;
    get_regs();

    out("Program stopped at 0x");
    print_hex(regs.pc, 16);
    out("\n");
}

static void wait_for_stop(void)
{
    int status;
    struct breakpoint *bp;
    const char *fname;
    int fline;
    const Elf64_Sym *fsym;
    int sig;

    sys_wait4(child_pid, &status, WUNTRACED, NULL);

    if (WIFEXITED(status)) {
        out("Program exited with code ");
        print_dec((u64)WEXITSTATUS(status));
        out("\n");
        child_running = 0;
        child_started = 0;
        return;
    }

    if (WIFSIGNALED(status)) {
        out("Program terminated by signal ");
        print_dec((u64)WTERMSIG(status));
        out("\n");
        child_running = 0;
        child_started = 0;
        return;
    }

    if (WIFSTOPPED(status)) {
        sig = WSTOPSIG(status);
        child_running = 0;
        get_regs();

        if (sig == SIGTRAP) {
            bp = find_bp_at(regs.pc);
            if (bp) {
                out("Breakpoint ");
                print_dec((u64)bp->id);
                out(" hit at 0x");
                print_hex(regs.pc, 16);
                fsym = find_func_at(regs.pc);
                if (fsym) {
                    out(" in ");
                    out(sym_name(fsym));
                    out("()");
                }
                if (addr_to_line(regs.pc, &fname, &fline)) {
                    out(" at ");
                    out(fname);
                    out(":");
                    print_dec((u64)fline);
                }
                out("\n");
                return;
            }
        } else if (sig == SIGSEGV) {
            out("Program received SIGSEGV at 0x");
            print_hex(regs.pc, 16);
            out("\n");
            return;
        } else if (sig == SIGBUS) {
            out("Program received SIGBUS at 0x");
            print_hex(regs.pc, 16);
            out("\n");
            return;
        } else if (sig == SIGILL) {
            out("Program received SIGILL at 0x");
            print_hex(regs.pc, 16);
            out("\n");
            return;
        }

        out("Stopped at 0x");
        print_hex(regs.pc, 16);
        if (sig != SIGTRAP) {
            out(" (signal ");
            print_dec((u64)sig);
            out(")");
        }
        out("\n");
    }
}

static void step_over_bp(void)
{
    struct breakpoint *bp;
    int status;
    u64 word;

    bp = find_bp_at(regs.pc);
    if (!bp) {
        return;
    }

    restore_bp_insn(bp);

    sys_ptrace(PTRACE_SINGLESTEP, child_pid, 0, 0);
    sys_wait4(child_pid, &status, WUNTRACED, NULL);

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        child_running = 0;
        child_started = 0;
        out("Program exited during single step\n");
        return;
    }

    get_regs();

    /* Re-insert breakpoint */
    word = peek_data(bp->addr);
    word = (word & UPPER32_MASK) | BRK_INSN;
    poke_data(bp->addr, word);
}

/* ---- Commands ---- */

static void cmd_continue(void)
{
    if (!child_started) {
        out("Program not started. Use 'run' first.\n");
        return;
    }

    step_over_bp();
    if (!child_started) {
        return;
    }

    sys_ptrace(PTRACE_CONT, child_pid, 0, 0);
    child_running = 1;
    wait_for_stop();
}

static void cmd_step(void)
{
    const char *fname;
    int fline;

    if (!child_started) {
        out("Program not started. Use 'run' first.\n");
        return;
    }

    step_over_bp();
    if (!child_started) {
        return;
    }

    sys_ptrace(PTRACE_SINGLESTEP, child_pid, 0, 0);
    wait_for_stop();

    if (child_started) {
        out("0x");
        print_hex(regs.pc, 16);
        if (addr_to_line(regs.pc, &fname, &fline)) {
            out(" at ");
            out(fname);
            out(":");
            print_dec((u64)fline);
        }
        out("\n");
    }
}

static void cmd_next(void)
{
    u32 insn;
    u64 word;
    u64 next_addr;
    struct breakpoint temp_bp;
    int status;
    const char *fname;
    int fline;

    if (!child_started) {
        out("Program not started. Use 'run' first.\n");
        return;
    }

    get_regs();
    word = peek_data(regs.pc);
    insn = (u32)(word & 0xFFFFFFFFu);

    /* Check if BL or BLR */
    if ((insn & 0xFC000000u) == 0x94000000u ||
        (insn & 0xFFFFFC1Fu) == 0xD63F0000u) {
        next_addr = regs.pc + 4;

        step_over_bp();
        if (!child_started) {
            return;
        }

        mem_set(&temp_bp, 0, sizeof(temp_bp));
        temp_bp.addr = next_addr;
        temp_bp.enabled = 1;
        temp_bp.id = -1;

        word = peek_data(next_addr);
        temp_bp.orig_insn = (u32)(word & 0xFFFFFFFFu);
        word = (word & UPPER32_MASK) | BRK_INSN;
        poke_data(next_addr, word);

        sys_ptrace(PTRACE_CONT, child_pid, 0, 0);
        sys_wait4(child_pid, &status, WUNTRACED, NULL);

        /* Remove temp breakpoint */
        word = peek_data(next_addr);
        word = (word & UPPER32_MASK) | (u64)temp_bp.orig_insn;
        poke_data(next_addr, word);

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            child_running = 0;
            child_started = 0;
            out("Program exited\n");
            return;
        }

        get_regs();
        out("0x");
        print_hex(regs.pc, 16);
        if (addr_to_line(regs.pc, &fname, &fline)) {
            out(" at ");
            out(fname);
            out(":");
            print_dec((u64)fline);
        }
        out("\n");
    } else {
        cmd_step();
    }
}

static void cmd_info_regs(void)
{
    int i;
    static const char *reg_names[] = {
        "x0 ", "x1 ", "x2 ", "x3 ", "x4 ", "x5 ", "x6 ", "x7 ",
        "x8 ", "x9 ", "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30"
    };

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    get_regs();
    for (i = 0; i < 31; i++) {
        out(reg_names[i]);
        out("  0x");
        print_hex(regs.regs[i], 16);
        if ((i & 1) == 1) {
            out("\n");
        } else {
            out("    ");
        }
    }
    out("\n");
    out("sp   0x");
    print_hex(regs.sp, 16);
    out("\n");
    out("pc   0x");
    print_hex(regs.pc, 16);
    out("\n");
}

static void cmd_backtrace(void)
{
    u64 fp;
    u64 lr;
    u64 prev_fp;
    int frame;
    const Elf64_Sym *fsym;
    const char *fname;
    int fline;

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    get_regs();

    /* Frame 0: current PC */
    out("#0  0x");
    print_hex(regs.pc, 16);
    fsym = find_func_at(regs.pc);
    if (fsym) {
        out(" in ");
        out(sym_name(fsym));
        out("()");
    }
    if (addr_to_line(regs.pc, &fname, &fline)) {
        out(" at ");
        out(fname);
        out(":");
        print_dec((u64)fline);
    }
    out("\n");

    /* Walk frame pointer chain: x29 (FP) */
    fp = regs.regs[29];
    frame = 1;

    while (fp != 0 && frame < 64) {
        prev_fp = peek_data(fp);
        lr = peek_data(fp + 8);

        if (lr == 0) {
            break;
        }

        out("#");
        print_dec((u64)frame);
        out("  0x");
        print_hex(lr, 16);

        fsym = find_func_at(lr);
        if (fsym) {
            out(" in ");
            out(sym_name(fsym));
            out("()");
        }
        if (addr_to_line(lr, &fname, &fline)) {
            out(" at ");
            out(fname);
            out(":");
            print_dec((u64)fline);
        }
        out("\n");

        if (prev_fp <= fp) {
            break;
        }
        fp = prev_fp;
        frame++;
    }
}

static void cmd_examine(const char *arg)
{
    u64 addr;
    u64 val;
    int i;

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    addr = parse_number(arg);
    if (addr == 0) {
        out("Usage: examine <address>\n");
        return;
    }

    for (i = 0; i < 4; i++) {
        out("0x");
        print_hex(addr, 16);
        out(":  0x");
        val = peek_data(addr);
        print_hex(val, 16);
        out("\n");
        addr += 8;
    }
}

static const char *decode_insn(u32 insn)
{
    static char buf[64];

    if (insn == 0xD503201Fu) {
        return "nop";
    }
    if (insn == 0xD65F03C0u) {
        return "ret";
    }
    if ((insn & 0xFFE0001Fu) == 0xD4200000u) {
        return "brk";
    }
    if ((insn & 0xFFE0001Fu) == 0xD4000001u) {
        return "svc";
    }
    if ((insn & 0xFC000000u) == 0x14000000u) {
        return "b";
    }
    if ((insn & 0xFC000000u) == 0x94000000u) {
        return "bl";
    }
    if ((insn & 0xFFFFFC1Fu) == 0xD61F0000u) {
        return "br";
    }
    if ((insn & 0xFFFFFC1Fu) == 0xD63F0000u) {
        return "blr";
    }
    if ((insn & 0xFF000010u) == 0x54000000u) {
        static const char *cond_names[] = {
            "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
            "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
        };
        int ci;
        int j;
        ci = insn & 0xf;
        buf[0] = 'b'; buf[1] = '.';
        for (j = 0; cond_names[ci][j]; j++) {
            buf[2 + j] = cond_names[ci][j];
        }
        buf[2 + j] = '\0';
        return buf;
    }
    if ((insn & 0x1F000000u) == 0x11000000u) {
        return ((insn >> 30) & 1) ? "sub" : "add";
    }
    if ((insn & 0x3B000000u) == 0x39000000u) {
        return ((insn >> 22) & 1) ? "ldr" : "str";
    }
    if ((insn & 0x3E000000u) == 0x28000000u ||
        (insn & 0x3E000000u) == 0x2C000000u ||
        (insn & 0x3E000000u) == 0x2A000000u) {
        return ((insn >> 22) & 1) ? "ldp" : "stp";
    }
    /* MOVZ (32-bit and 64-bit) */
    if ((insn & 0x7F800000u) == 0x52800000u) {
        return "movz";
    }
    /* MOVK (32-bit and 64-bit) */
    if ((insn & 0x7F800000u) == 0x72800000u) {
        return "movk";
    }
    /* MOVN (32-bit and 64-bit) */
    if ((insn & 0x7F800000u) == 0x12800000u) {
        return "movn";
    }
    if ((insn & 0x9F000000u) == 0x90000000u) {
        return "adrp";
    }
    if ((insn & 0x9F000000u) == 0x10000000u) {
        return "adr";
    }
    /* CMP (SUBS with Rd=XZR) */
    if ((insn & 0x7F00001Fu) == 0x6B00001Fu ||
        (insn & 0x7F00001Fu) == 0x7100001Fu) {
        return "cmp";
    }

    buf[0] = '.'; buf[1] = 'w'; buf[2] = 'o'; buf[3] = 'r';
    buf[4] = 'd'; buf[5] = '\0';
    return buf;
}

static void cmd_disassemble(const char *arg)
{
    u64 addr;
    u64 word;
    u32 insn;
    int i;
    const Elf64_Sym *fsym;

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    get_regs();

    if (arg && *arg) {
        arg = skip_ws(arg);
        if (*arg) {
            addr = parse_number(arg);
        } else {
            addr = regs.pc;
        }
    } else {
        addr = regs.pc;
    }

    fsym = find_func_at(addr);
    if (fsym) {
        out("Dump of ");
        out(sym_name(fsym));
        out("():\n");
    }

    for (i = 0; i < 10; i++) {
        word = peek_data(addr);
        insn = (u32)(word & 0xFFFFFFFFu);

        if (addr == regs.pc) {
            out("=> ");
        } else {
            out("   ");
        }
        out("0x");
        print_hex(addr, 16);
        out(":  ");
        print_hex((u64)insn, 8);
        out("  ");
        out(decode_insn(insn));
        out("\n");

        addr += 4;
    }
}

static void cmd_print(const char *arg)
{
    u64 val;
    int reg;
    const char *rname;

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    get_regs();
    arg = skip_ws(arg);

    if (arg[0] == '$' || arg[0] == 'x' || arg[0] == 'X') {
        rname = arg;
        if (*rname == '$') {
            rname++;
        }

        if (seq(rname, "pc")) {
            val = regs.pc;
        } else if (seq(rname, "sp")) {
            val = regs.sp;
        } else if (seq(rname, "fp") || seq(rname, "x29")) {
            val = regs.regs[29];
        } else if (seq(rname, "lr") || seq(rname, "x30")) {
            val = regs.regs[30];
        } else if (rname[0] == 'x' || rname[0] == 'X') {
            reg = 0;
            rname++;
            while (*rname >= '0' && *rname <= '9') {
                reg = reg * 10 + (*rname - '0');
                rname++;
            }
            if (reg >= 0 && reg <= 30) {
                val = regs.regs[reg];
            } else {
                out("Unknown register\n");
                return;
            }
        } else {
            out("Unknown register: ");
            out(arg);
            out("\n");
            return;
        }

        out("$");
        out(arg[0] == '$' ? arg + 1 : arg);
        out(" = 0x");
        print_hex(val, 16);
        out(" (");
        print_sdec((i64)val);
        out(")\n");
        return;
    }

    if (arg[0] == '*') {
        u64 addr;

        addr = parse_number(arg + 1);
        val = peek_data(addr);
        out("*0x");
        print_hex(addr, 16);
        out(" = 0x");
        print_hex(val, 16);
        out("\n");
        return;
    }

    val = lookup_symbol(arg);
    if (val) {
        out(arg);
        out(" = 0x");
        print_hex(val, 16);
        out(" (address)\n");
        return;
    }

    out("Symbol not found: ");
    out(arg);
    out("\n");
}

/* ---- Command: list (show source) ---- */
static char src_buf[SRC_BUF_SIZE];
static const char *cached_src_file;

static void cmd_list(void)
{
    const char *fname;
    int fline;
    int target_line;
    int fd;
    long fsize;
    long n;
    int line_num;
    int start;
    int end_line;
    long i;

    if (!child_started) {
        out("Program not started.\n");
        return;
    }

    get_regs();

    if (!addr_to_line(regs.pc, &fname, &fline)) {
        out("No source info for current PC\n");
        return;
    }

    target_line = fline;

    if (!cached_src_file || !seq(cached_src_file, fname)) {
        fd = (int)sys_openat(AT_FDCWD, fname, O_RDONLY, 0);
        if (fd < 0) {
            out("Cannot open source file: ");
            out(fname);
            out("\n");
            return;
        }
        fsize = 0;
        while (fsize < SRC_BUF_SIZE - 1) {
            n = sys_read(fd, src_buf + fsize, SRC_BUF_SIZE - 1 - fsize);
            if (n <= 0) {
                break;
            }
            fsize += n;
        }
        src_buf[fsize] = '\0';
        sys_close(fd);
        cached_src_file = fname;
    }

    start = target_line - 5;
    if (start < 1) {
        start = 1;
    }
    end_line = target_line + 5;

    line_num = 1;
    i = 0;

    while (line_num < start && src_buf[i]) {
        if (src_buf[i] == '\n') {
            line_num++;
        }
        i++;
    }

    while (line_num <= end_line && src_buf[i]) {
        if (line_num == target_line) {
            out("=> ");
        } else {
            out("   ");
        }

        if (line_num < 10) {
            out("   ");
        } else if (line_num < 100) {
            out("  ");
        } else if (line_num < 1000) {
            out(" ");
        }
        print_dec((u64)line_num);
        out("  ");

        while (src_buf[i] && src_buf[i] != '\n') {
            sys_write(1, &src_buf[i], 1);
            i++;
        }
        out("\n");
        if (src_buf[i] == '\n') {
            i++;
        }
        line_num++;
    }
}

/* ---- Command: break ---- */
static void cmd_break(const char *arg)
{
    u64 addr;
    int line;
    const char *colon;
    char fname[256];
    int fi;
    int bp_id;

    arg = skip_ws(arg);
    if (!*arg) {
        out("Usage: break <function> | break <file>:<line> | break *<address>\n");
        return;
    }

    if (arg[0] == '*') {
        addr = parse_number(arg + 1);
        if (addr == 0) {
            out("Invalid address\n");
            return;
        }
        bp_id = insert_breakpoint(addr);
        if (bp_id >= 0) {
            out("Breakpoint ");
            print_dec((u64)bp_id);
            out(" at 0x");
            print_hex(addr, 16);
            out("\n");
        }
        return;
    }

    colon = arg;
    while (*colon && *colon != ':') {
        colon++;
    }

    if (*colon == ':') {
        fi = 0;
        while (arg < colon && fi < 255) {
            fname[fi++] = *arg++;
        }
        fname[fi] = '\0';
        colon++;
        line = 0;
        while (*colon >= '0' && *colon <= '9') {
            line = line * 10 + (*colon - '0');
            colon++;
        }
        if (line <= 0) {
            out("Invalid line number\n");
            return;
        }
        addr = line_to_addr(fname, line);
        if (addr == 0) {
            out("No code at ");
            out(fname);
            out(":");
            print_dec((u64)line);
            out("\n");
            return;
        }
        bp_id = insert_breakpoint(addr);
        if (bp_id >= 0) {
            out("Breakpoint ");
            print_dec((u64)bp_id);
            out(" at 0x");
            print_hex(addr, 16);
            out(" (");
            out(fname);
            out(":");
            print_dec((u64)line);
            out(")\n");
        }
        return;
    }

    addr = lookup_symbol(arg);
    if (addr == 0) {
        out("Symbol not found: ");
        out(arg);
        out("\n");
        return;
    }
    bp_id = insert_breakpoint(addr);
    if (bp_id >= 0) {
        out("Breakpoint ");
        print_dec((u64)bp_id);
        out(" at 0x");
        print_hex(addr, 16);
        out(" (");
        out(arg);
        out(")\n");
    }
}

static void cmd_info_breakpoints(void)
{
    int i;
    const Elf64_Sym *fsym;

    if (bp_count == 0) {
        out("No breakpoints.\n");
        return;
    }

    out("Num  Enb  Address            What\n");
    for (i = 0; i < bp_count; i++) {
        print_dec((u64)breakpoints[i].id);
        out("    ");
        out(breakpoints[i].enabled ? "y    " : "n    ");
        out("0x");
        print_hex(breakpoints[i].addr, 16);
        out("  ");
        fsym = find_func_at(breakpoints[i].addr);
        if (fsym) {
            out("in ");
            out(sym_name(fsym));
            out("()");
        }
        out("\n");
    }
}

static void cmd_delete(const char *arg)
{
    int id;
    int i;

    arg = skip_ws(arg);
    if (!*arg) {
        out("Usage: delete <breakpoint-id>\n");
        return;
    }

    id = (int)parse_number(arg);
    for (i = 0; i < bp_count; i++) {
        if (breakpoints[i].id == id) {
            if (breakpoints[i].enabled && child_started) {
                restore_bp_insn(&breakpoints[i]);
            }
            breakpoints[i].enabled = 0;
            out("Breakpoint ");
            print_dec((u64)id);
            out(" deleted\n");
            return;
        }
    }
    out("No breakpoint number ");
    print_dec((u64)id);
    out("\n");
}

static void cmd_run(void)
{
    if (child_started) {
        sys_kill(child_pid, SIGKILL);
        sys_wait4(child_pid, NULL, 0, NULL);
        child_started = 0;
        child_running = 0;
    }
    start_child();
}

/* ---- Read a line of input ---- */
static char input_buf[MAX_INPUT];

static int read_line(char *buf, int size)
{
    int i;
    long n;

    i = 0;
    while (i < size - 1) {
        n = sys_read(0, &buf[i], 1);
        if (n <= 0) {
            return -1;
        }
        if (buf[i] == '\n') {
            buf[i] = '\0';
            return i;
        }
        i++;
    }
    buf[i] = '\0';
    return i;
}

/* ---- Main command loop ---- */

static char last_cmd[MAX_INPUT];

static void process_command(const char *line)
{
    char cmd[64];
    const char *rest;
    int li;

    line = skip_ws(line);
    if (*line == '\0') {
        if (last_cmd[0]) {
            line = last_cmd;
        } else {
            return;
        }
    } else {
        li = 0;
        while (line[li] && li < MAX_INPUT - 1) {
            last_cmd[li] = line[li];
            li++;
        }
        last_cmd[li] = '\0';
    }

    rest = next_token(line, cmd, sizeof(cmd));

    if (seq(cmd, "quit") || seq(cmd, "q")) {
        if (child_started) {
            sys_kill(child_pid, SIGKILL);
            sys_wait4(child_pid, NULL, 0, NULL);
        }
        sys_exit(0);
    }

    if (seq(cmd, "run") || seq(cmd, "r")) {
        cmd_run();
        return;
    }

    if (seq(cmd, "continue") || seq(cmd, "c")) {
        cmd_continue();
        return;
    }

    if (seq(cmd, "step") || seq(cmd, "s")) {
        cmd_step();
        return;
    }

    if (seq(cmd, "next") || seq(cmd, "n")) {
        cmd_next();
        return;
    }

    if (seq(cmd, "break") || seq(cmd, "b")) {
        cmd_break(rest);
        return;
    }

    if (seq(cmd, "delete") || seq(cmd, "d")) {
        cmd_delete(rest);
        return;
    }

    if (seq(cmd, "backtrace") || seq(cmd, "bt")) {
        cmd_backtrace();
        return;
    }

    if (seq(cmd, "print") || seq(cmd, "p")) {
        cmd_print(rest);
        return;
    }

    if (seq(cmd, "examine") || seq(cmd, "x")) {
        cmd_examine(rest);
        return;
    }

    if (seq(cmd, "disassemble") || seq(cmd, "disas") ||
        seq(cmd, "di")) {
        cmd_disassemble(rest);
        return;
    }

    if (seq(cmd, "list") || seq(cmd, "l")) {
        cmd_list();
        return;
    }

    if (seq(cmd, "info")) {
        char subcmd[64];

        rest = next_token(rest, subcmd, sizeof(subcmd));
        if (seq(subcmd, "registers") || seq(subcmd, "reg") ||
            seq(subcmd, "r")) {
            cmd_info_regs();
            return;
        }
        if (seq(subcmd, "breakpoints") || seq(subcmd, "break") ||
            seq(subcmd, "b")) {
            cmd_info_breakpoints();
            return;
        }
        out("Unknown info subcommand: ");
        out(subcmd);
        out("\n");
        return;
    }

    if (seq(cmd, "help") || seq(cmd, "h")) {
        out("Commands:\n");
        out("  run (r)             Start/restart the program\n");
        out("  break (b) <loc>     Set breakpoint (func, file:line, *addr)\n");
        out("  delete (d) <id>     Delete breakpoint\n");
        out("  continue (c)        Continue execution\n");
        out("  step (s)            Single-step one instruction\n");
        out("  next (n)            Step over function calls\n");
        out("  print (p) <expr>    Print register ($x0, $pc) or symbol\n");
        out("  backtrace (bt)      Show call stack\n");
        out("  info reg            Show all registers\n");
        out("  info break          Show breakpoints\n");
        out("  disassemble (disas) Disassemble around PC or address\n");
        out("  examine (x) <addr>  Show memory at address\n");
        out("  list (l)            Show source around current line\n");
        out("  quit (q)            Exit debugger\n");
        return;
    }

    out("Unknown command: ");
    out(cmd);
    out(". Type 'help' for help.\n");
}

/* ---- Entry point ---- */

int main(int argc, char **argv)
{
    int n;

    /* Handle --version early */
    if (argc >= 2 && seq(argv[1], "--version")) {
        write_str(1, "GNU gdb (free-dbg) 14.1\n");
        sys_exit(0);
    }

    if (argc < 2) {
        write_str(2, "Usage: free-dbg <executable> [args...]\n");
        sys_exit(1);
    }

    target_path = argv[1];
    target_argv = &argv[1];

    /* Load and parse ELF */
    file_size = read_file(target_path, file_buf, FILE_BUF_SIZE);
    parse_elf();
    load_symtab();
    parse_debug_line();

    out("free-dbg: loaded ");
    out(target_path);
    out("\n");

    if (symtab_count > 0) {
        print_dec((u64)symtab_count);
        out(" symbols");
    }
    if (line_table_count > 0) {
        out(", ");
        print_dec((u64)line_table_count);
        out(" line entries");
    }
    out("\n");

    child_started = 0;
    child_running = 0;
    last_cmd[0] = '\0';

    for (;;) {
        out("(dbg) ");
        n = read_line(input_buf, MAX_INPUT);
        if (n < 0) {
            out("\n");
            break;
        }
        process_command(input_buf);
    }

    if (child_started) {
        sys_kill(child_pid, SIGKILL);
        sys_wait4(child_pid, NULL, 0, NULL);
    }

    return 0;
}
