/*
 * hero2touch - Galaxy S7 Edge (hero2lte, Exynos 8890) touchscreen for Windows.
 *
 * Self-contained KMDF lower filter under mshidkmdf:
 *   - opens clock gates / pinmux for HSI2C7 (touch) and HSI2C15 (PMIC)
 *   - turns on S2MPS16 LDO32 (1.85V vdd) and LDO33 (3.3V avdd)
 *   - initialises the S6SA552 controller at 0x48 on HSI2C7
 *   - polls the touch IRQ line (gpa1-0, active low) and reports the first
 *     fingers as a 5-contact HID touch screen; keys as HID keyboard/consumer.
 *
 * The register layout follows Linux i2c-exynos5.c, pinctrl-exynos and the
 * s6sa552 driver by Ivaylo Ivanov (GPL-2.0), so this file is GPL-2.0 too.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>

#define DBG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "hero2touch: " fmt "\n", __VA_ARGS__)

/* ---- physical addresses ------------------------------------------------ */
#define CMU_PERIC1_BASE   0x14C80000ULL  /* CG_CTRL_VAL_ACLK_PERIC1_66_HSI2C 0x804 bit3 */
#define CMU_CCORE_BASE    0x105B0000ULL  /* CG_CTRL_VAL_PCLK_CCORE_AP 0x900 bit0 */
#define PINCTRL_ALIVE     0x10580000ULL  /* gpa1 @ +0x20 */
#define PINCTRL_CCORE     0x105A0000ULL  /* etc0 @ +0x00 (HSI2C15) */
#define PINCTRL_TOUCH     0x14CE0000ULL  /* gpf1 @ +0x00 (HSI2C7) */
#define HSI2C7_BASE       0x14E10000ULL
#define HSI2C15_BASE      0x10550000ULL

/* ---- HSI2C registers --------------------------------------------------- */
#define HSI2C_CTL          0x00
#define HSI2C_FIFO_CTL     0x04
#define HSI2C_TRAILIG_CTL  0x08
#define HSI2C_INT_ENABLE   0x20
#define HSI2C_INT_STATUS   0x24
#define HSI2C_FIFO_STATUS  0x30
#define HSI2C_TX_DATA      0x34
#define HSI2C_RX_DATA      0x38
#define HSI2C_CONF         0x40
#define HSI2C_AUTO_CONF    0x44
#define HSI2C_TIMEOUT      0x48
#define HSI2C_TRANS_STATUS 0x50
#define HSI2C_TIMING_FS1   0x60
#define HSI2C_TIMING_FS2   0x64
#define HSI2C_TIMING_FS3   0x68
#define HSI2C_TIMING_SLA   0x6C
#define HSI2C_ADDR         0x70

#define CTL_FUNC_MODE_I2C  (1u << 0)
#define CTL_MASTER         (1u << 3)
#define CTL_RXCHON         (1u << 6)
#define CTL_TXCHON         (1u << 7)
#define CTL_SW_RST         (1u << 31)
#define FIFO_RX_EN         (1u << 0)
#define FIFO_TX_EN         (1u << 1)
#define INT_TRANS_DONE     (1u << 7)
#define INT_TRANS_ABORT    (1u << 8)
#define INT_NO_DEV_ACK     (1u << 9)
#define INT_NO_DEV         (1u << 10)
#define INT_TIMEOUT        (1u << 11)
#define INT_ERRS           (INT_TRANS_ABORT | INT_NO_DEV_ACK | INT_NO_DEV | INT_TIMEOUT)
#define FIFO_RX_LVL(x)     (((x) >> 16) & 0x7f)
#define CONF_AUTO_MODE     (1u << 31)
#define AUTO_READ          (1u << 16)
#define AUTO_STOP          (1u << 17)
#define AUTO_RUN           (1u << 31)
#define TIMEOUT_EN         (1u << 31)
#define TRANS_MASTER_BUSY  (1u << 17)

/* ---- devices ----------------------------------------------------------- */
#define PMIC_ADDR    0x66
#define PMIC_L32CTRL 0x5C   /* 0.7V + 25mV*n, 1.85V -> 46 */
#define PMIC_L33CTRL 0x5D   /* 1.8V + 25mV*n, 3.3V  -> 60 */
#define TS_ADDR      0x48
#define TS_SENSE_ON        0x40
#define TS_TOUCH_FUNCTION  0x63
#define TS_DEVICE_ID       0x52
#define TS_BOOT_STATUS     0x55
#define TS_READ_ONE_EVENT  0x71
#define TS_CLEAR_EVENTS    0x60
#define TS_MAX             4095

