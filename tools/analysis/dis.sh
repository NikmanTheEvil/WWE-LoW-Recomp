#!/bin/bash
# dis.sh <start_hex> <end_hex> [image.bin] - disassembles a range of the memory image
# written by xexdump (base 0x82000000). Needs llvm-objcopy / llvm-objdump.
S=$(( $1 )); E=$(( $2 )); B=$((0x82000000))
IMG=${3:-${LOW_IMAGE:-image.bin}}
TMP=$(mktemp -d)
dd if="$IMG" of="$TMP/dis.bin" bs=4 skip=$(( (S-B)/4 )) count=$(( (E-S)/4 )) 2>/dev/null
llvm-objcopy -I binary -O elf64-powerpc --rename-section=.data=.text,code,alloc,load "$TMP/dis.bin" "$TMP/dis.elf"
llvm-objdump -d --no-show-raw-insn --no-leading-addr --adjust-vma=$S --triple=powerpc64-unknown-linux-gnu --mattr=+altivec "$TMP/dis.elf" \
  | tail -n +7 | sed -E 's/ <_binary[^>]*>//' | awk -v s=$S '{printf "%08X  %s\n", s+(NR-1)*4, $0}'
rm -rf "$TMP"
