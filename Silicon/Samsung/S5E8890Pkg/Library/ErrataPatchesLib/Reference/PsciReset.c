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

/*
 * SMP: Samsung's PSCI 0.1 CPU_ON starts the core at the entry point but does
 * not pass the context id (x0 = garbage, x1 = entry), so Windows' secondary
 * startup code never gets its context and the other cores never come up.
 *
 * Fix, all set up here before the kernel runs:
 *   - slot page = slack after ntoskrnl's .data section (mapped, zero, unused):
 *       +0x00 + aff0*16 : { entry, context } for each core
 *       +0x80           : physical address of the trampoline
 *       +0x100          : trampoline (runs with the MMU off on the new core):
 *                         x0 = context, br entry
 *   - hook = slack after .text: called instead of the SMC stub at the CPU_ON
 *     call site; saves { entry, context } in the core's slot, cleans it to
 *     memory and passes the trampoline as the entry point instead.
 */
struct sect { char name[8]; u32 vsize, va, rawsize, rawptr, r0, r1; unsigned short n0, n1; u32 ch; };

static inline void clean_poc(void *p)
{
  __asm__ volatile ("dc cvac, %0\n dsb sy" :: "r"(p) : "memory");
}

static u64 va_to_pa(u64 va)
{
  u64 par;
  __asm__ volatile ("at s1e1r, %1\n isb\n mrs %0, par_el1" : "=r"(par) : "r"(va));
  if (par & 1) return 0;
  return (par & 0xFFFFFFFFF000ull) | (va & 0xFFF);
}

static u32 enc_b(u64 from, u64 to, u32 op) { return op | (u32)(((to - from) >> 2) & 0x3FFFFFF); }

void SmpPatch(void *loader_block)
{
  struct list *head = (struct list *)((char *)loader_block + 0x10);
  struct list *e = head->f;
  if (e == head) return;
  char *base = *(char **)((char *)e + 0x30);
  u32 size = *(u32 *)((char *)e + 0x40);
  char *nt = base + *(u32 *)(base + 0x3C);
  unsigned short nsec = *(unsigned short *)(nt + 6), opt = *(unsigned short *)(nt + 20);
  struct sect *s = (struct sect *)(nt + 24 + opt);
  char *text = 0, *data = 0; u32 textend = 0, dataend = 0;
  for (u32 i = 0; i < nsec; i++) {
    if (s[i].name[0] == '.' && s[i].name[1] == 't' && s[i].name[2] == 'e' && s[i].name[3] == 'x' && s[i].name[4] == 't' && !s[i].name[5])
      { text = base + s[i].va; textend = s[i].vsize; }
    if (s[i].name[0] == '.' && s[i].name[1] == 'd' && s[i].name[2] == 'a' && s[i].name[3] == 't' && s[i].name[4] == 'a' && !s[i].name[5])
      { data = base + s[i].va; dataend = s[i].vsize; }
  }
  if (!text || !data) return;

  /* CPU_ON call site: ldr x9, [x20, #0x40] ; ldr x1, [sp, #0x10] ; bl smc_stub */
  u32 *w = (u32 *)base, n = size / 4, site = 0;
  for (u32 i = 0; i + 2 < n; i++)
    if (w[i] == 0xF9402289u && w[i + 1] == 0xF9400BE1u && (w[i + 2] >> 26) == 0x25) { site = i + 2; break; }
  if (!site) return;
  /* the function must load CPU_ON (0xC4000003) */
  {
    int ok = 0;
    for (u32 j = site - 16; j < site; j++)
      if ((w[j] & 0xFF000000u) == 0x18000000u) {               /* ldr wN, literal */
        int imm = (int)((w[j] >> 5) & 0x7FFFF); if (imm & 0x40000) imm -= 0x80000;
        if (w[j + imm] == 0xC4000003u) ok = 1;
      }
    if (!ok) return;
  }
  u64 sitepc = (u64)&w[site];
  int simm = (int)(w[site] & 0x3FFFFFF); if (simm & 0x2000000) simm -= 0x4000000;
  u64 stub = sitepc + (u64)(long long)simm * 4;

  /* caves */
  u64 hook = ((u64)text + textend + 15) & ~15ull;
  u64 slot = ((u64)data + dataend + 0xFF) & ~0xFFull;
  if ((hook & 0xFFF) > 0xFC0 || (slot & 0xFFF) > 0xE00) return;     /* must stay in the page */
  for (u32 k = 0; k < 16; k++) if (((u32 *)hook)[k]) return;
  for (u32 k = 0; k < 128; k++) if (((u32 *)slot)[k]) return;
  u64 tramp = slot + 0x100, tramppa = va_to_pa(tramp);
  if (!tramppa) return;

  /* trampoline + its physical address */
  static const u32 tr[6] = { 0x10FFF80Au, 0xD53800ABu, 0x92401D6Bu, 0x8B0B114Bu, 0xA9400161u, 0xD61F0020u };
  for (u32 k = 0; k < 6; k++) ((u32 *)tramp)[k] = tr[k];
  *(u64 *)(slot + 0x80) = tramppa;
  for (u64 p = slot; p < slot + 0x140; p += 64) clean_poc((void *)p);

  /* hook: adrp/add x10 = slot ; save ; x2 = trampoline ; b smc_stub */
  u32 *h = (u32 *)hook;
  long long pages = (long long)((slot >> 12) - (hook >> 12));
  h[0] = 0x9000000Au | (u32)((pages & 3) << 29) | (u32)(((pages >> 2) & 0x7FFFF) << 5);
  h[1] = 0x9100014Au | (u32)((slot & 0xFFF) << 10);
  h[2] = 0x92401C2Bu; h[3] = 0x8B0B114Bu; h[4] = 0xA9000D62u; h[5] = 0xD50B7A2Bu; h[6] = 0xD5033F9Fu;
  h[7] = 0xF9404142u;
  h[8] = enc_b(hook + 32, stub, 0x14000000u);
  for (u32 k = 0; k < 9; k++) sync_icache(&h[k]);

  w[site] = enc_b(sitepc, hook, 0x94000000u);                  /* bl hook */
  sync_icache(&w[site]);
}
