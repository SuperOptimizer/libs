/*
 * mock_proc.S - CPU feature detection and processor setup
 * Exercises: mrs with ID registers, ubfx/sbfx bitfield extract,
 * conditional branches, movz+movk wide immediates, logical ops,
 * .set/.equ constants, .ifdef conditional assembly
 */

/* Feature bit constants */
.set SCTLR_EL1_M,  0x1      /* MMU enable */
.set SCTLR_EL1_A,  0x2      /* Alignment check */
.set SCTLR_EL1_C,  0x4      /* Data cache enable */
.set SCTLR_EL1_I,  0x1000   /* Instruction cache enable */

.set TCR_T0SZ,     16       /* 48-bit VA for TTBR0 */
.set TCR_T1SZ,     16       /* 48-bit VA for TTBR1 */
.set TCR_TG0_4K,   0x0      /* 4KB granule for TTBR0 */
.set TCR_TG1_4K,   0x2      /* 4KB granule for TTBR1 */

.section ".text"

/*
 * get_current_el - Return the current exception level (0-3)
 * Returns: x0 = current EL number
 */
.global get_current_el
.type get_current_el, %function
get_current_el:
    mrs x0, currentel
    /* CurrentEL field is bits [3:2] */
    ubfx x0, x0, #2, #2
    ret

/*
 * check_cpu_features - Detect CPU features via ID registers
 * Returns: x0 = feature bitmask
 *   bit 0: has atomic instructions (LSE)
 *   bit 1: has CRC32
 *   bit 2: has SHA256
 *   bit 3: has AES
 */
.global check_cpu_features
.type check_cpu_features, %function
check_cpu_features:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x0, #0              /* result bitmask */

    /* Read ID_AA64ISAR0_EL1 - not in our sysreg table,
     * so we test with a known register instead.
     * In real kernel code: mrs x1, id_aa64isar0_el1 */
    mrs x1, currentel       /* placeholder */

    /* Test: extract bits and conditionally set flags */
    /* Atomic (bits [23:20]) */
    ubfx x2, x1, #20, #4
    cbz x2, 1f
    orr x0, x0, #1          /* set bit 0: has atomics */
1:

    /* CRC32 (bits [19:16]) */
    ubfx x2, x1, #16, #4
    cbz x2, 2f
    orr x0, x0, #2          /* set bit 1: has CRC32 */
2:

    /* SHA2 (bits [15:12]) */
    ubfx x2, x1, #12, #4
    cbz x2, 3f
    orr x0, x0, #4          /* set bit 2: has SHA256 */
3:

    /* AES (bits [7:4]) */
    ubfx x2, x1, #4, #4
    cbz x2, 4f
    orr x0, x0, #8          /* set bit 3: has AES */
4:

    ldp x29, x30, [sp], #16
    ret

/*
 * setup_mmu_regs - Configure MMU control registers (does not enable MMU)
 * Demonstrates wide immediate loading and system register writes
 */
.global setup_mmu_regs
.type setup_mmu_regs, %function
setup_mmu_regs:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    /* Load SCTLR_EL1 */
    mrs x0, sctlr_el1
    /* Clear MMU, alignment, cache bits */
    movz x1, #0xFFFF
    movk x1, #0xFFFF, lsl #16
    movk x1, #0xFFFF, lsl #32
    movk x1, #0xFFFF, lsl #48
    /* mask = ~(M | A | C | I) */
    mov x2, #0x1007
    mvn x2, x2
    and x0, x0, x2
    msr sctlr_el1, x0
    isb

    /* Load TCR_EL1 with 48-bit VA, 4K granule */
    /* TCR value: T0SZ=16, T1SZ=16<<16, TG0=4K, TG1=4K<<30 */
    movz x0, #16            /* T0SZ */
    movk x0, #16, lsl #16   /* T1SZ */
    msr tcr_el1, x0
    isb

    /* Zero out TTBR0/TTBR1 */
    mov x0, #0
    msr ttbr0_el1, x0
    msr ttbr1_el1, x0
    isb

    /* Set MAIR_EL1 with memory attribute types */
    /* Attr0: Device-nGnRnE (0x00)
     * Attr1: Normal, Inner/Outer WB (0xFF)
     * Attr2: Normal, Non-cacheable (0x44) */
    movz x0, #0xFF00
    movk x0, #0x0044, lsl #16
    msr mair_el1, x0
    isb

    ldp x29, x30, [sp], #16
    ret

/*
 * cpu_relax - Low-power wait hint
 * Exercises: yield, wfe, nop patterns
 */
.global cpu_relax
.type cpu_relax, %function
cpu_relax:
    yield
    ret

/*
 * spin_lock - Simple spinlock acquire
 * Exercises: ldxr/stxr loop with wfe
 */
.global spin_lock
.type spin_lock, %function
spin_lock:
1:  ldxr w1, [x0]
    cbnz w1, 2f
    mov w2, #1
    stxr w3, w2, [x0]
    cbnz w3, 1b
    /* Acquired - memory barrier */
    dmb ish
    ret
2:  /* Contended - wait for event */
    wfe
    b 1b

/*
 * spin_unlock - Simple spinlock release
 */
.global spin_unlock
.type spin_unlock, %function
spin_unlock:
    dmb ish
    mov w1, #0
    str w1, [x0]
    sev
    ret

/*
 * memcpy_aligned - 8-byte aligned memcpy using ldp/stp
 * x0 = dst, x1 = src, x2 = len (must be multiple of 16)
 */
.global memcpy_aligned
.type memcpy_aligned, %function
memcpy_aligned:
    cbz x2, 2f
1:  ldp x3, x4, [x1], #16
    stp x3, x4, [x0], #16
    sub x2, x2, #16
    cbnz x2, 1b
2:  ret

/*
 * Data section with spinlock variable
 */
.section ".data"
.balign 4
.global test_lock
test_lock:
    .word 0
