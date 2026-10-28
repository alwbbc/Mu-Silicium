/* ARM64 Linux-style header so S-Boot accepts it as a kernel */
.section .text.head, "ax"
.global _start
_start:
  b     entry
  .long 0
  .quad 0x80000          /* text_offset */
  .quad _end - _start    /* image size  */
  .quad 0, 0, 0, 0
  .ascii "ARM\x64"
  .long 0
entry:
  mov   x19, x0          /* DTB */
  mov   x20, x1
  adr   x21, _start      /* where we were loaded */
  mrs   x22, CurrentEL
  ldr   x9, =0xB3000000  /* stack in free RAM */
  mov   sp, x9
  mov   x0, x19
  mov   x1, x21
  mov   x2, x22
  bl    diag_main
1: wfe
  b     1b
