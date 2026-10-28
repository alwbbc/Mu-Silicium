#ifndef _UFS_CAL_8890_H_
#define _UFS_CAL_8890_H_

#include <Library/UfsHostBridge.h>
#include <Library/UfsCalAdapterLib.h>

//
// Register Bases (hero2lte DT: ufs@0x155A0000)
//
#define EXYNOS8890_UFS_HCI_BASE        0x155A0000UL
#define EXYNOS8890_UFS_VS_BASE         0x155A0100UL
#define EXYNOS8890_UFS_UNIPRO_BASE     0x155A1000UL
#define EXYNOS8890_UFS_PMA_BASE        0x155A1800UL
#define EXYNOS8890_UFS_UFSP_BASE       0x155A2000UL
#define EXYNOS8890_UFS_PHY_ISO         0x105C0724UL   // PMU, ufs-phy-sys
#define EXYNOS8890_UFS_SYSREG          0x10E50700UL   // ufs-sys (IO coherency)

//
// Clocks
// PCLK must end up within the DT "pclk-freq-avail-range" (70 - 133 MHz).
//
#define UFS_MCLK_RATE                  166000000ULL   // sclk_ufsunipro (assumed; link + HS G3 verified on SM-G935F)
#define UFS_PCLK_RATE                  133000000ULL   // sclk_ufsunipro20_cfg (assumed, DT: use-seperated-pclk)
#define UFS_PCLK_AVAIL_MIN             70000000UL
#define UFS_PCLK_AVAIL_MAX             133000000UL

//
// HCI Vendor Specific Registers (offsets from VS base)
//
#define HCI_1US_TO_CNT_VAL             0x0C
#define  CNT_VAL_1US_MASK              0x3FF
#define HCI_TXPRDT_ENTRY_SIZE          0x00
#define HCI_RXPRDT_ENTRY_SIZE          0x04
#define HCI_AXIDMA_RWDATA_BURST_LEN    0x6C
#define HCI_ERROR_EN_DL_LAYER          0x7C
#define HCI_ERROR_EN_N_LAYER           0x80
#define HCI_ERROR_EN_T_LAYER           0x84
#define HCI_FORCE_HCS                  0xB4
#define  CLK_STOP_CTRL_EN_ALL          (BIT7 | BIT6 | BIT5)
#define  HCI_CORECLK_STOP_EN           BIT4
#define  DFES_ERR_EN                   BIT31
#define  DFES_DEF_DL_ERRS              (BIT5 | BIT13)
#define  DFES_DEF_N_ERRS               (BIT0 | BIT1 | BIT2)
#define  DFES_DEF_T_ERRS               (BIT0 | BIT1 | BIT2 | BIT4)

//
// UniPro APB Registers
//
#define UNIP_DME_PACP_CNFBIT              0x0C8
#define UNIP_DME_PWR_REQ_LOCALL2TIMER0    0x098
#define UNIP_DME_PWR_REQ_LOCALL2TIMER1    0x09C
#define UNIP_DME_PWR_REQ_LOCALL2TIMER2    0x0A0
#define UNIP_DME_PWR_REQ_REMOTEL2TIMER0   0x0A4
#define UNIP_DME_PWR_REQ_REMOTEL2TIMER1   0x0A8
#define UNIP_DME_PWR_REQ_REMOTEL2TIMER2   0x0AC

//
// MIB Attributes
//
#ifndef UIC_ARG_MIB_SEL
#define UIC_ARG_MIB_SEL(a, s)          ((((UINT32)(a)) << 16) | ((s) & 0xFFFF))
#endif
#ifndef UIC_ARG_MIB
#define UIC_ARG_MIB(a)                 UIC_ARG_MIB_SEL (a, 0)
#endif

#define TX_HIBERN8TIME_CAP             0x000F
#define TX_ADV_GRANULARITY_CAP         0x0010
#define TX_MIN_ACTIVATE_TIME           0x0033
#define RX_HS_G1_SYNC_LENGTH_CAP       0x008B
#define RX_HS_G1_PREP_LENGTH_CAP       0x008C
#define RX_MIN_ACTIVATETIME_CAP        0x008F
#define RX_HIBERN8TIME_CAP             0x0092
#define RX_HS_G2_SYNC_LENGTH_CAP       0x0094
#define RX_HS_G3_SYNC_LENGTH_CAP       0x0095
#define RX_HS_G2_PREP_LENGTH_CAP       0x0096
#define RX_HS_G3_PREP_LENGTH_CAP       0x0097
#define RX_ADV_GRANULARITY_CAP         0x0098
#define  SYNC_RANGE_COARSE             (1 << 6)
#define  SYNC_LEN(x)                   ((x) & 0x3F)
#define  PREP_LEN(x)                   ((x) & 0xF)

