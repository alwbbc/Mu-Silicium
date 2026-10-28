/**
  Exynos 8890 UFS calibration.

  Re-implements the link bring-up done by the Galaxy S7 kernel
  (drivers/scsi/ufs/ufs-exynos.c, universal8890) on top of the generic
  SamsungPkg UfsDxe: pre-link PHY init, post-link connection setup and the
  power mode change calibration. The PHY tables come from the device's DT.

  SPDX-License-Identifier: GPL-2.0-or-later
**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/PlatformUfsLib.h>

#include "UfsCal.h"
#include "UfsPhyTables.h"

STATIC UINT64 mPclkRate;
STATIC UINT64 mMclkRate;
STATIC UINT8  mLanes = 1;

//
// Helpers
//
STATIC
struct UfsHost *
HostOf (struct UfsCalParam *p)
{
  return (struct UfsHost *)p->Host;
}

STATIC
VOID
DmeSet (struct UfsCalParam *p, UINT32 Attr, UINT32 Value)
{
  ufs_lld_dme_set (p->Host, Attr, Value);
}

STATIC
UINT32
DmeGet (struct UfsCalParam *p, UINT32 Attr)
{
  UINT32 Value = 0;

  ufs_lld_dme_get (p->Host, Attr, &Value);

  return Value;
}

STATIC
VOID
HciWrite (struct UfsCalParam *p, UINT32 Offset, UINT32 Value)
{
  MmioWrite32 ((UINTN)HostOf (p)->VsAddr + Offset, Value);
}

STATIC
UINT32
HciRead (struct UfsCalParam *p, UINT32 Offset)
{
  return MmioRead32 ((UINTN)HostOf (p)->VsAddr + Offset);
}

STATIC
UINT64
CalcTimeCntr (UINT64 PeriodNs)
{
  CONST UINT64 Precise = 10;
  UINT64       ClkPeriod;
  UINT64       Fraction;

  ClkPeriod = 1000000000ULL / mPclkRate;
  Fraction  = ((1000000000ULL % mPclkRate) * Precise) / mPclkRate;

  return (PeriodNs * Precise) / ((ClkPeriod * Precise) + Fraction);
}

STATIC
BOOLEAN
MatchMode (struct UicPwrMode *Pmd, UINT32 Flag)
{
  if (Pmd == NULL) {
    return TRUE;
  }

  if (IS_PWR_MODE_HS (Pmd->Mode)) {
    return (Flag & PMD_HS_BIT (Pmd->Gear, Pmd->Lane)) != 0;
  }

  if (IS_PWR_MODE_PWM (Pmd->Mode)) {
    return (Flag & PMD_PWM_BIT (Pmd->Gear, Pmd->Lane)) != 0;
  }

  return FALSE;
}

STATIC
VOID
ConfigPhy (
  struct UfsCalParam *p,
  CONST UFS_PHY_CFG  *Cfg,
  struct UicPwrMode  *Pmd
  )
{
  for (; Cfg->Flag != PHY_CFG_NONE; Cfg++) {
    for (UINT8 Lane = 0; Lane < mLanes; Lane++) {
      if (!MatchMode (Pmd, Cfg->Flag)) {
        continue;
      }

      switch (Cfg->Layer) {
        case PHY_PCS_COMN:
        case UNIPRO_STD_MIB:
        case UNIPRO_DBG_MIB:
          if (Lane == 0) {
            DmeSet (p, UIC_ARG_MIB (Cfg->Address), Cfg->Value);
          }
          break;

        case PHY_PCS_RXTX:
          DmeSet (p, UIC_ARG_MIB_SEL (Cfg->Address, Lane), Cfg->Value);
          break;

        case PHY_PMA_COMN:
          if (Lane == 0) {
            ufs_lld_pma_write (p->Host, Cfg->Value, PHY_PMA_COMN_ADDR (Cfg->Address));
          }
          break;

        case PHY_PMA_TRSV:
          ufs_lld_pma_write (p->Host, Cfg->Value, PHY_PMA_TRSV_ADDR (Cfg->Address, Lane));
          break;

        case UNIPRO_DBG_APB:
          ufs_lld_unipro_write (p->Host, Cfg->Value, Cfg->Address);
          break;
      }
    }
  }
}

//
// Pre-Link Steps
//
STATIC
VOID
SetPwmClkDiv (struct UfsCalParam *p)
{
  CONST UINT64 Div      = 30;
  CONST UINT64 Mult     = 20;
  CONST UINT64 PwmMin   = 3000000;
  CONST UINT64 PwmMax   = 9000000;
  CONST UINT64 Divs[]   = { 32, 16, 8, 4 };
  UINT64       Period   = 1000000000ULL / mPclkRate;
  UINT64       Best     = 0;
  INTN         BestIdx  = -1;

  for (UINTN i = 0; i < ARRAY_SIZE (Divs); i++) {
    UINT64 Clk = 1000000000ULL * Mult / (Period * Divs[i] * Div);

    if (Clk >= PwmMin && Clk <= PwmMax && Clk > Best) {
      BestIdx = (INTN)i;
      Best    = Clk;
    }
  }

  if (BestIdx >= 0) {
    DmeSet (p, UIC_ARG_MIB (CMN_PWM_CMN_CTRL), (UINT32)BestIdx & 0x3);
  } else {
    DEBUG ((EFI_D_ERROR, "UFS: No PWM Clock Divider for PCLK %lu Hz\n", mPclkRate));
  }
}

STATIC
VOID
ConfigPhyTime (struct UfsCalParam *p)
{
  UINT64 TxLineResetP = CalcTimeCntr (TX_DIF_P_NSEC);
  UINT64 TxHighZ      = CalcTimeCntr (TX_HIGH_Z_CNT_NSEC);
  UINT64 TxBase       = CalcTimeCntr (TX_BASE_UNIT_NSEC);
  UINT64 TxGran       = CalcTimeCntr (TX_GRAN_UNIT_NSEC);
  UINT64 TxSleep      = CalcTimeCntr (TX_SLEEP_CNT);
  UINT64 RxLineReset  = CalcTimeCntr (RX_DIF_P_NSEC);
  UINT64 RxBase       = CalcTimeCntr (RX_BASE_UNIT_NSEC);
  UINT64 RxGran       = CalcTimeCntr (RX_GRAN_UNIT_NSEC);
  UINT64 RxSleep      = CalcTimeCntr (RX_SLEEP_CNT);
  UINT64 RxStall      = CalcTimeCntr (RX_STALL_CNT);

  SetPwmClkDiv (p);

  DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 1);

  for (UINT8 i = 0; i < mLanes; i++) {
    DmeSet (p, UIC_ARG_MIB_SEL (RX_FILLER_ENABLE, i),      0x2);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_LINERESET_VAL, i),      (RxLineReset >> 12) & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_BASE_NVAL_07_00, i),    RxBase & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_BASE_NVAL_15_08, i),    (RxBase >> 8) & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_GRAN_NVAL_07_00, i),    RxGran & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_GRAN_NVAL_10_08, i),    (RxGran >> 8) & 0x3);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_OV_SLEEP_CNT_TIMER, i), (RxSleep >> 6) & 0x1F);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_OV_STALL_CNT_TIMER, i), (RxStall >> 4) & 0xFF);
  }

  for (UINT8 i = 0; i < mLanes; i++) {
    DmeSet (p, UIC_ARG_MIB_SEL (TX_LINERESET_P_VAL, i),    (TxLineResetP >> 12) & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_HIGH_Z_CNT_07_00, i),   TxHighZ & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_HIGH_Z_CNT_11_08, i),   (TxHighZ >> 8) & 0xF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_BASE_NVAL_07_00, i),    TxBase & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_BASE_NVAL_15_08, i),    (TxBase >> 8) & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_GRAN_NVAL_07_00, i),    TxGran & 0xFF);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_GRAN_NVAL_10_08, i),    (TxGran >> 8) & 0x3);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_OV_SLEEP_CNT_TIMER, i), TX_OV_H8_ENTER_EN | ((TxSleep >> 5) & 0x7F));
    DmeSet (p, UIC_ARG_MIB_SEL (TX_MIN_ACTIVATE_TIME, i),  0xA);
  }

  DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 0);
}

STATIC
VOID
ConfigPhyCapAttr (struct UfsCalParam *p)
{
  DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 1);

  for (UINT8 i = 0; i < mLanes; i++) {
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G1_SYNC_LENGTH_CAP, i), SYNC_RANGE_COARSE | SYNC_LEN (0xF));
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G2_SYNC_LENGTH_CAP, i), SYNC_RANGE_COARSE | SYNC_LEN (0xF));
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G3_SYNC_LENGTH_CAP, i), SYNC_RANGE_COARSE | SYNC_LEN (0xF));
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G1_PREP_LENGTH_CAP, i), PREP_LEN (0xF));
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G2_PREP_LENGTH_CAP, i), PREP_LEN (0xF));
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HS_G3_PREP_LENGTH_CAP, i), PREP_LEN (0xF));
  }

  // DT: ufs-rx-adv-fine-gran-sup_en = 0
  for (UINT8 i = 0; i < mLanes; i++) {
    DmeSet (p, UIC_ARG_MIB_SEL (RX_ADV_GRANULARITY_CAP, i),  0);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_ADV_GRANULARITY_CAP, i),  0);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_MIN_ACTIVATETIME_CAP, i), UFS_RX_MIN_ACTV_TIME_CAP);
    DmeSet (p, UIC_ARG_MIB_SEL (RX_HIBERN8TIME_CAP, i),      UFS_RX_HIBERN8_TIME_CAP);
    DmeSet (p, UIC_ARG_MIB_SEL (TX_HIBERN8TIME_CAP, i),      UFS_TX_HIBERN8_TIME_CAP);
  }

  DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 0);
}

//
// Public CAL Interface
//
UINT8
UfsCalGetTargetBoard (VOID)
{
  return 0;
}

UfsCalError
UfsCalInit (struct UfsCalParam *p)
{
  UINT64 Pclk = UFS_PCLK_RATE;
  UINT8  Div  = 0;

  // Same (cumulative) divider search as exynos_ufs_set_unipro_pclk()
  do {
    Pclk /= (Div + 1);

    if (Pclk <= UFS_PCLK_AVAIL_MAX) {
      break;
    }

    Div++;
  } while (Pclk >= UFS_PCLK_AVAIL_MIN);

  mPclkRate = Pclk;
  mMclkRate = UFS_MCLK_RATE;

  DEBUG ((EFI_D_WARN, "UFS 8890: PCLK %lu Hz, MCLK %lu Hz\n", mPclkRate, mMclkRate));

  if (mPclkRate < UFS_PCLK_AVAIL_MIN) {
    DEBUG ((EFI_D_ERROR, "UFS 8890: PCLK %lu Hz Below Available Range!\n", mPclkRate));
    return UFS_CAL_INV_ARG;
  }

  return UFS_CAL_NO_ERROR;
}

UfsCalError
UfsCalPreLink (struct UfsCalParam *p)
{
  UINT32 Reg;

  mLanes = p->AvailableLane ? p->AvailableLane : 1;
  DEBUG ((EFI_D_WARN, "UFS 8890: Pre-Link, %u Lane(s)\n", mLanes));

  // Kernel exynos_ufs_init(): HCI core clock stop off
  Reg = HciRead (p, HCI_FORCE_HCS);
  Reg &= ~(HCI_CORECLK_STOP_EN | BIT8 | BIT10 | BIT11);
  HciWrite (p, HCI_FORCE_HCS, Reg);

  // Error Reporting
  HciWrite (p, HCI_ERROR_EN_DL_LAYER, DFES_ERR_EN | DFES_DEF_DL_ERRS);
  HciWrite (p, HCI_ERROR_EN_N_LAYER,  DFES_ERR_EN | DFES_DEF_N_ERRS);
  HciWrite (p, HCI_ERROR_EN_T_LAYER,  DFES_ERR_EN | DFES_DEF_T_ERRS);

  // exynos_ufs_ctrl_clk (true)
  HciWrite (p, HCI_FORCE_HCS, HciRead (p, HCI_FORCE_HCS) | CLK_STOP_CTRL_EN_ALL);

  // M-PHY Init
  ConfigPhy (p, mPhyInit, NULL);

  // UniPro
  ufs_lld_unipro_write (p->Host, 0x1, UNIP_DME_PACP_CNFBIT);
  DmeSet (p, UIC_ARG_MIB (PA_DBG_CLK_PERIOD), (UINT32)((1000000000ULL + mMclkRate - 1) / mMclkRate));
  DmeSet (p, UIC_ARG_MIB (PA_TXTRAILINGCLOCKS), 0x10);

  // M-PHY Timing & Capabilities
  ConfigPhyTime (p);
  ConfigPhyCapAttr (p);

  return UFS_CAL_NO_ERROR;
}

UfsCalError
UfsCalPostLink (struct UfsCalParam *p)
{
  UINT32 PeerTActivate;
  UINT32 Hibern8Time;

  DEBUG ((EFI_D_WARN, "UFS 8890: Post-Link\n"));

  // Establish Connection
  DmeSet (p, UIC_ARG_MIB (T_CONNECTIONSTATE), 0);
  DmeSet (p, UIC_ARG_MIB (N_DEVICEID),        0);
  DmeSet (p, UIC_ARG_MIB (N_DEVICEID_VALID),  1);
  DmeSet (p, UIC_ARG_MIB (T_PEERDEVICEID),    1);
  DmeSet (p, UIC_ARG_MIB (T_PEERCPORTID),     0);
  DmeSet (p, UIC_ARG_MIB (T_CPORTFLAGS),      CPORT_DEF_FLAGS);
  DmeSet (p, UIC_ARG_MIB (T_TRAFFICCLASS),    0);
  DmeSet (p, UIC_ARG_MIB (T_CONNECTIONSTATE), 1);

  // Aggregation Timeout
  HciWrite (p, HCI_1US_TO_CNT_VAL, (UINT32)CalcTimeCntr (IATOVAL_NSEC / 40) & CNT_VAL_1US_MASK);
  HciWrite (p, HCI_AXIDMA_RWDATA_BURST_LEN, 0xF);

  // DT: ufs-opts-skip-connection-estab
  DmeSet (p, UIC_ARG_MIB (T_DBG_SKIP_INIT_HIBERN8_EXIT), 1);

  PeerTActivate = DmeGet (p, UIC_ARG_MIB (PA_TACTIVATE));
  Hibern8Time   = DmeGet (p, UIC_ARG_MIB (PA_HIBERN8TIME));

  if (UFS_RX_MIN_ACTV_TIME_CAP <= PeerTActivate) {
    ufs_lld_dme_peer_set (p->Host, UIC_ARG_MIB (PA_TACTIVATE), PeerTActivate + 1);
  }

  DmeSet (p, UIC_ARG_MIB (PA_HIBERN8TIME), Hibern8Time + 1);

  ConfigPhy (p, mPostPhyInit, NULL);

  return UFS_CAL_NO_ERROR;
}

UfsCalError
UfsCalPrePmc (struct UfsCalParam *p)
{
  struct UicPwrMode *Pmd = p->Pmd;
  UINT32             Mask;
  UINT64             SyncLen;

  DEBUG ((EFI_D_WARN, "UFS 8890: Pre-PMC M%u G%u L%u S%u\n", Pmd->Mode, Pmd->Gear, Pmd->Lane, Pmd->HsSeries));

  // L2 Timers (DT ufs,pmd-local-l2-timer / ufs,pmd-remote-l2-timer)
  DmeSet (p, UIC_ARG_MIB (DL_FC0PROTTIMEOUTVAL),   0x1F40);
  DmeSet (p, UIC_ARG_MIB (DL_TC0REPLAYTIMEOUTVAL), 0x6D60);
  DmeSet (p, UIC_ARG_MIB (DL_AFC0REQTIMEOUTVAL),   0x4E20);
  DmeSet (p, UIC_ARG_MIB (PA_PWRMODEUSERDATA0),    0x2EE0);
  DmeSet (p, UIC_ARG_MIB (PA_PWRMODEUSERDATA1),    0x7D00);
  DmeSet (p, UIC_ARG_MIB (PA_PWRMODEUSERDATA2),    0x3E80);

  ufs_lld_unipro_write (p->Host, 0x1F40, UNIP_DME_PWR_REQ_LOCALL2TIMER0);
  ufs_lld_unipro_write (p->Host, 0x6D60, UNIP_DME_PWR_REQ_LOCALL2TIMER1);
  ufs_lld_unipro_write (p->Host, 0x4E20, UNIP_DME_PWR_REQ_LOCALL2TIMER2);
  ufs_lld_unipro_write (p->Host, 0x2EE0, UNIP_DME_PWR_REQ_REMOTEL2TIMER0);
  ufs_lld_unipro_write (p->Host, 0x7D00, UNIP_DME_PWR_REQ_REMOTEL2TIMER1);
  ufs_lld_unipro_write (p->Host, 0x3E80, UNIP_DME_PWR_REQ_REMOTEL2TIMER2);

  if (IS_PWR_MODE_HS (Pmd->Mode)) {
    // Sync Pattern Mask
    SyncLen = (Pmd->Gear == 1) ? 80000 : (Pmd->Gear == 2) ? 40000 : 20000;
    Mask    = (UINT32)((CalcTimeCntr (SyncLen) >> 8) & 0xFF);

    DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 1);
    for (UINT8 i = 0; i < mLanes; i++) {
      DmeSet (p, UIC_ARG_MIB_SEL (RX_SYNC_MASK_LENGTH, i), Mask);
    }
    DmeSet (p, UIC_ARG_MIB (PA_DBG_OV_TM), 0);

    ConfigPhy (p, (Pmd->HsSeries == 1) ? mCalibOfHsRateA : mCalibOfHsRateB, Pmd);
  } else if (IS_PWR_MODE_PWM (Pmd->Mode)) {
    ConfigPhy (p, mCalibOfPwm, Pmd);
  }

  return UFS_CAL_NO_ERROR;
}

STATIC
BOOLEAN
WaitBit (struct UfsCalParam *p, UINT32 PmaOffset, UINT32 Bit, CONST CHAR8 *Name)
{
  for (UINTN i = 0; i < 100000; i++) {
    if (ufs_lld_pma_read (p->Host, PmaOffset) & Bit) {
      return TRUE;
    }

    MicroSecondDelay (1);
  }

  DEBUG ((EFI_D_ERROR, "UFS 8890: Timeout Waiting for %a Lock!\n", Name));
  return FALSE;
}

UfsCalError
UfsCalPostPmc (struct UfsCalParam *p)
{
  struct UicPwrMode *Pmd = p->Pmd;

  DEBUG ((EFI_D_WARN, "UFS 8890: Post-PMC\n"));

  if (IS_PWR_MODE_HS (Pmd->Mode)) {
    ConfigPhy (p, (Pmd->HsSeries == 1) ? mPostCalibOfHsRateA : mPostCalibOfHsRateB, Pmd);

    if (!WaitBit (p, PHY_PMA_COMN_ADDR (0x1E), BIT5, "PLL") ||
        !WaitBit (p, PHY_PMA_TRSV_ADDR (UFS_WAIT_CDR_LOCK, 0), BIT4, "CDR")) {
      return UFS_CAL_TIMEOUT;
    }
  } else if (IS_PWR_MODE_PWM (Pmd->Mode)) {
    ConfigPhy (p, mPostCalibOfPwm, Pmd);
  }

  return UFS_CAL_NO_ERROR;
}
