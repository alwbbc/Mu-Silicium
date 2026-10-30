/** @file
  hero2lte boot selector:
    Volume Up   = Windows (installed; WinPE if none is installed)
    Volume Down = Linux
    Home        = WinPE (Windows setup / maintenance)
    Power       = UEFI Shell
  Defaults to Windows after 5 seconds.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/ArmLib.h>
#include <Library/CacheMaintenanceLib.h>

/* ---- SMP probe: can Samsung's EL3 start the other A53 cores? ---------- */
/* secondary entry (MMU off), source SmpGicProbe.S.txt: box = page+0x800, per CPU 32 bytes:
 * +0 0xC0DE0000, +8 GICD_ISENABLER0 after a non-secure write of 0x4800FFFF (SGI0-15,
 * PPI27 virt timer, PPI30 phys timer: only non-secure bits stick), +16 CNTVCT-CNTPCT,
 * +24 GICD_ISENABLER0 before; then PSCI CPU_OFF */
STATIC CONST UINT32 mCpuProbeCode[] = {
  0x10004009, 0xD53800AB, 0x92401D6B, 0x8B0B152A, 0xD2A2200E, 0xF282000E, 0xB94101CD, 0xF9000D4D,
  0x52A9000C, 0x729FFFEC, 0xB90101CC, 0xD5033F9F, 0xB94101CD, 0xF900054D, 0xD503201F, 0xD5033F9F,
  0xD5033FDF, 0xD53BE04C, 0xD53BE02D, 0xCB0D018C, 0xF900094C, 0x52B81BCD, 0xB900014D, 0xD5033F9F,
  0x52B08000, 0x72800040, 0xD4000003, 0xD503205F, 0x17FFFFFF

};

/* SMP trampoline (diag+0x400) and exception catcher vectors (diag+0x800) used by
 * ErrataPatchesLib's SmpPatch; source: SmpTrampoline.S.txt next to this file */
STATIC CONST UINT32 mSmpTrampoline[] = {
  0x10000009, 0xD110012C, 0xF940412A, 0xD53800AB, 0x92401D6B, 0x8B0B114D, 0xA94001A1, 0x8B0B158E,
  0x52AF400F, 0x2A0B01EF, 0xB90041CF, 0xA90485C0, 0xD2A61A0F, 0xF281000F, 0xD518100F, 0x9120018F,
  0xD518C00F, 0xD5033FDF, 0xD5033F9F, 0xD61F0020, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801C05, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801C25, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801C45, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801C65, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801C85, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801CA5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801CC5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801CE5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801D05, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801D25, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801D45, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801D65, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801D85, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801DA5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801DC5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F, 0xD503201F,
  0xD5385200, 0xD5384021, 0xD5386002, 0x10000003, 0x9274CC63, 0xD53800A4, 0x92401C84, 0x8B041463,
  0x910C0063, 0xA9000460, 0xD2801DE5, 0xA9011462, 0xD5033F9F, 0xD503205F, 0x17FFFFFF
};

STATIC VOID
SmpInstallTrampoline (VOID)
{
  VOID *T = (VOID *)(UINTN)0xFDC00400;
  CopyMem (T, mSmpTrampoline, sizeof (mSmpTrampoline));
  WriteBackInvalidateDataCacheRange (T, sizeof (mSmpTrampoline));
  InvalidateInstructionCacheRange (T, sizeof (mSmpTrampoline));
}

