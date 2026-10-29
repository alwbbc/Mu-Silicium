/** @file
  hero2lte boot selector: Volume Up = Windows, Volume Down = Linux,
  Power = UEFI Shell. Defaults to Windows (if present) after 5 seconds.

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

#define WIN_LOADER   L"\\EFI\\Boot\\bootaa64.efi"
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
  EFI_HANDLE     Win = FindVolume (WIN_MARKER, WIN_LOADER);
  EFI_HANDLE     Lnx = FindVolume (LNX_MARKER, LNX_KERNEL);
  EFI_EVENT      Timer, Events[2];
  EFI_INPUT_KEY  Key;
  UINTN          Index, Left = 5;
  INTN           Choice = Win ? 1 : (Lnx ? 2 : 0);

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"\n  Galaxy S7 Edge (hero2lte) - boot menu\n\n");
  Print (L"  Volume Up   : Windows %s\n", Win ? L"" : L"(not found)");
  Print (L"  Volume Down : Linux   %s\n", Lnx ? L"" : L"(not found)");
  Print (L"  Power       : UEFI Shell\n\n");

  gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &Timer);
  gBS->SetTimer (Timer, TimerPeriodic, 10000000);  /* 1 s */
  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = Timer;
  gST->ConIn->Reset (gST->ConIn, FALSE);

  while (Left > 0) {
    Print (L"\r  Default: %s in %d s ", Choice == 1 ? L"Windows" : Choice == 2 ? L"Linux" : L"Shell", Left);
    gBS->WaitForEvent (2, Events, &Index);
    if (Index == 1) { Left--; continue; }
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) continue;
    if (Key.ScanCode == SCAN_UP && Win)   { Choice = 1; break; }
    if (Key.ScanCode == SCAN_DOWN && Lnx) { Choice = 2; break; }
    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.ScanCode == SCAN_ESC) { Choice = 0; break; }
  }
  gBS->CloseEvent (Timer);
  Print (L"\n\n");

  if (Choice == 1) {
    Print (L"  Starting Windows...\n");
    Launch (ImageHandle, Win, WIN_LOADER, NULL);
  } else if (Choice == 2) {
    Print (L"  Starting Linux...\n");
    Launch (ImageHandle, Lnx, LNX_KERNEL, LNX_CMDLINE);
  }
  return EFI_SUCCESS;  /* back to the shell */
}
