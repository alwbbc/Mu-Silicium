#!/bin/bash
# Standalone hero2lte probe: dumps Decon/VPP/DSIM registers into the
# exynos-snapshot kernel log (0x91A00000) so TWRP's /proc/last_kmsg shows them.
set -e
cd "$(dirname "$0")"
# Optional: TRIG_MODE=1 (TE trigger test) or 2 (SW trigger test) instead of the register dump
clang --target=aarch64-none-elf -ffreestanding -fno-builtin -nostdlib -O1 -fno-pic -mgeneral-regs-only -mstrict-align ${TRIG_MODE:+-DTRIG_MODE=$TRIG_MODE} -c diag.c -o diag.o
clang --target=aarch64-none-elf -c start.S -o start.o
ld.lld -static -T link.ld start.o diag.o -o diag.elf
llvm-objcopy -O binary diag.elf diag.bin
python3 ../hero2lte_mkboot.py diag.bin ../../DTBs/hero2lte-dt.img ../../../Mu-hero2lte-diag.img ../../ramdisk