#define TX_LINERESET_P_VAL             0x027D
#define TX_OV_SLEEP_CNT_TIMER          0x028E
#define  TX_OV_H8_ENTER_EN             (1 << 7)
#define TX_HIGH_Z_CNT_11_08            0x028C
#define TX_HIGH_Z_CNT_07_00            0x028D
#define TX_BASE_NVAL_07_00             0x0293
#define TX_BASE_NVAL_15_08             0x0294
#define TX_GRAN_NVAL_07_00             0x0295
#define TX_GRAN_NVAL_10_08             0x0296
#define RX_FILLER_ENABLE               0x0316
#define RX_LINERESET_VAL               0x0317
#define RX_SYNC_MASK_LENGTH            0x0321
#define RX_OV_SLEEP_CNT_TIMER          0x0340
#define RX_OV_STALL_CNT_TIMER          0x0341
#define RX_GRAN_NVAL_10_08             0x0352
#define RX_GRAN_NVAL_07_00             0x0353
#define RX_BASE_NVAL_15_08             0x0354
#define RX_BASE_NVAL_07_00             0x0355
#define CMN_PWM_CMN_CTRL               0x0402

#define PA_AVAILRXDATALANES            0x1540
#define PA_TXTRAILINGCLOCKS            0x1564
#define PA_TACTIVATE                   0x15A8
#define PA_HIBERN8TIME                 0x15A7
#define PA_PWRMODEUSERDATA0            0x15B0
#define PA_PWRMODEUSERDATA1            0x15B1
#define PA_PWRMODEUSERDATA2            0x15B2
#define DL_FC0PROTTIMEOUTVAL           0x2041
#define DL_TC0REPLAYTIMEOUTVAL         0x2042
#define DL_AFC0REQTIMEOUTVAL           0x2043
#define N_DEVICEID                     0x3000
#define N_DEVICEID_VALID               0x3001
#define T_CONNECTIONSTATE              0x4020
#define T_PEERDEVICEID                 0x4021
#define T_PEERCPORTID                  0x4022
#define T_TRAFFICCLASS                 0x4023
#define T_CPORTFLAGS                   0x4025
#define  CPORT_DEF_FLAGS               ((1 << 2) | (1 << 1))
#define PA_DBG_CLK_PERIOD              0x9514
#define PA_DBG_OV_TM                   0x9540
#define T_DBG_SKIP_INIT_HIBERN8_EXIT   0xC001

//
// M-PHY Timing (ns), from the 8890 kernel
//
#define TX_DIF_P_NSEC                  3000000
#define TX_DIF_N_NSEC                  1000000
#define RX_DIF_P_NSEC                  1000000
#define RX_HIBERN8_WAIT_NSEC           4000000
#define RX_BASE_UNIT_NSEC              100000
#define RX_GRAN_UNIT_NSEC              4000
#define RX_SLEEP_CNT                   1280
#define RX_STALL_CNT                   320
#define TX_HIGH_Z_CNT_NSEC             20000
#define TX_BASE_UNIT_NSEC              100000
#define TX_GRAN_UNIT_NSEC              4000
#define TX_SLEEP_CNT                   1000
#define IATOVAL_NSEC                   20000

//
// DT Properties
//
#define UFS_RX_MIN_ACTV_TIME_CAP       3
#define UFS_RX_HIBERN8_TIME_CAP        2
#define UFS_TX_HIBERN8_TIME_CAP        2
#define UFS_WAIT_CDR_LOCK              0x6E

//
// PHY Table Layers & Power Mode Masks (8890 kernel numbering)
//
enum {
  PHY_CFG_NONE = 0,
  PHY_PCS_COMN,
  PHY_PCS_RXTX,
  PHY_PMA_COMN,
  PHY_PMA_TRSV,
  UNIPRO_STD_MIB,
  UNIPRO_DBG_MIB,
  UNIPRO_DBG_APB,
};

#define PMD_PWM_BIT(g, l)              (1U << (((g) - 1) * 2 + ((l) - 1)))
#define PMD_HS_BIT(g, l)               (1U << (10 + ((g) - 1) * 2 + ((l) - 1)))

#define PHY_PMA_COMN_ADDR(r)           ((r) << 2)
#define PHY_PMA_TRSV_ADDR(r, l)        (((r) + (0x30 * (l))) << 2)

typedef struct {
  UINT32 Address;
  UINT32 Value;
  UINT32 Flag;
  UINT32 Layer;
} UFS_PHY_CFG;

enum {
  FAST_MODE     = 1,
  SLOW_MODE     = 2,
  FASTAUTO_MODE = 4,
  SLOWAUTO_MODE = 5,
};

#define IS_PWR_MODE_HS(m)              (((m) == FAST_MODE) || ((m) == FASTAUTO_MODE))
#define IS_PWR_MODE_PWM(m)             (((m) == SLOW_MODE) || ((m) == SLOWAUTO_MODE))

#endif /* _UFS_CAL_8890_H_ */