/* ---- HID ---------------------------------------------------------------- */
#define MAX_CONTACTS 5
static const UCHAR ReportDescriptor[] = {
    /* report 1: multi-touch screen, 5 contacts (parallel mode) */
    0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x0F, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,
    0x09, 0x30, 0x46, 0xAD, 0x02, 0x81, 0x02,      /* X, 68.5 mm */
    0x09, 0x31, 0x46, 0xC2, 0x04, 0x81, 0x02,      /* Y, 121.8 mm */
    0x55, 0x00, 0x65, 0x00, 0x45, 0x00,
    0xC0,
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x0F, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,
    0x09, 0x30, 0x46, 0xAD, 0x02, 0x81, 0x02,      /* X, 68.5 mm */
    0x09, 0x31, 0x46, 0xC2, 0x04, 0x81, 0x02,      /* Y, 121.8 mm */
    0x55, 0x00, 0x65, 0x00, 0x45, 0x00,
    0xC0,
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x0F, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,
    0x09, 0x30, 0x46, 0xAD, 0x02, 0x81, 0x02,      /* X, 68.5 mm */
    0x09, 0x31, 0x46, 0xC2, 0x04, 0x81, 0x02,      /* Y, 121.8 mm */
    0x55, 0x00, 0x65, 0x00, 0x45, 0x00,
    0xC0,
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x0F, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,
    0x09, 0x30, 0x46, 0xAD, 0x02, 0x81, 0x02,      /* X, 68.5 mm */
    0x09, 0x31, 0x46, 0xC2, 0x04, 0x81, 0x02,      /* Y, 121.8 mm */
    0x55, 0x00, 0x65, 0x00, 0x45, 0x00,
    0xC0,
    0x05, 0x0D, 0x09, 0x22, 0xA1, 0x02,
    0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x81, 0x03,
    0x09, 0x51, 0x25, 0x0F, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x55, 0x0E, 0x65, 0x11,
    0x09, 0x30, 0x46, 0xAD, 0x02, 0x81, 0x02,      /* X, 68.5 mm */
    0x09, 0x31, 0x46, 0xC2, 0x04, 0x81, 0x02,      /* Y, 121.8 mm */
    0x55, 0x00, 0x65, 0x00, 0x45, 0x00,
    0xC0,
    0x05, 0x0D, 0x09, 0x54, 0x15, 0x00, 0x25, MAX_CONTACTS, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02,
    /* feature 4: contact count maximum */
    0x85, 0x04, 0x09, 0x55, 0x25, MAX_CONTACTS, 0xB1, 0x02,
    0xC0,
    /* report 2: keyboard (modifiers + 6 keys); Home = Left GUI, Power = Win+L */
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0xC0,
    /* report 3: consumer control, volume up / down */
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03,
    0x09, 0xE9, 0x09, 0xEA, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0xC0,
    /* report 6: absolute mouse, used until Windows starts the touch collection */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x06, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x01, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
    0x75, 0x07, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x0F,
    0x35, 0x00, 0x46, 0xFF, 0x0F,
    0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0x45, 0x00,
    0xC0, 0xC0,
    /* feature 5: device configuration (input mode / device index) */
    0x05, 0x0D, 0x09, 0x0E, 0xA1, 0x01, 0x85, 0x05, 0x09, 0x23, 0xA1, 0x02,
    0x09, 0x52, 0x09, 0x53, 0x15, 0x00, 0x25, 0x0A, 0x75, 0x08, 0x95, 0x02, 0xB1, 0x02,
    0xC0, 0xC0
};
#include <pshpack1.h>
typedef struct { UCHAR Tip; UCHAR Id; USHORT X; USHORT Y; } CONTACT;
typedef struct { UCHAR Id; CONTACT C[MAX_CONTACTS]; UCHAR Count; } TOUCH_REPORT;
typedef struct { UCHAR Id; UCHAR Mods; UCHAR Rsvd; UCHAR Keys[6]; } KBD_REPORT;
typedef struct { UCHAR Id; UCHAR Bits; } CC_REPORT;
typedef struct { UCHAR Id; UCHAR Buttons; USHORT X; USHORT Y; } MOUSE_REPORT;
#include <poppack.h>
#define QSZ 40
C_ASSERT(sizeof(TOUCH_REPORT) <= QSZ);

static const HID_DESCRIPTOR HidDescriptor = {
    sizeof(HID_DESCRIPTOR), 0x21, 0x0101, 0x00, 0x01,
    { 0x22, sizeof(ReportDescriptor) }
};

typedef struct _DEV_CTX {
    WDFQUEUE ReadQueue;
    PUCHAR CmuPeric1, CmuCcore, PinAlive, PinCcore, PinTouch, I2c7, I2c15;
    PKTHREAD Thread;
    KEVENT StopEvent;
    BOOLEAN HwOk;
    volatile LONG TouchOk;     /* touch collection queried us -> send touch, not mouse */
    UCHAR Q[16][QSZ];           /* pending raw reports */
    UCHAR QLen[16];
    ULONG QHead, QCount;
    KSPIN_LOCK Lock;
} DEV_CTX, *PDEV_CTX;
/* Hand-rolled WDF_DECLARE_CONTEXT_TYPE: the macro puts a const object into
 * ".data" via __declspec(allocate), which clang turns into a second,
 * read-only .data section. */
