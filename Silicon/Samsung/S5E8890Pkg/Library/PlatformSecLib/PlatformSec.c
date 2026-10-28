/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>

#include "PlatformRegisters.h"

VOID
EnableFrameBufferWrites ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR DeconRegion;

  // Locate "Decon" Memory Region
  Status = LocateMemoryRegionByName ("Decon", &DeconRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // The S7 Edge panel runs in MIPI command mode, so the Decon only pushes a
  // frame when triggered. Switch to TE (HW) triggering, unmasked and without
  // auto-mask, so every TE pulse refreshes the panel from the frame buffer.
  // Verified on hardware with the hero2lte-diag TRIG_MODE=1 probe.
  //
  MmioAndThenOr32 (
    DeconRegion.Address + HW_SW_TRIG_CONTROL,
    ~(HW_SW_TRIG_CONTROL_HW_TRIG_MASK | HW_SW_TRIG_CONTROL_TRIG_AUTO_MASK_TRIG),
    HW_SW_TRIG_CONTROL_HW_TRIG_EN
    );

  // Latch Shadow Registers
  MmioOr32 (DeconRegion.Address + SHADOW_REG_UPDATE_REQ, SHADOW_REG_UPDATE_REQ_GLOBAL);
}

VOID
PlatformInitialize ()
{
  // Enable Frame Buffer Writes
  EnableFrameBufferWrites ();
}
