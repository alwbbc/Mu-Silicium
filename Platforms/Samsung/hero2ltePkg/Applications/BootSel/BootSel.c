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
/* secondary entry (MMU off): box = page+0x800 (PC relative), per CPU 32 bytes:
 * +0 0xC0DE0000|CurrentEL, +8/+16/+24 = x0/x1/x2 at entry; then PSCI CPU_OFF */
STATIC CONST UINT32 mCpuProbeCode[] = {
  0x10004009, 0xD53800AB, 0x92401D6B, 0x8B0B152A, 0xF9000540, 0xF9000941, 0xF9000D42, 0xD538424C,
  0x52B81BCD, 0x2A0C01AD, 0xB900014D, 0xD5033F9F, 0x52B08000, 0x72800040, 0xD4000003, 0xD503205F,
  0x17FFFFFF
};

STATIC VOID
CpuProbe (VOID)
{
  EFI_PHYSICAL_ADDRESS Page = 0xFFFFFFFF;
  volatile UINT32     *Box;
  ARM_SMC_ARGS         Args;
  UINTN                Cpu, Wait;

  Print (L"  CPU0 MPIDR %lx EL%d\n", ArmReadMpidr (), (UINT32)(ArmReadCurrentEL () >> 2));
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
    Print (L"  CPU_ON 0x10%d -> %d, mark %08x (%d ms) x0=%lx x1=%lx x2=%lx\n", Cpu, (INT32)Args.Arg0, Box[Cpu * 8], Wait,
           *(volatile UINT64 *)&Box[Cpu * 8 + 2], *(volatile UINT64 *)&Box[Cpu * 8 + 4], *(volatile UINT64 *)&Box[Cpu * 8 + 6]);
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
  CpuProbe ();
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