static WDF_OBJECT_CONTEXT_TYPE_INFO DevCtxTypeInfo = {
    sizeof(WDF_OBJECT_CONTEXT_TYPE_INFO), "DEV_CTX", sizeof(DEV_CTX), &DevCtxTypeInfo, NULL
};
#define GetCtx(h) ((PDEV_CTX)WdfObjectGetTypedContextWorker((WDFOBJECT)(h), DevCtxTypeInfo.UniqueType))

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE EvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE EvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY EvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT EvtD0Exit;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL EvtInternalIoctl;

/* ---- helpers ----------------------------------------------------------- */
static __inline ULONG Rd(PUCHAR b, ULONG o) { return READ_REGISTER_ULONG((volatile ULONG *)(b + o)); }
static __inline void Wr(PUCHAR b, ULONG o, ULONG v) { WRITE_REGISTER_ULONG((volatile ULONG *)(b + o), v); }

static void SleepMs(ULONG ms)
{
    LARGE_INTEGER t; t.QuadPart = -(LONGLONG)ms * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &t);
}

static PUCHAR MapIo(ULONGLONG pa, SIZE_T len)
{
    PHYSICAL_ADDRESS p; p.QuadPart = (LONGLONG)pa;
    return (PUCHAR)MmMapIoSpaceEx(p, len, PAGE_READWRITE | PAGE_NOCACHE);
}

/* pin config: 4-bit function field in CON (+0), PUD (+8) 4 bits/pin */
static void PinFunc(PUCHAR bank, ULONG pin, ULONG func)
{
    ULONG con = Rd(bank, 0x0);
    con &= ~(0xFu << (pin * 4)); con |= (func & 0xF) << (pin * 4);
    Wr(bank, 0x0, con);
    ULONG pud = Rd(bank, 0x8);
    pud &= ~(0xFu << (pin * 4));
    Wr(bank, 0x8, pud);
}

/* ---- HSI2C (polled, auto mode) ----------------------------------------- */
static void I2cSetup(PUCHAR r)
{
    /* Exynos8895-style timing; assume ipclk <= 133 MHz, target 400 kHz.
     * A slower real clock only makes the bus slower. */
    const ULONG clkin = 133000000, op = 400000;
    ULONG ctl = Rd(r, HSI2C_CTL);
    Wr(r, HSI2C_CTL, ctl | CTL_SW_RST);
    Wr(r, HSI2C_CTL, ctl & ~CTL_SW_RST);

    ULONG flt = (Rd(r, HSI2C_CONF) >> 16) & 0x7;
    LONG temp = (LONG)(clkin / op) - (LONG)(flt + 3) * 2;
    LONG div = temp / 512;
    LONG cyc = (temp + (LONG)((flt + 3) % (div + 1)) * 2) / (div + 1) - 2;
    ULONG tl = (ULONG)(cyc * 64 / 100); if (tl > 0xFF) tl = 0xFF;
    ULONG th = (ULONG)cyc - tl;
    Wr(r, HSI2C_TIMING_FS1, tl << 24 | tl << 16 | tl << 8);
    Wr(r, HSI2C_TIMING_FS2, (tl / 2) << 24 | tl << 8 | th);
    Wr(r, HSI2C_TIMING_FS3, (ULONG)div << 16 | (ULONG)cyc);
    Wr(r, HSI2C_TIMING_SLA, tl / 2);

    Wr(r, HSI2C_TIMEOUT, Rd(r, HSI2C_TIMEOUT) & ~TIMEOUT_EN);
    Wr(r, HSI2C_CTL, CTL_FUNC_MODE_I2C | CTL_MASTER);
    Wr(r, HSI2C_TRAILIG_CTL, 0xF);
    Wr(r, HSI2C_CONF, Rd(r, HSI2C_CONF) | CONF_AUTO_MODE);
}

