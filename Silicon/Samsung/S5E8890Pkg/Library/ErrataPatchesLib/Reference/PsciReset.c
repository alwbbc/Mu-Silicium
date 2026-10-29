/*
 * hero2lte (Exynos 8890): Samsung's EL3 implements PSCI 0.1 only, so the
 * PSCI SYSTEM_RESET / SYSTEM_OFF calls Windows makes return NOT_SUPPORTED and
 * HalReturnToFirmware bugchecks 0xA0 (BEEF, 8004, 84000009, FFFFFFFF).
 *
 * HalReturnToFirmware already has an EFI ResetSystem path that is only taken
 * when the PSCI reset/off capability bits are clear:
 *     ldr w8, [x20, #0x60] ; tbz w8, #8,  efi_path ; ldr x9, [x20, #0x40]   (reset)
 *     ldr w8, [x19, #0x60] ; tbz w8, #10, efi_path ; ldr x9, [x19, #0x40]   (off)
 * Turn the tbz into an unconditional b to the same target, so reset/off go
 * through EFI ResetSystem (PMU SWRESET / PS_HOLD in S5E8890Pkg).
 *
 * Runs in winload right before OslArm64TransferToKernel; x0 = loader block.
 * Built position independent; see BuildShellCode.sh.
 */
typedef unsigned long long u64;
typedef unsigned int u32;

struct list { struct list *f, *b; };

static inline void sync_icache(u32 *p)
{
  __asm__ volatile ("dc cvau, %0\n dsb ish\n ic ivau, %0\n dsb ish\n isb" :: "r"(p) : "memory");
}

void PsciResetPatch(void *loader_block)
{
  struct list *head = (struct list *)((char *)loader_block + 0x10);   /* LoadOrderListHead */
  struct list *e = head->f;
  if (e == head) return;
  /* first entry is ntoskrnl: DllBase at +0x30, SizeOfImage at +0x40 */
  u32 *w = *(u32 **)((char *)e + 0x30);
  u32 n = *(u32 *)((char *)e + 0x40) / 4;
  for (u32 i = 0; i + 2 < n; i++) {
    u32 t = w[i + 1];
    if ((t & 0x7F00001Fu) != 0x36000008u) continue;               /* tbz w8, #b, ... */
    if (w[i] == 0xB9406288u && w[i + 2] == 0xF9402289u && t == 0x364001A8u) { /* reset */
      w[i + 1] = 0x14000000u | ((t >> 5) & 0x3FFFu);              /* b same target */
      sync_icache(&w[i + 1]);
    } else if (w[i] == 0xB9406268u && w[i + 2] == 0xF9402269u && t == 0x365001A8u) { /* off */
      w[i + 1] = 0x14000000u | ((t >> 5) & 0x3FFFu);
      sync_icache(&w[i + 1]);
    }
  }
}