/* log left by the previous Windows boot (ErrataPatchesLib SmpPatch) */
STATIC VOID
SmpDiag (VOID)
{
  volatile UINT64 *D = (volatile UINT64 *)(UINTN)0xFDC00000;
  UINTN            Cpu;

  InvalidateDataCacheRange ((VOID *)D, EFI_PAGE_SIZE);
  Print (L"  SMP: boot#%ld patch %lx timer %lx slot %lx", D[0x40], D[0], D[2], D[1]);
  for (Cpu = 1; Cpu < 4; Cpu++)
    Print (L" | %x ctx %lx", (UINT32)D[8 + Cpu * 4], D[9 + Cpu * 4]);
  Print (L"\n");
  for (Cpu = 0; Cpu < 4; Cpu++)        /* sctlr, cntfrq, cntvct-cntpct, gicd itargetsr0, mark2 */
    Print (L"  c%d: sc %lx f %lx vo %lx gi %lx %x\n", Cpu, D[0x20 + Cpu * 8], D[0x21 + Cpu * 8],
           D[0x22 + Cpu * 8], D[0x23 + Cpu * 8], (UINT32)D[0x24 + Cpu * 8]);
  for (Cpu = 1; Cpu < 4; Cpu++)        /* exceptions caught before Windows set its own VBAR */
    if (D[0x63 + Cpu * 4])
      Print (L"  c%d EXC vec %lx esr %lx elr %lx far %lx\n", Cpu, D[0x63 + Cpu * 4] - 0xE0, D[0x60 + Cpu * 4],
             D[0x61 + Cpu * 4], D[0x62 + Cpu * 4]);
  if (D[0] == 0x5A0F && D[1] >= 0x880000000ull && D[1] < 0x900000000ull) {   /* only trust a complete SmpPatch log */
    volatile UINT64 *S = (volatile UINT64 *)(UINTN)D[1];
    Print (L"  slot:");
    for (Cpu = 1; Cpu < 4; Cpu++) Print (L" %lx/%lx", S[Cpu * 2], S[Cpu * 2 + 1]);
    Print (L" tr %lx\n", S[16]);
    /* ntoskrnl is loaded physically contiguous: slot is at RVA 0xD18400.
     * RVA 0xD17DC0 = how long HAL waited (ms) for the last core to report in */
    Print (L"  hal wait %x\n", *(volatile UINT32 *)(UINTN)(D[1] - 0xD18400 + 0xD17DC0));
  }
  ZeroMem ((VOID *)D, 0x200);
  ZeroMem ((VOID *)((UINTN)D + 0x300), 0x100);
  D[0x40] = (D[0x40] < 1000) ? D[0x40] + 1 : 1;    /* boot counter: shows the page survives a reset */
  WriteBackInvalidateDataCacheRange ((VOID *)D, EFI_PAGE_SIZE);
}

STATIC VOID
CpuProbe (VOID)
{
  EFI_PHYSICAL_ADDRESS Page = 0xFFFFFFFF;
  volatile UINT32     *Box;
  ARM_SMC_ARGS         Args;
  UINTN                Cpu, Wait;

  {
    volatile UINT32 *En = (volatile UINT32 *)(UINTN)0x11001100;
    UINT32 Before = En[0], After;
    UINT64 Vct, Pct;
    En[0] = 0x4800FFFF;
    ArmDataSynchronizationBarrier ();
    After = En[0];
    ((volatile UINT32 *)(UINTN)0x11001180)[0] = 0x4800FFFF & ~Before;   /* restore */
    __asm__ volatile ("isb\n mrs %0, cntvct_el0\n mrs %1, cntpct_el0" : "=r"(Vct), "=r"(Pct));
    Print (L"  CPU0 MPIDR %lx EL%d en %08x->%08x vo %lx\n", ArmReadMpidr (), (UINT32)(ArmReadCurrentEL () >> 2),
           Before, After, Vct - Pct);
  }
  if (EFI_ERROR (gBS->AllocatePages (AllocateMaxAddress, EfiBootServicesCode, 1, &Page))) return;
  ZeroMem ((VOID *)(UINTN)Page, EFI_PAGE_SIZE);
  CopyMem ((VOID *)(UINTN)Page, mCpuProbeCode, sizeof (mCpuProbeCode));
  Box = (volatile UINT32 *)(UINTN)(Page + 0x800);
  WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Page, EFI_PAGE_SIZE);
  InvalidateInstructionCacheRange ((VOID *)(UINTN)Page, EFI_PAGE_SIZE);

  Args.Arg0 = 0x84000000;                    /* PSCI_VERSION */
  ArmCallSmc (&Args);
  Print (L"  PSCI_VERSION -> %lx  box=%lx\n", Args.Arg0, (UINT64)(UINTN)Box);

  for (Cpu = 1; Cpu < 4; Cpu++) {
    ZeroMem (&Args, sizeof (Args));
    Args.Arg0 = 0xC4000003;                  /* CPU_ON */
    Args.Arg1 = 0x100 | Cpu;
    Args.Arg2 = Page;
    Args.Arg3 = (UINTN)Box;
    ArmCallSmc (&Args);
    __builtin_arm_sev ();
    for (Wait = 0; Wait < 1000; Wait++) {
      InvalidateDataCacheRange ((VOID *)Box, 256);
      if (Box[Cpu * 8]) break;
      gBS->Stall (1000);
    }
    gBS->Stall (50000);                      /* let it run through all the steps */
    InvalidateDataCacheRange ((VOID *)Box, 256);
    Print (L"  CPU_ON 0x10%d -> %d, mark %08x (%d ms) en %08x->%08x vo %lx\n", Cpu, (INT32)Args.Arg0, Box[Cpu * 8], Wait,
           Box[Cpu * 8 + 6], Box[Cpu * 8 + 2], *(volatile UINT64 *)&Box[Cpu * 8 + 4]);
  }
}