/* one message, len <= 64 (FIFO depth) */
static NTSTATUS I2cMsg(PUCHAR r, UCHAR addr, BOOLEAN read, PUCHAR buf, ULONG len, BOOLEAN stop)
{
    ULONG ctl = Rd(r, HSI2C_CTL) & ~(CTL_TXCHON | CTL_RXCHON);
    ULONG auto_conf = len | AUTO_RUN | (stop ? AUTO_STOP : 0);
    ULONG i, st = 0, spins;

    Wr(r, HSI2C_INT_STATUS, 0xFFFFFFFF);
    Wr(r, HSI2C_ADDR, (ULONG)(addr & 0x3ff) << 10);
    Wr(r, HSI2C_FIFO_CTL, FIFO_RX_EN | FIFO_TX_EN | (read ? (len << 4) : (len << 16)));
    if (read) { ctl |= CTL_RXCHON; auto_conf |= AUTO_READ; } else ctl |= CTL_TXCHON;
    Wr(r, HSI2C_CTL, ctl);
    Wr(r, HSI2C_INT_ENABLE, INT_TRANS_DONE | INT_ERRS);
    if (!read) for (i = 0; i < len; i++) Wr(r, HSI2C_TX_DATA, buf[i]);
    Wr(r, HSI2C_AUTO_CONF, auto_conf);

    for (spins = 0; spins < 20000; spins++) {      /* ~20 ms */
        st = Rd(r, HSI2C_INT_STATUS);
        if (st & (INT_TRANS_DONE | INT_ERRS)) break;
        KeStallExecutionProcessor(1);
    }
    Wr(r, HSI2C_INT_STATUS, st);
    Wr(r, HSI2C_INT_ENABLE, 0);
    if (!(st & INT_TRANS_DONE) || (st & INT_ERRS)) {
        I2cSetup(r);                                   /* reset the block on error */
        return (st & (INT_NO_DEV_ACK | INT_NO_DEV)) ? STATUS_NO_SUCH_DEVICE : STATUS_IO_TIMEOUT;
    }
    if (read) {
        for (i = 0; i < len; i++) {
            for (spins = 0; spins < 2000 && FIFO_RX_LVL(Rd(r, HSI2C_FIFO_STATUS)) == 0; spins++)
                KeStallExecutionProcessor(1);
            buf[i] = (UCHAR)Rd(r, HSI2C_RX_DATA);
        }
    }
    return STATUS_SUCCESS;
}

/* write reg, then read len bytes (repeated start) */
static NTSTATUS I2cReadReg(PUCHAR r, UCHAR addr, UCHAR reg, PUCHAR buf, ULONG len)
{
    NTSTATUS st = I2cMsg(r, addr, FALSE, &reg, 1, FALSE);
    if (!NT_SUCCESS(st)) return st;
    return I2cMsg(r, addr, TRUE, buf, len, TRUE);
}

static NTSTATUS I2cWrite(PUCHAR r, UCHAR addr, const UCHAR *data, ULONG len)
{
    UCHAR tmp[8]; ULONG i;
    for (i = 0; i < len && i < sizeof(tmp); i++) tmp[i] = data[i];
    return I2cMsg(r, addr, FALSE, tmp, len, TRUE);
}

/* ---- diagnostics: HKLM\...\Services\hero2touch\Parameters\<name> ---- */
static void Diag(PCWSTR name, ULONG value)
{
    WDFKEY key; UNICODE_STRING n;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    if (!NT_SUCCESS(WdfDriverOpenParametersRegistryKey(WdfGetDriver(), KEY_WRITE, WDF_NO_OBJECT_ATTRIBUTES, &key)))
        return;
    RtlInitUnicodeString(&n, name);
    WdfRegistryAssignULong(key, &n, value);
    WdfRegistryClose(key);
}

/* ---- hardware bring-up ------------------------------------------------- */
static NTSTATUS PmicLdoOn(PDEV_CTX c, UCHAR reg, UCHAR vsel)
{
    UCHAR v = 0, w[2];
    NTSTATUS st = I2cReadReg(c->I2c15, PMIC_ADDR, reg, &v, 1);
    DBG("PMIC reg %02x = %02x (st %x)", reg, v, st);
    Diag(reg == PMIC_L32CTRL ? L"PmicL32Before" : L"PmicL33Before", NT_SUCCESS(st) ? v : (ULONG)st);
    if (!NT_SUCCESS(st)) return st;
    w[0] = reg; w[1] = (UCHAR)(0xC0 | (vsel & 0x3F));
    if (v == w[1]) return STATUS_SUCCESS;
    return I2cWrite(c->I2c15, PMIC_ADDR, w, 2);
}

