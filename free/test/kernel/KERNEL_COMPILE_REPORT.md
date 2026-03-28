# Linux Kernel Compilation Report

## Summary

free-cc can compile **110 Linux kernel C source files** when paired with
minimal stub headers. All outputs are valid ELF 64-bit LSB relocatable
aarch64 objects.

### Real kernel source files: 55

| Category                  | Files                                                                      | Count |
|---------------------------|----------------------------------------------------------------------------|-------|
| lib/ core                 | ctype, string, sort, bsearch, hexdump, kasprintf, hweight                  | 7     |
| lib/ data structs         | list_sort, rbtree, llist, find_bit, bitmap, plist, timerqueue, klist, interval_tree, kfifo | 10 |
| lib/ parsing              | kstrtox, cmdline, argv_split, parser, uuid, glob                           | 6     |
| lib/math/                 | gcd, lcm, int_pow, int_sqrt, div64, rational, reciprocal_div, int_log      | 8     |
| lib/ string & formatting  | string_helpers, seq_buf, errname, net_utils                                | 4     |
| lib/ bit & byte           | bcd, bitrev, clz_tab, clz_ctz, memweight                                  | 5     |
| lib/ sync & locking       | dec_and_lock, debug_locks, ratelimit, refcount                             | 4     |
| lib/ networking           | nlattr, win_minmax, dynamic_queue_limits, checksum                         | 4     |
| lib/ I/O & user space     | iomap_copy, bust_spinlocks, strnlen_user, strncpy_from_user, usercopy      | 5     |
| lib/ misc                 | cpumask, flex_proportions                                                  | 2     |

### Kernel-pattern test files: 48

| Category                  | Files                                                                      | Count |
|---------------------------|----------------------------------------------------------------------------|-------|
| Data structures           | linked_list, rbtree, hash_table, llist, plist, bitmap, ring_buffer         | 7     |
| Synchronization           | spinlock, rwlock, atomic, xchg, refcount                                   | 5     |
| Memory management         | slab_cache, kref, mempool, goto_cleanup, error_handling                    | 5     |
| Device patterns           | kobject, callback, iomem, scatterlist, workqueue, waitqueue                | 6     |
| String & I/O              | string_ops, ctype, seq_file, hex_dump, printk, variadic                    | 6     |
| Type system               | container_of, designated_init, typeof, static_assert, attribute            | 5     |
| Control flow              | state_machine, errno, notifier_chain, export_symbol                        | 4     |
| Architecture              | inline_asm, endian, percpu                                                 | 3     |
| Math                      | math64, minmax, sort_bsearch, bitops, timer, uuid, klist                   | 7     |

### Existing kernel test files: 7

kernel_test through kernel_test8 (excluding kernel_test4 which has
inline asm operand syntax issues).

## Compiler Flags Used

```
free-cc -nostdinc -std=gnu89 -Itest/kernel/include \
  -ffreestanding -D__KERNEL__ -DCONFIG_64BIT -D__NO_FORTIFY \
  -c -o output.o input.c
```

Key flags:
- `-std=gnu89`: Required for `inline`, `__restrict__`, designated
  initializers, compound literals, variadic macros, typeof, statement
  expressions
- `-nostdinc`: Prevents pulling in the toolchain's own libc headers
- `-ffreestanding`: Freestanding compilation mode

## Stub Headers

Over 150 stub headers in `test/kernel/include/` replace complex real kernel
headers with definitions sufficient for compilation. Key additions for the
expansion to 110 files:

- `linux/xarray.h` - XArray data structure types and operations
- `linux/idr.h` - ID radix tree types
- `linux/cpumask.h` - CPU mask operations
- `linux/workqueue.h` - Work queue types and macros
- `linux/scatterlist.h` - Scatter-gather I/O types
- `linux/kfifo.h` - Kernel FIFO buffer types
- `linux/interval_tree.h`, `linux/interval_tree_generic.h` - Interval tree macros
- `linux/flex_proportions.h` - Flexible proportions tracking
- `linux/percpu_counter.h` - Per-CPU counters
- `linux/dcache.h` - Dentry cache structures
- `linux/sched.h` - Task and mm_struct definitions
- `linux/rculist.h` - RCU-protected list operations
- `net/checksum.h` - Network checksum operations
- `asm/io.h` - Memory-mapped I/O operations