/* set before handing control to the UEFI Shell, so the BOOTSEL.EFI that the
 * Shell's startup.nsh runs does not bring the menu straight back */
STATIC EFI_GUID mHero2VarGuid = { 0x6a1f2b3c, 0x8890, 0x4e2a, { 0x9b, 0x55, 0x48, 0x45, 0x52, 0x4f, 0x32, 0x4c } };
STATIC EFI_GUID mShellFileGuid = { 0x7C04A583, 0x9E3E, 0x4F1C, { 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 } };

STATIC VOID
ConnectAll (VOID)
{
  EFI_HANDLE *Handles;
  UINTN       Count, i, Pass;

  for (Pass = 0; Pass < 2; Pass++) {
    if (EFI_ERROR (gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &Count, &Handles))) return;
    for (i = 0; i < Count; i++) gBS->ConnectController (Handles[i], NULL, NULL, TRUE);
    FreePool (Handles);
  }
}

STATIC VOID
LaunchShell (EFI_HANDLE ImageHandle)
{
  EFI_LOADED_IMAGE_PROTOCOL           *Self;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH    Node;
  EFI_DEVICE_PATH_PROTOCOL            *Dp;
  EFI_HANDLE                           Image;
  UINT8                                One = 1;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Self))) return;
  EfiInitializeFwVolDevicepathNode (&Node, &mShellFileGuid);
  Dp = AppendDevicePathNode (DevicePathFromHandle (Self->DeviceHandle), (EFI_DEVICE_PATH_PROTOCOL *)&Node);
  if (Dp == NULL) return;
  gRT->SetVariable (L"Hero2InShell", &mHero2VarGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS, sizeof (One), &One);
  if (!EFI_ERROR (gBS->LoadImage (FALSE, ImageHandle, Dp, NULL, 0, &Image)))
    gBS->StartImage (Image, NULL, NULL);
  else
    Print (L"  UEFI Shell is not in this firmware volume (BOOTSEL.EFI started from disk?)\n");
}

#define WIN_LOADER   L"\\EFI\\Boot\\bootaa64.efi"
#define OS_LOADER    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define OS_MARKER    L"\\EFI\\Microsoft\\Boot\\BCD"
#define WIN_MARKER   L"\\sources\\boot.wim"
#define LNX_KERNEL   L"\\IMAGE.EFI"
#define LNX_MARKER   L"\\HERO2LTE.DTB"
#define LNX_CMDLINE  L"dtb=\\HERO2LTE.DTB initrd=\\ROOTFS.CPZ earlycon=efifb console=tty0 efi=noruntime " \
                     L"clk_ignore_unused pd_ignore_unused regulator_ignore_unused loglevel=4 printk.time=1"

STATIC BOOLEAN
FileExists (EFI_HANDLE Handle, CHAR16 *Path)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root, *File;

  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) return FALSE;
  if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) return FALSE;
  if (EFI_ERROR (Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0))) { Root->Close (Root); return FALSE; }
  File->Close (File);
  Root->Close (Root);
  return TRUE;
}

STATIC EFI_HANDLE
FindVolume (CHAR16 *Marker, CHAR16 *Loader)
{
  EFI_HANDLE  *Handles;
  UINTN       Count, i;
  EFI_HANDLE  Found = NULL;

  if (EFI_ERROR (gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles)))
    return NULL;
  for (i = 0; i < Count && !Found; i++)
    if (FileExists (Handles[i], Marker) && FileExists (Handles[i], Loader)) Found = Handles[i];
  FreePool (Handles);
  return Found;
}