static NTSTATUS HwInit(PDEV_CTX c)
{
    NTSTATUS st;
    UCHAR ev[8] = {0}, bs = 0, id[3] = {0};
    int tries;

    c->CmuPeric1 = MapIo(CMU_PERIC1_BASE, 0x1000);
    c->CmuCcore  = MapIo(CMU_CCORE_BASE, 0x1000);
    c->PinAlive  = MapIo(PINCTRL_ALIVE, 0x1000);
    c->PinCcore  = MapIo(PINCTRL_CCORE, 0x1000);
    c->PinTouch  = MapIo(PINCTRL_TOUCH, 0x1000);
    c->I2c7      = MapIo(HSI2C7_BASE, 0x1000);
    c->I2c15     = MapIo(HSI2C15_BASE, 0x1000);
    if (!c->CmuPeric1 || !c->CmuCcore || !c->PinAlive || !c->PinCcore ||
        !c->PinTouch || !c->I2c7 || !c->I2c15)
        return STATUS_INSUFFICIENT_RESOURCES;

    Diag(L"Stage", 1);
    Diag(L"CmuPeric1_804", Rd(c->CmuPeric1, 0x804));
    Diag(L"CmuCcore_900", Rd(c->CmuCcore, 0x900));
    Diag(L"GpioA0Dat", Rd(c->PinAlive, 0x4));
    Diag(L"GpioA1Con", Rd(c->PinAlive + 0x20, 0x0));
    /* clock gates */
    Wr(c->CmuPeric1, 0x804, Rd(c->CmuPeric1, 0x804) | (1u << 3));
    Wr(c->CmuCcore, 0x900, Rd(c->CmuCcore, 0x900) | (1u << 0));
    /* pinmux: HSI2C7 = gpf1-0/1 func2, HSI2C15 = etc0-0/1 func2, IRQ gpa1-0 input */
    PinFunc(c->PinTouch, 0, 2); PinFunc(c->PinTouch, 1, 2);
    PinFunc(c->PinCcore, 0, 2); PinFunc(c->PinCcore, 1, 2);
    PinFunc(c->PinAlive + 0x20, 0, 0);

    Diag(L"Stage", 2);
    I2cSetup(c->I2c15);
    I2cSetup(c->I2c7);
    Diag(L"I2c15Conf", Rd(c->I2c15, HSI2C_CONF));
    Diag(L"I2c7Conf", Rd(c->I2c7, HSI2C_CONF));
    Diag(L"Stage", 3);

    st = PmicLdoOn(c, PMIC_L32CTRL, 46);
    if (NT_SUCCESS(st)) st = PmicLdoOn(c, PMIC_L33CTRL, 60);
    Diag(L"PmicStatus", (ULONG)st);
    Diag(L"Stage", 4);
    if (!NT_SUCCESS(st)) DBG("PMIC failed %x, trying touch anyway", st);
    SleepMs(150);

    for (tries = 0; tries < 10; tries++) {
        st = I2cReadReg(c->I2c7, TS_ADDR, TS_READ_ONE_EVENT, ev, 8);
        if (NT_SUCCESS(st)) break;
        SleepMs(30);
    }
    DBG("first event %02x %02x st %x", ev[0], ev[1], st);
    Diag(L"TouchFirstEvent", (ULONG)ev[0] | (ULONG)ev[1] << 8 | (ULONG)ev[2] << 16 | (ULONG)ev[3] << 24);
    Diag(L"TouchStatus", (ULONG)st);
    Diag(L"Stage", 5);
    if (!NT_SUCCESS(st)) return st;

    I2cReadReg(c->I2c7, TS_ADDR, TS_BOOT_STATUS, &bs, 1);
    I2cReadReg(c->I2c7, TS_ADDR, TS_DEVICE_ID, id, 3);
    DBG("boot status %02x devid %02x%02x%02x", bs, id[0], id[1], id[2]);
    Diag(L"TouchBootStatus", bs);
    Diag(L"TouchDevId", (ULONG)id[0] << 16 | (ULONG)id[1] << 8 | id[2]);

    { UCHAR w[2] = { TS_TOUCH_FUNCTION, 0x01 }; I2cWrite(c->I2c7, TS_ADDR, w, 2); }
    SleepMs(20);
    { UCHAR w = TS_CLEAR_EVENTS; I2cWrite(c->I2c7, TS_ADDR, &w, 1); }
    { UCHAR w = TS_SENSE_ON; st = I2cWrite(c->I2c7, TS_ADDR, &w, 1); }
    Diag(L"SenseOnStatus", (ULONG)st);
    Diag(L"Stage", 6);
    return st;
}

/* ---- report delivery --------------------------------------------------- */
static void TryComplete(PDEV_CTX c)
{
    for (;;) {
        WDFREQUEST req;
        UCHAR buf[QSZ]; ULONG len;
        KIRQL irql;

        KeAcquireSpinLock(&c->Lock, &irql);
        if (!c->QCount) { KeReleaseSpinLock(&c->Lock, irql); return; }
        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(c->ReadQueue, &req))) {
            KeReleaseSpinLock(&c->Lock, irql); return;
        }
        len = c->QLen[c->QHead];
        RtlCopyMemory(buf, c->Q[c->QHead], len);
        c->QHead = (c->QHead + 1) % 16; c->QCount--;
        KeReleaseSpinLock(&c->Lock, irql);

        {
            WDFMEMORY mem; NTSTATUS st = WdfRequestRetrieveOutputMemory(req, &mem);
            if (NT_SUCCESS(st)) st = WdfMemoryCopyFromBuffer(mem, 0, buf, len);
            WdfRequestCompleteWithInformation(req, st, NT_SUCCESS(st) ? len : 0);
        }
    }
}

