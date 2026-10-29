#!/bin/sh
# Build the PSCI reset patch prologue and prepend it to the CNTFRQ shell code.
set -e
cd "$(dirname "$0")"
clang --target=aarch64-none-elf -O2 -ffreestanding -fno-stack-protector -fPIC -mgeneral-regs-only \
      -fno-asynchronous-unwind-tables -c PsciReset.c -o /tmp/psci.o
cat > /tmp/pro.S <<'ASM'
  .text
  .globl _start
_start:
  stp x0, x1, [sp, #-32]!
  str x30, [sp, #16]
  bl  PsciResetPatch
  ldr x30, [sp, #16]
  ldp x0, x1, [sp], #32
  b   end_of_prologue
ASM
clang --target=aarch64-none-elf -c /tmp/pro.S -o /tmp/pro.o
printf 'SECTIONS { . = 0; .text : { /tmp/pro.o(.text) /tmp/psci.o(.text*) end_of_prologue = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }' > /tmp/ld.x
ld.lld -T /tmp/ld.x /tmp/pro.o /tmp/psci.o -o /tmp/psci.elf
llvm-objcopy -O binary --only-section=.text /tmp/psci.elf /tmp/psci.bin
llvm-objdump -d /tmp/psci.elf