STATIC EFI_STATUS
Launch (EFI_HANDLE ParentImage, EFI_HANDLE Volume, CHAR16 *Path, CHAR16 *Options)
{
  EFI_DEVICE_PATH_PROTOCOL   *Dp = FileDevicePath (Volume, Path);
  EFI_HANDLE                 Image;
  EFI_LOADED_IMAGE_PROTOCOL  *Li;
  EFI_STATUS                 Status;

  Status = gBS->LoadImage (FALSE, ParentImage, Dp, NULL, 0, &Image);
  if (EFI_ERROR (Status)) { Print (L"LoadImage %s: %r\n", Path, Status); return Status; }
  if (Options && !EFI_ERROR (gBS->HandleProtocol (Image, &gEfiLoadedImageProtocolGuid, (VOID **)&Li))) {
    Li->LoadOptions     = Options;
    Li->LoadOptionsSize = (UINT32)StrSize (Options);
  }
  Status = gBS->StartImage (Image, NULL, NULL);
  Print (L"%s returned %r\n", Path, Status);
  return Status;
}

EFI_STATUS
EFIAPI
BootSelMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE     Os, Win, Lnx;
  EFI_EVENT      Timer, Events[2];
  EFI_INPUT_KEY  Key;
  UINTN          Index, Left = 5;
  INTN           Choice;
  UINT8          InShell = 0;
  UINTN          VarSize = sizeof (InShell);

  if (!EFI_ERROR (gRT->GetVariable (L"Hero2InShell", &mHero2VarGuid, NULL, &VarSize, &InShell)) && InShell) {
    return EFI_SUCCESS;                      /* we are inside the Shell the menu started */
  }

  ConnectAll ();                             /* booted straight from BootOrder: nothing is connected yet */
  Os  = FindVolume (OS_MARKER, OS_LOADER);
  Win = FindVolume (WIN_MARKER, WIN_LOADER);
  Lnx = FindVolume (LNX_MARKER, LNX_KERNEL);
  Choice = Os ? 3 : (Win ? 1 : (Lnx ? 2 : 0));

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"\n  Galaxy S7 Edge (hero2lte) - boot menu\n\n");
  Print (L"  Volume Up   : Windows %s\n", Os ? L"" : (Win ? L"Setup (WinPE)" : L"(not found)"));
  Print (L"  Volume Down : Linux   %s\n", Lnx ? L"" : L"(not found)");
  Print (L"  Home        : WinPE   %s\n", Win ? L"" : L"(not found)");
  Print (L"  Power       : UEFI Shell\n\n");
  CpuProbe ();   /* SMP probe: non-secure SGIs/PPIs and virtual counter offset per core */
  SmpDiag ();
  SmpInstallTrampoline ();
  Print (L"\n");

  gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &Timer);
  gBS->SetTimer (Timer, TimerPeriodic, 10000000);  /* 1 s */
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = Timer;
  gST->ConIn->Reset (gST->ConIn, FALSE);

  while (Left > 0) {
    Print (L"\r  Default: %s in %d s ", Choice == 3 ? L"Windows" : Choice == 1 ? L"WinPE" : Choice == 2 ? L"Linux" : L"Shell", Left);
    gBS->WaitForEvent (2, Events, &Index);
    if (Index == 1) { Left--; continue; }
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) continue;
    if (Key.ScanCode == SCAN_UP && (Os || Win)) { Choice = Os ? 3 : 1; break; }
    if (Key.ScanCode == SCAN_DOWN && Lnx) { Choice = 2; break; }
    if (Key.ScanCode == SCAN_ESC && Win)  { Choice = 1; break; }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) { Choice = 0; break; }
  }
  gBS->CloseEvent (Timer);
  Print (L"\n\n");

  if (Choice == 3) {
    Print (L"  Starting Windows...\n");
    Launch (ImageHandle, Os, OS_LOADER, NULL);
  } else if (Choice == 1) {
    Print (L"  Starting WinPE...\n");
    Launch (ImageHandle, Win, WIN_LOADER, NULL);
  } else if (Choice == 2) {
    Print (L"  Starting Linux...\n");
    Launch (ImageHandle, Lnx, LNX_KERNEL, LNX_CMDLINE);
  }
  /* Power key, or the chosen OS returned: open the UEFI Shell */
  LaunchShell (ImageHandle);
  return EFI_SUCCESS;
}