## Compiler Features Exercised

The 110 kernel files exercise these GNU C and compiler features:

1. **typeof / __typeof__** - Used pervasively in kernel macros
2. **Statement expressions `({ ... })`** - min/max, container_of, swap
3. **GNU-style variadic macros** - `fmt, args...` syntax
4. **Designated initializers** - `.field = val` in struct init
5. **Compound literals** - `(struct type){ ... }` expressions
6. **`__attribute__` (many forms)** - section, aligned, used, weak, packed,
   always_inline, noinline, pure, const, deprecated, visibility, alias,
   nonnull, format, unused, cold, hot, noreturn
7. **`__builtin_expect`** - likely/unlikely branch hints
8. **`__builtin_constant_p`** - Compile-time constant detection
9. **`__builtin_memcmp`** - Builtin memory comparison
10. **`__builtin_trap`** - BUG() implementation
11. **`_Static_assert`** - Compile-time assertions (with and without message)
12. **`__restrict__`** - Pointer aliasing in gnu89 mode
13. **`inline` in gnu89** - Inline functions
14. **Zero-length arrays** - Flexible array members
15. **Anonymous unions/structs** - Nested anonymous aggregates
16. **Bitfield operations** - Extensive bitfield manipulation
17. **Complex pointer arithmetic** - Container_of, list operations
18. **Function pointers in structs** - Callback patterns (rbtree, sort, ops tables)
19. **Recursive data structures** - Linked lists, trees, hash tables
20. **Complex preprocessor macros** - Token pasting, multi-level expansion
21. **`bool` type** - Used in glob.c, string_helpers.c
22. **`fallthrough` attribute** - Switch case fall-through annotation
23. **Inline assembly** - AArch64 barriers (dmb, dsb, isb), register reads (clz, rbit)
24. **va_list / va_start / va_arg / va_end** - Variadic function implementation
25. **DECLARE_BITMAP** - Compile-time bitmap allocation
26. **INTERVAL_TREE_DEFINE** - Macro-generated typed data structures
27. **Scatterlist iteration** - for_each_sg patterns
28. **Work queue patterns** - INIT_WORK, schedule_work, container_of in callbacks
29. **ERR_PTR/IS_ERR/PTR_ERR** - Error pointer encoding/decoding
30. **goto-based cleanup** - Multi-level resource cleanup on error

## How to Run

```bash
# Clone kernel source (one-time)
git clone --depth=1 --filter=blob:none --sparse \
  https://github.com/torvalds/linux.git /tmp/linux
cd /tmp/linux
git sparse-checkout set --skip-checks \
  lib/ctype.c lib/string.c lib/sort.c lib/bsearch.c lib/hexdump.c \
  lib/kasprintf.c lib/kstrtox.c lib/kstrtox.h lib/argv_split.c \
  lib/cmdline.c lib/find_bit.c lib/bitmap.c lib/list_sort.c \
  lib/rbtree.c lib/hweight.c lib/llist.c lib/parser.c lib/uuid.c \
  lib/math/gcd.c lib/math/lcm.c lib/math/int_pow.c lib/math/int_sqrt.c \
  lib/math/div64.c lib/math/rational.c lib/math/reciprocal_div.c \
  lib/math/int_log.c lib/plist.c lib/bcd.c lib/clz_tab.c lib/bitrev.c \
  lib/errname.c lib/clz_ctz.c lib/memweight.c lib/string_helpers.c \
  lib/seq_buf.c lib/win_minmax.c lib/nlattr.c lib/glob.c \
  lib/dec_and_lock.c lib/debug_locks.c lib/iomap_copy.c \
  lib/bust_spinlocks.c lib/strnlen_user.c lib/strncpy_from_user.c \
  lib/net_utils.c lib/dynamic_queue_limits.c lib/ratelimit.c \
  lib/refcount.c lib/timerqueue.c lib/klist.c lib/usercopy.c \
  lib/checksum.c lib/interval_tree.c lib/cpumask.c \
  lib/flex_proportions.c lib/kfifo.c

# Run compilation test
cd /path/to/free
./test/kernel/run_real_kernel.sh /tmp/linux
```
