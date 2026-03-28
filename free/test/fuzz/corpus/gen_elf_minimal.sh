#!/bin/sh
# gen_elf_minimal.sh - Generate a minimal ELF64 binary for fuzz corpus.
# Creates elf_minimal.bin in the same directory.
# chmod +x gen_elf_minimal.sh
#
# The generated file is a valid ELF64 aarch64 relocatable header (64 bytes).
# Uses octal printf escapes for maximum portability.

OUTDIR="$(dirname "$0")"
OUTFILE="${OUTDIR}/elf_minimal.bin"

# Use python if available for reliable binary output, else try printf with octal
if command -v python3 >/dev/null 2>&1; then
    python3 -c "
import struct, sys
# ELF64 header (64 bytes)
ident = b'\\x7fELF'       # magic
ident += b'\\x02'          # ELFCLASS64
ident += b'\\x01'          # ELFDATA2LSB
ident += b'\\x01'          # EV_CURRENT
ident += b'\\x00'          # ELFOSABI_NONE
ident += b'\\x00' * 8      # padding
ehdr = ident
ehdr += struct.pack('<H', 1)    # e_type = ET_REL
ehdr += struct.pack('<H', 183)  # e_machine = EM_AARCH64
ehdr += struct.pack('<I', 1)    # e_version
ehdr += struct.pack('<Q', 0)    # e_entry
ehdr += struct.pack('<Q', 0)    # e_phoff
ehdr += struct.pack('<Q', 0)    # e_shoff
ehdr += struct.pack('<I', 0)    # e_flags
ehdr += struct.pack('<H', 64)   # e_ehsize
ehdr += struct.pack('<H', 56)   # e_phentsize
ehdr += struct.pack('<H', 0)    # e_phnum
ehdr += struct.pack('<H', 64)   # e_shentsize
ehdr += struct.pack('<H', 0)    # e_shnum
ehdr += struct.pack('<H', 0)    # e_shstrndx
sys.stdout.buffer.write(ehdr)
" > "${OUTFILE}"
elif command -v python >/dev/null 2>&1; then
    python -c "
import struct, sys
ident = b'\\x7fELF\\x02\\x01\\x01\\x00' + b'\\x00' * 8
ehdr = ident
ehdr += struct.pack('<HHIQQQIHHHHHH', 1, 183, 1, 0, 0, 0, 0, 64, 56, 0, 64, 0, 0)
sys.stdout.write(ehdr)
" > "${OUTFILE}"
else
    # Fallback: use printf with octal escapes
    # octal: \177=0x7f  \105=E  \114=L  \106=F
    #        \002=ELFCLASS64  \001=LSB  \001=EV_CURRENT
    printf '\177ELF\002\001\001\000' > "${OUTFILE}"
    printf '\000\000\000\000\000\000\000\000' >> "${OUTFILE}"
    printf '\001\000' >> "${OUTFILE}"             # e_type = ET_REL
    printf '\267\000' >> "${OUTFILE}"             # e_machine = 183
    printf '\001\000\000\000' >> "${OUTFILE}"     # e_version
    printf '\000\000\000\000\000\000\000\000' >> "${OUTFILE}"  # e_entry
    printf '\000\000\000\000\000\000\000\000' >> "${OUTFILE}"  # e_phoff
    printf '\000\000\000\000\000\000\000\000' >> "${OUTFILE}"  # e_shoff
    printf '\000\000\000\000' >> "${OUTFILE}"     # e_flags
    printf '\100\000' >> "${OUTFILE}"             # e_ehsize = 64
    printf '\070\000' >> "${OUTFILE}"             # e_phentsize = 56
    printf '\000\000' >> "${OUTFILE}"             # e_phnum
    printf '\100\000' >> "${OUTFILE}"             # e_shentsize = 64
    printf '\000\000' >> "${OUTFILE}"             # e_shnum
    printf '\000\000' >> "${OUTFILE}"             # e_shstrndx
fi

SIZE=$(wc -c < "${OUTFILE}" | tr -d ' ')
echo "Generated ${OUTFILE} (${SIZE} bytes)"
