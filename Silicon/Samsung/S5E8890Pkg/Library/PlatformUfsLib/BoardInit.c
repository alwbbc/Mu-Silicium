/**
  Exynos 8890 UFS board init (Galaxy S7 / S7 Edge).

  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/PlatformUfsLib.h>

#include "UfsCal.h"

//
// Secure Monitor Calls used by the 8890 kernel (include/linux/smc.h)
//
#define SMC_CMD_FMP                    0xC2001810
#define SMC_CMD_SMU                    0xC2001820
#define FMP_SECURITY                   0x8
#define FMP_DESC_OFF                   0x0
#define FMP_SMU_INIT                   0x0
#define FMP_SMU_OFF                    0x0

//
// UFS VCC/VCCQ are PMIC rails left on by S-Boot, there is no GPIO to toggle.
// UfsDxe still wants a register for device power, so give it a dummy one.
//
STATIC UINT32 mDummyDevPwr = 1;

STATIC
VOID
ConfigSmu (VOID)
{
  ARM_SMC_ARGS Args;

  // exynos_ufs_config_smu(): FMP descriptors off, SMU off
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_FMP;
  Args.Arg1 = FMP_SECURITY;
  Args.Arg2 = EXYNOS8890_UFS_UFSP_BASE;
  Args.Arg3 = FMP_DESC_OFF;
  ArmCallSmc (&Args);
  DEBUG ((EFI_D_WARN, "UFS 8890: FMP Security SMC = 0x%lx\n", Args.Arg0));

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = SMC_CMD_SMU;
  Args.Arg1 = FMP_SMU_INIT;
  Args.Arg2 = FMP_SMU_OFF;
  Args.Arg3 = 0;
  ArmCallSmc (&Args);
  DEBUG ((EFI_D_WARN, "UFS 8890: SMU Init SMC = 0x%lx\n", Args.Arg0));
}

EFI_STATUS
UfsBoardInit (struct UfsHost *Ufs)
{
  DEBUG ((EFI_D_WARN, "UFS 8890: Board Init\n"));

  Ufs->IoAddr      = (VOID *)EXYNOS8890_UFS_HCI_BASE;
  Ufs->VsAddr      = (VOID *)EXYNOS8890_UFS_VS_BASE;
  Ufs->UniProAddr  = (VOID *)EXYNOS8890_UFS_UNIPRO_BASE;
  Ufs->UfsPaddr    = (VOID *)EXYNOS8890_UFS_UFSP_BASE;
  Ufs->PhyPma      = (VOID *)EXYNOS8890_UFS_PMA_BASE;
  Ufs->PhyIsoAddr  = (VOID *)EXYNOS8890_UFS_PHY_ISO;
  Ufs->DevPwrAddr  = (VOID *)&mDummyDevPwr;
  Ufs->DevPwrShift = 0;

  Ufs->MclkRate    = UFS_MCLK_RATE;

  // DT: ufs,pmd-attr-gear = 3, HS rate B, 1 lane
  Ufs->GearMode    = 3;

  // IO Coherency (exynos_ufs_enable_io_coherency)
  MmioOr32 (EXYNOS8890_UFS_SYSREG, BIT16 | BIT17);

  ConfigSmu ();

  return EFI_SUCCESS;
}
