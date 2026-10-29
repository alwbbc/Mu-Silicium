/** @file
  Exynos 8890 ResetSystemLib (runtime capable).

  Samsung's EL3 firmware on the 8890 only implements PSCI 0.1 (CPU_ON/OFF/
  SUSPEND), so PSCI SYSTEM_RESET/SYSTEM_OFF do not exist. Reset through the
  PMU instead, like Linux does for the Exynos7 family:
    reboot   : PMU + 0x0400 (SWRESET) = 1
    poweroff : PMU + 0x330C (PS_HOLD_CONTROL) bit 8 cleared

  The PMU window is registered as runtime MMIO and converted on
  SetVirtualAddressMap, so the OS (Windows) can call ResetSystem().

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Library/TimerLib.h>

#define PMU_BASE              0x105C0000UL
#define PMU_SIZE              0x00010000UL
#define PMU_SWRESET           0x0400
#define PMU_PS_HOLD_CONTROL   0x330C
#define PS_HOLD_HIGH          BIT8

STATIC UINTN      mPmuBase = PMU_BASE;
STATIC EFI_EVENT  mVirtualAddressChangeEvent;

VOID
EFIAPI
ResetCold (VOID)
{
  MmioWrite32 (mPmuBase + PMU_SWRESET, 1);
  for (;;) { CpuDeadLoop (); }
}

VOID
EFIAPI
ResetWarm (VOID)
{
  ResetCold ();
}

VOID
EFIAPI
ResetShutdown (VOID)
{
  MmioAnd32 (mPmuBase + PMU_PS_HOLD_CONTROL, ~(UINT32)PS_HOLD_HIGH);
  /* with a charger attached the PMIC keeps us alive: fall back to reboot */
  MicroSecondDelay (2000000);
  ResetCold ();
}

VOID
EFIAPI
ResetPlatformSpecific (IN UINTN DataSize, IN VOID *ResetData)
{
  ResetCold ();
}

VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID           *ResetData)
{
  switch (ResetType) {
    case EfiResetShutdown:
      ResetShutdown ();
      break;
    case EfiResetWarm:
    case EfiResetCold:
    case EfiResetPlatformSpecific:
    default:
      ResetCold ();
      break;
  }
}

STATIC
VOID
EFIAPI
VirtualAddressChange (IN EFI_EVENT Event, IN VOID *Context)
{
  EfiConvertPointer (0, (VOID **)&mPmuBase);
}

EFI_STATUS
EFIAPI
ResetSystemLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Desc;
  EFI_STATUS                      Status;

  Status = gDS->GetMemorySpaceDescriptor (PMU_BASE, &Desc);
  if (EFI_ERROR (Status) || Desc.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    gDS->AddMemorySpace (EfiGcdMemoryTypeMemoryMappedIo, PMU_BASE, PMU_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  } else {
    gDS->SetMemorySpaceCapabilities (PMU_BASE, PMU_SIZE, Desc.Capabilities | EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  }
  Status = gDS->SetMemorySpaceAttributes (PMU_BASE, PMU_SIZE, EFI_MEMORY_UC | EFI_MEMORY_RUNTIME);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: PMU runtime attribute: %r\n", __FUNCTION__, Status));
  }

  gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, VirtualAddressChange, NULL,
                      &gEfiEventVirtualAddressChangeGuid, &mVirtualAddressChangeEvent);
  return EFI_SUCCESS;
}
