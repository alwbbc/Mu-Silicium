#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/PlatformBootOptionsLib.h>

//
// ShellPkg/Application/Shell/Shell.inf FILE_GUID (gUefiShellFileGuid)
//
STATIC EFI_GUID mUefiShellFileGuid = {
  0x7C04A583, 0x9E3E, 0x4F1C, { 0xAD, 0x65, 0xE0, 0x52, 0x68, 0xD0, 0xB4, 0xD1 }
};

VOID
GetPlatformBootOptions (
  OUT EFI_PLATFORM_BOOT_OPTION **BootOption,
  OUT UINT8                     *BootOptionCount)
{
  EFI_PLATFORM_BOOT_OPTION *LocalBootOption;

  *BootOption      = NULL;
  *BootOptionCount = 0;

  LocalBootOption = AllocateZeroPool (sizeof (EFI_PLATFORM_BOOT_OPTION));
  if (LocalBootOption == NULL) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Allocate Memory for Boot Options!\n", __FUNCTION__));
    return;
  }

  // UEFI Shell, tried after Internal Storage / USB until an OS is installed
  LocalBootOption[0].AppName       = L"UEFI Shell";
  LocalBootOption[0].AppGuid       = &mUefiShellFileGuid;
  LocalBootOption[0].LoadAttribute = LOAD_OPTION_ACTIVE;

  *BootOption      = LocalBootOption;
  *BootOptionCount = 1;
}
