# Kbuild simulation Makefile
# Mimics the Linux kernel build structure using the free toolchain

TOPDIR := $(shell cd ../../.. && pwd)
BUILDDIR := $(TOPDIR)/build

CC      := $(BUILDDIR)/free-cc
AS      := $(BUILDDIR)/free-as
LD      := $(BUILDDIR)/free-ld
AR      := $(BUILDDIR)/free-ar
NM      := $(BUILDDIR)/free-nm
OBJCOPY := $(BUILDDIR)/free-objcopy
STRIP   := $(BUILDDIR)/free-strip

# Kernel-like compiler flags
KBUILD_CFLAGS := -std=gnu89 \
	-nostdinc \
	-ffreestanding \
	-D__KERNEL__ \
	-ffunction-sections \
	-fdata-sections \
	-mgeneral-regs-only \
	-c

INCLUDES := -I.

# Source files (mimic obj-y += pattern from Kbuild)
obj-y := fs.o list.o printk.o main.o
asm-y := head.o

# Targets
all: Image vmlinux.stripped vmlinux.sym

# Step 1: Compile C files
%.o: %.c
	$(CC) $(KBUILD_CFLAGS) $(INCLUDES) -o $@ $<

# Step 2: Assemble .S files
head.o: head.S
	$(AS) $< -o $@

# Step 3: Create built-in.a archive
built-in.a: $(obj-y)
	$(AR) rcs $@ $(obj-y)

# Step 4: Link into vmlinux
vmlinux: built-in.a $(asm-y)
	$(LD) -T vmlinux.lds -o $@ $(asm-y) --whole-archive built-in.a --no-whole-archive

# Step 5: Binary image
Image: vmlinux
	$(OBJCOPY) -O binary $< $@

# Step 6: Symbol list
vmlinux.sym: vmlinux
	$(NM) -n $< > $@

# Step 7: Stripped version
vmlinux.stripped: vmlinux
	$(STRIP) --strip-debug -o $@ $<

clean:
	rm -f *.o built-in.a vmlinux Image vmlinux.sym vmlinux.stripped

.PHONY: all clean