static void Enqueue(PDEV_CTX c, const void *rep, ULONG len, BOOLEAN coalesce)
{
    KIRQL irql;
    KeAcquireSpinLock(&c->Lock, &irql);
    if (coalesce && c->QCount) {
        /* replace the newest report if it has the same ID (pointer moves) */
        ULONG last = (c->QHead + c->QCount - 1) % 16;
        if (c->Q[last][0] == ((const UCHAR *)rep)[0] && c->QLen[last] == len) {
            RtlCopyMemory(c->Q[last], rep, len);
            KeReleaseSpinLock(&c->Lock, irql); TryComplete(c); return;
        }
    }
    if (c->QCount == 16) { c->QHead = (c->QHead + 1) % 16; c->QCount--; }
    {
        ULONG slot = (c->QHead + c->QCount) % 16;
        RtlCopyMemory(c->Q[slot], rep, len); c->QLen[slot] = (UCHAR)len; c->QCount++;
    }
    KeReleaseSpinLock(&c->Lock, irql);
    TryComplete(c);
}

typedef struct { BOOLEAN Used, Down; UCHAR Tid; USHORT X, Y; } SLOT;

/* one parallel-mode report with every tracked contact; lifted ones go out
 * once with Tip=0 and are then freed */
static void PublishTouch(PDEV_CTX c, SLOT *sl)
{
    if (!c->TouchOk) {
        /* fallback: first contact as an absolute mouse */
        static MOUSE_REPORT last = { 6, 0, 0, 0 };
        MOUSE_REPORT m = last; m.Buttons = 0;
        for (int i = 0; i < MAX_CONTACTS; i++)
            if (sl[i].Used && sl[i].Down) { m.Buttons = 1; m.X = sl[i].X; m.Y = sl[i].Y; break; }
        for (int i = 0; i < MAX_CONTACTS; i++) if (sl[i].Used && !sl[i].Down) sl[i].Used = FALSE;
        Enqueue(c, &m, sizeof(m), (BOOLEAN)(m.Buttons && last.Buttons));
        last = m;
        return;
    }
    TOUCH_REPORT r; RtlZeroMemory(&r, sizeof(r)); r.Id = 1;
    for (int i = 0; i < MAX_CONTACTS; i++) {
        if (!sl[i].Used) continue;
        CONTACT *k = &r.C[r.Count++];
        k->Tip = sl[i].Down ? 1 : 0; k->Id = sl[i].Tid; k->X = sl[i].X; k->Y = sl[i].Y;
        if (!sl[i].Down) sl[i].Used = FALSE;
    }
    Enqueue(c, &r, sizeof(r), FALSE);
}

/* ---- polling thread ---------------------------------------------------- */
static VOID PollThread(PVOID ctx)
{
    PDEV_CTX c = (PDEV_CTX)ctx;
    LARGE_INTEGER period; period.QuadPart = -80000; /* 8 ms */
    UCHAR ev[8];
    SLOT sl[MAX_CONTACTS];
    BOOLEAN home = FALSE, vup = FALSE, vdn = FALSE, pwr = FALSE;
    ULONG ticks = 0, events = 0, irqlow = 0;
    RtlZeroMemory(sl, sizeof(sl));
    Diag(L"PollThread", 1);

    for (;;) {
        if (KeWaitForSingleObject(&c->StopEvent, Executive, KernelMode, FALSE, &period) == STATUS_SUCCESS)
            break;
        if (c->PinAlive) {
            /* keys are active low: Home gpa0-5, VolUp gpa0-3, VolDown gpa0-4 */
            ULONG d = Rd(c->PinAlive, 0x4);
            BOOLEAN h = !(d & (1u << 5)), u = !(d & (1u << 3)), v = !(d & (1u << 4));
            BOOLEAN p = !(Rd(c->PinAlive + 0x40, 0x4) & (1u << 4));   /* Power gpa2-4 */
            if (h != home || p != pwr) {
                KBD_REPORT k; RtlZeroMemory(&k, sizeof(k)); k.Id = 2;
                k.Mods = (h || p) ? 0x08 : 0; k.Keys[0] = p ? 0x0F : 0;   /* Win, Win+L */
                Enqueue(c, &k, sizeof(k), FALSE); home = h; pwr = p;
            }
            if (u != vup || v != vdn) {
                CC_REPORT cc; cc.Id = 3; cc.Bits = (UCHAR)((u ? 1 : 0) | (v ? 2 : 0));
                Enqueue(c, &cc, sizeof(cc), FALSE); vup = u; vdn = v;
            }
        }
        if (++ticks % 125 == 0) { Diag(L"PollTicks", ticks); Diag(L"TouchEvents", events); Diag(L"IrqLowSeen", irqlow); }
        if (!c->HwOk) continue;
        if (!(Rd(c->PinAlive + 0x20, 0x4) & 1)) irqlow++;
        /* IRQ line low = events pending */
        BOOLEAN changed = FALSE;
        for (int n = 0; n < 32 && !(Rd(c->PinAlive + 0x20, 0x4) & 1); n++) {
            if (!NT_SUCCESS(I2cReadReg(c->I2c7, TS_ADDR, TS_READ_ONE_EVENT, ev, 8)) || !ev[0])
                break;
            events++;
            if ((ev[0] >> 6) != 1 || !(ev[1] & 0x0F)) continue;   /* not a coordinate */
            UCHAR tid = (ev[1] & 0x0F) - 1, state = ev[0] & 7;
            USHORT x = (USHORT)((ev[2] << 4) | (ev[4] >> 4));
            USHORT y = (USHORT)((ev[3] << 4) | (ev[4] & 0x0F));
            if (x > TS_MAX) x = TS_MAX;
            if (y > TS_MAX) y = TS_MAX;
            int i, f = -1;
            for (i = 0; i < MAX_CONTACTS; i++)
                if (sl[i].Used && sl[i].Down && sl[i].Tid == tid) break;
            if (state == 1 || state == 2) {
                if (i == MAX_CONTACTS) {
                    for (int j = 0; j < MAX_CONTACTS; j++) if (!sl[j].Used) { f = j; break; }
                    if (f < 0) continue;                       /* > 5 fingers */
                    i = f; sl[i].Used = TRUE; sl[i].Down = TRUE; sl[i].Tid = tid;
                }
                sl[i].X = x; sl[i].Y = y; changed = TRUE;
            } else if (state == 3 && i < MAX_CONTACTS) {
                sl[i].Down = FALSE; changed = TRUE;
            }
        }
        if (changed) PublishTouch(c, sl);
        TryComplete(c);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/* ---- WDF plumbing ------------------------------------------------------ */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG cfg;
    WDF_DRIVER_CONFIG_INIT(&cfg, EvtDeviceAdd);
    DBG("DriverEntry %s", "v2");
    NTSTATUS st = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &cfg, WDF_NO_HANDLE);
    if (NT_SUCCESS(st)) { Diag(L"DriverEntry", 2); Diag(L"Stage", 0); }
    return st;
}

NTSTATUS EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT Init)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_IO_QUEUE_CONFIG qc;
    WDFDEVICE dev;
    PDEV_CTX c;
    NTSTATUS st;
    UNREFERENCED_PARAMETER(Driver);

    Diag(L"DeviceAdd", 1);
    WdfFdoInitSetFilter(Init);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = EvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware = EvtReleaseHardware;
    pnp.EvtDeviceD0Entry = EvtD0Entry;
    pnp.EvtDeviceD0Exit = EvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(Init, &pnp);

    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ContextTypeInfo = &DevCtxTypeInfo;
    st = WdfDeviceCreate(&Init, &attr, &dev);
    if (!NT_SUCCESS(st)) return st;
    c = GetCtx(dev);
    KeInitializeSpinLock(&c->Lock);
    KeInitializeEvent(&c->StopEvent, NotificationEvent, FALSE);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qc, WdfIoQueueDispatchParallel);
    qc.EvtIoInternalDeviceControl = EvtInternalIoctl;
    st = WdfIoQueueCreate(dev, &qc, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (!NT_SUCCESS(st)) return st;

    WDF_IO_QUEUE_CONFIG_INIT(&qc, WdfIoQueueDispatchManual);
    st = WdfIoQueueCreate(dev, &qc, WDF_NO_OBJECT_ATTRIBUTES, &c->ReadQueue);
    return st;
}

NTSTATUS EvtPrepareHardware(WDFDEVICE dev, WDFCMRESLIST raw, WDFCMRESLIST xlat)
{
    PDEV_CTX c = GetCtx(dev);
    NTSTATUS st;
    UNREFERENCED_PARAMETER(raw); UNREFERENCED_PARAMETER(xlat);
    Diag(L"PrepareHardware", 1);
    st = HwInit(c);
    DBG("HwInit -> %x", st);
    Diag(L"HwInitStatus", (ULONG)st);
    c->HwOk = NT_SUCCESS(st);
    return STATUS_SUCCESS;     /* keep the HID device even if the panel did not answer */
}

NTSTATUS EvtReleaseHardware(WDFDEVICE dev, WDFCMRESLIST xlat)
{
    PDEV_CTX c = GetCtx(dev);
    UNREFERENCED_PARAMETER(xlat);
    c->HwOk = FALSE;
    return STATUS_SUCCESS;
}

NTSTATUS EvtD0Entry(WDFDEVICE dev, WDF_POWER_DEVICE_STATE prev)
{
    PDEV_CTX c = GetCtx(dev);
    HANDLE h; OBJECT_ATTRIBUTES oa; NTSTATUS st;
    UNREFERENCED_PARAMETER(prev);
    KeClearEvent(&c->StopEvent);
    InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    st = PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL, PollThread, c);
    if (!NT_SUCCESS(st)) return st;
    ObReferenceObjectByHandle(h, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, (PVOID *)&c->Thread, NULL);
    ZwClose(h);
    return STATUS_SUCCESS;
}

NTSTATUS EvtD0Exit(WDFDEVICE dev, WDF_POWER_DEVICE_STATE target)
{
    PDEV_CTX c = GetCtx(dev);
    UNREFERENCED_PARAMETER(target);
    KeSetEvent(&c->StopEvent, 0, FALSE);
    if (c->Thread) {
        KeWaitForSingleObject(c->Thread, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(c->Thread);
        c->Thread = NULL;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS CopyOut(WDFREQUEST req, const void *src, size_t len, size_t *info)
{
    WDFMEMORY mem;
    NTSTATUS st = WdfRequestRetrieveOutputMemory(req, &mem);
    size_t have;
    if (!NT_SUCCESS(st)) return st;
    WdfMemoryGetBuffer(mem, &have);
    if (have < len) return STATUS_INVALID_BUFFER_SIZE;
    st = WdfMemoryCopyFromBuffer(mem, 0, (PVOID)src, len);
    if (NT_SUCCESS(st)) *info = len;
    return st;
}

VOID EvtInternalIoctl(WDFQUEUE q, WDFREQUEST req, size_t outLen, size_t inLen, ULONG code)
{
    PDEV_CTX c = GetCtx(WdfIoQueueGetDevice(q));
    NTSTATUS st = STATUS_NOT_SUPPORTED;
    size_t info = 0;
    UNREFERENCED_PARAMETER(outLen); UNREFERENCED_PARAMETER(inLen);

    switch (code) {
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        st = CopyOut(req, &HidDescriptor, sizeof(HidDescriptor), &info);
        break;
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES: {
        HID_DEVICE_ATTRIBUTES a; RtlZeroMemory(&a, sizeof(a));
        a.Size = sizeof(a); a.VendorID = 0x04E8; a.ProductID = 0x0552; a.VersionNumber = 2;
        st = CopyOut(req, &a, sizeof(a), &info);
        break; }
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        st = CopyOut(req, ReportDescriptor, sizeof(ReportDescriptor), &info);
        break;
    case IOCTL_HID_READ_REPORT:
        st = WdfRequestForwardToIoQueue(req, c->ReadQueue);
        if (NT_SUCCESS(st)) { TryComplete(c); return; }
        break;
    case IOCTL_HID_GET_FEATURE: {
        /* mshidkmdf: report id in the input buffer, report in the output buffer */
        WDFMEMORY in, out; size_t inl = 0, outl = 0; UCHAR id = 0, rep[3];
        ULONG n = 0;
        if (NT_SUCCESS(WdfRequestRetrieveInputMemory(req, &in))) {
            PUCHAR p = (PUCHAR)WdfMemoryGetBuffer(in, &inl); if (inl) id = p[0];
        }
        if (!NT_SUCCESS(WdfRequestRetrieveOutputMemory(req, &out))) { st = STATUS_INVALID_PARAMETER; Diag(L"FeatErr", 1); break; }
        WdfMemoryGetBuffer(out, &outl);
        if (!id && outl) WdfMemoryCopyToBuffer(out, 0, &id, 1);
        if (id == 4)      { rep[0] = 4; rep[1] = MAX_CONTACTS; n = 2; InterlockedExchange(&c->TouchOk, 1); }
        else if (id == 5) { rep[0] = 5; rep[1] = 2; rep[2] = 0; n = 3; }
        Diag(L"FeatId", id); Diag(L"FeatOutLen", (ULONG)outl);
        if (!n || outl < n) { st = STATUS_INVALID_PARAMETER; break; }
        st = WdfMemoryCopyFromBuffer(out, 0, rep, n);
        if (NT_SUCCESS(st)) info = n;
        break; }
    case IOCTL_HID_SET_FEATURE:
    case IOCTL_HID_ACTIVATE_DEVICE:
    case IOCTL_HID_DEACTIVATE_DEVICE:
        st = STATUS_SUCCESS;
        break;
    default:
        break;
    }
    WdfRequestCompleteWithInformation(req, st, info);
}
