/*
 * hero2ufs - Storport miniport for the Exynos 8890 UFS host (Galaxy S7 Edge).
 *
 * The UEFI firmware (SamsungPkg UfsDxe + S5E8890 PlatformUfsLib) already did
 * the vendor PHY calibration, link startup and HS gear switch, and leaves the
 * link up. This driver only takes over the UTP transfer request list and
 * exchanges SCSI commands with the device, polled, one request at a time.
 *
 * Exynos specifics (from UfsDxe / Linux ufs-exynos):
 *  - PRDT length / offsets in the UTRD are in bytes (PRDT_BYTE_GRAN)
 *  - each PRDT entry covers at most 4 KiB (VS_TXPRDT/RXPRDT_ENTRY_SIZE = 12)
 *  - VS_UTRL_NEXUS_TYPE must be all-ones for SCSI commands
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <ntddk.h>
#include <storport.h>

/* ---- registers --------------------------------------------------------- */
#define REG_IS          0x20
#define REG_IE          0x24
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5C
#define REG_UTRLRSR     0x60
#define REG_UTMRLBA     0x70
#define REG_UTMRLBAU    0x74
#define REG_UTMRLRSR    0x80
#define VS_TXPRDT_ENTRY_SIZE 0x100
#define VS_RXPRDT_ENTRY_SIZE 0x104
#define VS_UTRL_NEXUS_TYPE   0x140
#define VS_UMTRL_NEXUS_TYPE  0x144
#define VS_DATA_REORDER      0x160

#define IS_UTRCS        (1u << 0)
#define IS_FATAL        ((1u << 11) | (1u << 16) | (1u << 17) | (1u << 7))

#define UTP_HOST_TO_DEVICE  0x02000000
#define UTP_DEVICE_TO_HOST  0x04000000
#define UTP_INT_CMD         0x01000000
#define OCS_SUCCESS         0x0
#define OCS_INVALID         0xF

#define UPIU_COMMAND        0x01
#define UPIU_FLAG_WRITE     0x20
#define UPIU_FLAG_READ      0x40

#define PRDT_SEG            4096u
#define MAX_XFER            (512u * 1024u)
#define MAX_PRDT            256u
#define UPIU_SIZE           1024u

#include <pshpack1.h>
typedef struct {
    UCHAR  Type, Flags, Lun, Tag;
    UCHAR  CmdSet, Function, Response, Status;
    UCHAR  EhsLength, DeviceInfo;
    USHORT DataLength;         /* big endian */
} UPIU_HDR;
#include <poppack.h>

typedef struct {
    ULONG Dw[4];
    ULONG UcdLo, UcdHi;
    USHORT RspLen, RspOff;     /* bytes on Exynos */
    USHORT PrdtLen, PrdtOff;   /* bytes on Exynos */
} UTRD;

typedef struct { ULONG AddrLo, AddrHi, Rsvd, Size; } PRDT;

typedef struct {
    UCHAR Cmd[UPIU_SIZE];
    UCHAR Rsp[UPIU_SIZE];
    PRDT  Prdt[MAX_PRDT];
} UCD;

/* uncached layout: UTRL (1 KiB aligned) | UTMRL | UCD (page aligned) */
#define OFF_UTRL   0x0000
#define OFF_UTMRL  0x0400
#define OFF_UCD    0x1000
#define OFF_DATA   0x3000
#define DATA_SIZE  0x8000
#define UNCACHED_SIZE (OFF_DATA + DATA_SIZE)

typedef struct {
    PUCHAR Hci;               /* 0x155A0000 */
    PUCHAR Base;              /* uncached extension VA */
    STOR_PHYSICAL_ADDRESS BasePa;
    UTRD  *Utrd;
    UCD   *Ucd;
    ULONG Commands, Errors;
    ULONG LastOcs, LastScsi, LastIs;
    ULONG LastOp, LastSrb, LastLen, LastSegs, LastPaLo, LastPaHi, LastFlags;
    BOOLEAN Ready;
} HW_EXT, *PHW_EXT;

static __inline ULONG Rd(PHW_EXT e, ULONG o) { return StorPortReadRegisterUlong(e, (PULONG)(e->Hci + o)); }
static __inline void Wr(PHW_EXT e, ULONG o, ULONG v) { StorPortWriteRegisterUlong(e, (PULONG)(e->Hci + o), v); }

/* ---- diagnostics into Services\hero2ufs\Parameters (passive only) ------ */
static void Diag(PCWSTR name, ULONG value)
{
    UNICODE_STRING path, n; OBJECT_ATTRIBUTES oa; HANDLE key; ULONG disp;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return;
    RtlInitUnicodeString(&path, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\hero2ufs\\Parameters");
    InitializeObjectAttributes(&oa, &path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    if (!NT_SUCCESS(ZwCreateKey(&key, KEY_SET_VALUE, &oa, 0, NULL, 0, &disp))) return;
    RtlInitUnicodeString(&n, name);
    ZwSetValueKey(key, &n, 0, REG_DWORD, &value, sizeof(value));
    ZwClose(key);
}

/* ---- one SCSI command, polled ------------------------------------------ */
typedef struct { STOR_PHYSICAL_ADDRESS Pa; ULONG Len; } SEG;

static UCHAR UfsExec(PHW_EXT e, UCHAR lun, const UCHAR *cdb, ULONG cdbLen,
                     ULONG dataLen, BOOLEAN toDevice, const SEG *segs, ULONG nsegs,
                     PUCHAR sense, ULONG senseLen, PUCHAR scsiStatus)
{
    UTRD *t = e->Utrd;
    UCD *u = e->Ucd;
    UPIU_HDR *h = (UPIU_HDR *)u->Cmd;
    UPIU_HDR *r = (UPIU_HDR *)u->Rsp;
    ULONG i, n = 0, spins, is = 0, ocs;
    STOR_PHYSICAL_ADDRESS ucdPa;

    RtlZeroMemory(u->Cmd, sizeof(u->Cmd));
    RtlZeroMemory(u->Rsp, 64);
    RtlZeroMemory(t, sizeof(*t));

    /* PRDT: split every SG element into <= 4 KiB pieces */
    for (i = 0; i < nsegs && n < MAX_PRDT; i++) {
        ULONGLONG pa = (ULONGLONG)segs[i].Pa.QuadPart;
        ULONG left = segs[i].Len;
        while (left && n < MAX_PRDT) {
            ULONG chunk = left > PRDT_SEG ? PRDT_SEG : left;
            u->Prdt[n].AddrLo = (ULONG)pa;
            u->Prdt[n].AddrHi = (ULONG)(pa >> 32);
            u->Prdt[n].Rsvd = 0;
            u->Prdt[n].Size = chunk - 1;
            pa += chunk; left -= chunk; n++;
        }
    }

    h->Type = UPIU_COMMAND;
    h->Flags = dataLen ? (toDevice ? UPIU_FLAG_WRITE : UPIU_FLAG_READ) : 0;
    h->Lun = lun;
    h->Tag = 0;
    {
        PUCHAR tsf = u->Cmd + sizeof(UPIU_HDR);
        tsf[0] = (UCHAR)(dataLen >> 24); tsf[1] = (UCHAR)(dataLen >> 16);
        tsf[2] = (UCHAR)(dataLen >> 8);  tsf[3] = (UCHAR)dataLen;
        RtlCopyMemory(tsf + 4, cdb, cdbLen > 16 ? 16 : cdbLen);
    }

    ucdPa.QuadPart = e->BasePa.QuadPart + OFF_UCD;
    t->Dw[0] = UTP_INT_CMD | (dataLen ? (toDevice ? UTP_HOST_TO_DEVICE : UTP_DEVICE_TO_HOST) : 0);
    t->Dw[2] = OCS_INVALID;
    t->UcdLo = ucdPa.LowPart;
    t->UcdHi = (ULONG)ucdPa.HighPart;
    t->RspOff = (USHORT)FIELD_OFFSET(UCD, Rsp);
    t->RspLen = UPIU_SIZE;
    t->PrdtOff = dataLen ? (USHORT)FIELD_OFFSET(UCD, Prdt) : 0;
    t->PrdtLen = dataLen ? (USHORT)(n * sizeof(PRDT)) : 0;

    KeMemoryBarrier();
    Wr(e, REG_IS, Rd(e, REG_IS));
    Wr(e, REG_UTRLDBR, 1);

    for (spins = 0; spins < 5000000; spins++) {          /* ~5 s */
        is = Rd(e, REG_IS);
        if (!(Rd(e, REG_UTRLDBR) & 1)) break;
        if (is & IS_FATAL) break;
        KeStallExecutionProcessor(1);
    }
    Wr(e, REG_IS, is);
    KeMemoryBarrier();

    e->Commands++;
    ocs = t->Dw[2] & 0xF;
    e->LastOcs = ocs; e->LastIs = is;
    if (Rd(e, REG_UTRLDBR) & 1) {                          /* timeout: clear slot */
        Wr(e, REG_UTRLCLR, ~1u);
        e->Errors++;
        return SRB_STATUS_TIMEOUT;
    }
    if (ocs != OCS_SUCCESS || (is & IS_FATAL)) {
        e->Errors++;
        return SRB_STATUS_ERROR;
    }
    *scsiStatus = r->Status;
    e->LastScsi = r->Status;
    if (r->Status == SCSISTAT_CHECK_CONDITION && sense && senseLen) {
        PUCHAR d = u->Rsp + 32;                      /* data segment */
        ULONG sl = ((ULONG)d[0] << 8) | d[1];
        if (sl > senseLen) sl = senseLen;
        RtlCopyMemory(sense, d + 2, sl);
        return SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    }
    return r->Status == SCSISTAT_GOOD ? SRB_STATUS_SUCCESS : SRB_STATUS_ERROR;
}

/* The kernel loader always relocates drivers; make sure the image carries a
 * base relocation table even though the code itself is PC-relative. */
static void *volatile g_RelocAnchor = (void *)&g_RelocAnchor;

/* ---- Storport callbacks ------------------------------------------------ */
HW_FIND_ADAPTER HwFindAdapter;
HW_INITIALIZE HwInitialize;
HW_PASSIVE_INITIALIZE_ROUTINE HwPassiveInit;
HW_STARTIO HwStartIo;
HW_INTERRUPT HwInterrupt;
HW_RESET_BUS HwResetBus;
HW_ADAPTER_CONTROL HwAdapterControl;

ULONG HwFindAdapter(PVOID DevExt, PVOID HwContext, PVOID BusInfo, PCHAR Arg,
                    PPORT_CONFIGURATION_INFORMATION Cfg, PBOOLEAN Again)
{
    PHW_EXT e = (PHW_EXT)DevExt;
    ACCESS_RANGE *ar;
    ULONG len;
    UNREFERENCED_PARAMETER(HwContext); UNREFERENCED_PARAMETER(BusInfo); UNREFERENCED_PARAMETER(Arg);
    *Again = FALSE;

    Diag(L"FindAdapter", 1);
    Diag(L"AccessRanges", Cfg->NumberOfAccessRanges);
    Diag(L"InterfaceType", (ULONG)Cfg->AdapterInterfaceType);
    Diag(L"IrqLevel", Cfg->BusInterruptLevel);
    Diag(L"IrqVector", Cfg->BusInterruptVector);
    ar = &((*(Cfg->AccessRanges))[0]);
    if (ar->RangeLength == 0) { Diag(L"FindAdapterFail", 1); return SP_RETURN_NOT_FOUND; }
    e->Hci = (PUCHAR)StorPortGetDeviceBase(e, Cfg->AdapterInterfaceType, Cfg->SystemIoBusNumber,
                                           ar->RangeStart, ar->RangeLength, (BOOLEAN)!ar->RangeInMemory);
    if (!e->Hci) { Diag(L"FindAdapterFail", 2); return SP_RETURN_ERROR; }

    Cfg->MaximumTransferLength = MAX_XFER;
    Cfg->NumberOfPhysicalBreaks = 128;
    Cfg->AlignmentMask = 0x3;
    Cfg->ScatterGather = TRUE;
    Cfg->Master = TRUE;
    Cfg->CachesData = FALSE;
    Cfg->NumberOfBuses = 1;
    Cfg->MaximumNumberOfTargets = 1;
    Cfg->MaximumNumberOfLogicalUnits = 1;
    Cfg->Dma32BitAddresses = TRUE;
    Cfg->Dma64BitAddresses = SCSI_DMA64_MINIPORT_FULL64BIT_SUPPORTED;
    Cfg->SynchronizationModel = StorSynchronizeFullDuplex;
    Cfg->InitiatorBusId[0] = 7;
    Cfg->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;

    e->Base = (PUCHAR)StorPortGetUncachedExtension(e, Cfg, UNCACHED_SIZE);
    if (!e->Base) { Diag(L"FindAdapterFail", 3); return SP_RETURN_ERROR; }
    e->BasePa = StorPortGetPhysicalAddress(e, NULL, e->Base, &len);
    RtlZeroMemory(e->Base, UNCACHED_SIZE);
    e->Utrd = (UTRD *)(e->Base + OFF_UTRL);
    e->Ucd = (UCD *)(e->Base + OFF_UCD);

    Wr(e, REG_IE, 0);              /* we poll; keep the GIC line quiet */
    Diag(L"HCE", Rd(e, REG_HCE));
    Diag(L"HCS", Rd(e, REG_HCS));
    Diag(L"UcdPaHi", (ULONG)e->BasePa.HighPart);
    Diag(L"UcdPaLo", e->BasePa.LowPart);
    return SP_RETURN_FOUND;
}

BOOLEAN HwInitialize(PVOID DevExt)
{
    PHW_EXT e = (PHW_EXT)DevExt;
    STOR_PHYSICAL_ADDRESS utrl, utmrl;
    ULONG spins;

    Diag(L"HwInitialize", 1);
    utrl.QuadPart = e->BasePa.QuadPart + OFF_UTRL;
    utmrl.QuadPart = e->BasePa.QuadPart + OFF_UTMRL;

    /* stop lists, wait for in-flight UEFI work to finish */
    for (spins = 0; spins < 100000 && (Rd(e, REG_UTRLDBR) & 1); spins++) KeStallExecutionProcessor(1);
    Wr(e, REG_UTRLRSR, 0);
    Wr(e, REG_UTMRLRSR, 0);
    Wr(e, REG_IE, 0);
    Wr(e, REG_IS, Rd(e, REG_IS));

    Wr(e, REG_UTRLBA, utrl.LowPart);
    Wr(e, REG_UTRLBAU, (ULONG)utrl.HighPart);
    Wr(e, REG_UTMRLBA, utmrl.LowPart);
    Wr(e, REG_UTMRLBAU, (ULONG)utmrl.HighPart);

    Wr(e, VS_DATA_REORDER, 0xA);
    Wr(e, VS_TXPRDT_ENTRY_SIZE, 12);
    Wr(e, VS_RXPRDT_ENTRY_SIZE, 12);
    Wr(e, VS_UTRL_NEXUS_TYPE, 0xFFFFFFFF);
    Wr(e, VS_UMTRL_NEXUS_TYPE, 0xFFFFFFFF);

    Wr(e, REG_UTMRLRSR, 1);
    Wr(e, REG_UTRLRSR, 1);
    e->Ready = TRUE;

    return StorPortEnablePassiveInitialization(e, HwPassiveInit);
}

static VOID StatsThread(PVOID ctx)
{
    PHW_EXT e = (PHW_EXT)ctx;
    LARGE_INTEGER t; t.QuadPart = -20000000;   /* 2 s */
    for (;;) {
        KeDelayExecutionThread(KernelMode, FALSE, &t);
        Diag(L"Commands", e->Commands);
        Diag(L"Errors", e->Errors);
        Diag(L"LastOp", e->LastOp);
        Diag(L"LastSrb", e->LastSrb);
        Diag(L"LastLen", e->LastLen);
        Diag(L"LastSegs", e->LastSegs);
        Diag(L"LastPaHi", e->LastPaHi);
        Diag(L"LastPaLo", e->LastPaLo);
        Diag(L"LastFlags", e->LastFlags);
        Diag(L"LastOcs", e->LastOcs);
        Diag(L"LastScsi", e->LastScsi);
        Diag(L"LastIs", e->LastIs);
    }
}

/* data-in test through our own uncached buffer */
static UCHAR TestIn(PHW_EXT e, const UCHAR *cdb, ULONG cdbLen, ULONG len, PUCHAR st)
{
    SEG seg; UCHAR sense[18];
    seg.Pa.QuadPart = e->BasePa.QuadPart + OFF_DATA;
    seg.Len = len;
    RtlZeroMemory(e->Base + OFF_DATA, DATA_SIZE);
    return UfsExec(e, 0, cdb, cdbLen, len, FALSE, &seg, 1, sense, sizeof(sense), st);
}

static ULONG Crc32(const UCHAR *p, ULONG n)
{
    ULONG c = 0xFFFFFFFF, k;
    while (n--) { c ^= *p++; for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1))); }
    return ~c;
}
static ULONG Le32(PUCHAR p) { return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24; }

static ULONG Be32(PUCHAR p) { return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3]; }

BOOLEAN HwPassiveInit(PVOID DevExt)
{
    PHW_EXT e = (PHW_EXT)DevExt;
    UCHAR tur[6] = { 0 }, sense[18], st = 0, rc;
    ULONG i, bs = 4096;
    PUCHAR d = e->Base + OFF_DATA;

    Diag(L"PassiveInit", 1);
    Diag(L"HCS2", Rd(e, REG_HCS));
    /* the first commands after the hand-over usually report UNIT ATTENTION */
    for (i = 0; i < 4; i++) {
        rc = UfsExec(e, 0, tur, 6, 0, FALSE, NULL, 0, sense, sizeof(sense), &st);
        if (rc == SRB_STATUS_SUCCESS) break;
    }
    Diag(L"TurSrbStatus", rc);
    Diag(L"TurScsiStatus", st);

    {   UCHAR inq[6] = { 0x12, 0, 0, 0, 36, 0 };
        rc = TestIn(e, inq, 6, 36, &st);
        Diag(L"T1_InqSrb", rc); Diag(L"T1_InqOcs", e->LastOcs);
        Diag(L"T1_InqVendor", Be32(d + 8)); Diag(L"T1_InqByte0", Be32(d)); }
    {   UCHAR rcap[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        rc = TestIn(e, rcap, 10, 8, &st);
        Diag(L"T2_CapSrb", rc); Diag(L"T2_CapOcs", e->LastOcs);
        Diag(L"T2_LastLba", Be32(d)); Diag(L"T2_BlockSize", Be32(d + 4));
        if (rc == SRB_STATUS_SUCCESS && Be32(d + 4) && Be32(d + 4) <= 4096) bs = Be32(d + 4); }
    {   UCHAR rd[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };   /* READ(10) LBA 1, 1 block */
        rc = TestIn(e, rd, 10, bs, &st);
        Diag(L"T3_ReadSrb", rc); Diag(L"T3_ReadOcs", e->LastOcs); Diag(L"T3_ReadScsi", st);
        Diag(L"T3_Lba1_0", Be32(d)); Diag(L"T3_Lba1_4", Be32(d + 4));
        if (rc == SRB_STATUS_SUCCESS) {
            /* GPT header checks the way Windows does them */
            ULONG hsz = Le32(d + 12), hcrc = Le32(d + 16), calc;
            ULONG entLba = Le32(d + 72), nEnt = Le32(d + 80), entSz = Le32(d + 84), ecrc = Le32(d + 88);
            Diag(L"G_HeaderSize", hsz);
            Diag(L"G_MyLba", Le32(d + 24));
            Diag(L"G_AltLba", Le32(d + 32));
            Diag(L"G_FirstUsable", Le32(d + 40));
            Diag(L"G_LastUsable", Le32(d + 48));
            Diag(L"G_EntriesLba", entLba);
            Diag(L"G_NumEntries", nEnt);
            Diag(L"G_EntrySize", entSz);
            if (hsz >= 92 && hsz <= bs) {
                d[16] = d[17] = d[18] = d[19] = 0;
                calc = Crc32(d, hsz);
                Diag(L"G_HdrCrcOk", calc == hcrc);
            }
            if (nEnt && entSz && nEnt * entSz <= DATA_SIZE) {
                ULONG bytes = nEnt * entSz, blocks = (bytes + bs - 1) / bs;
                UCHAR rd2[10] = { 0x28, 0, (UCHAR)(entLba >> 24), (UCHAR)(entLba >> 16), (UCHAR)(entLba >> 8), (UCHAR)entLba,
                                  0, (UCHAR)(blocks >> 8), (UCHAR)blocks, 0 };
                rc = TestIn(e, rd2, 10, blocks * bs, &st);
                Diag(L"G_EntReadSrb", rc);
                if (rc == SRB_STATUS_SUCCESS) Diag(L"G_EntCrcOk", Crc32(d, bytes) == ecrc);
            }
        } }

    {   HANDLE h; OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL, StatsThread, e)))
            ZwClose(h); }
    return TRUE;
}

BOOLEAN HwStartIo(PVOID DevExt, PSCSI_REQUEST_BLOCK Srb)
{
    PHW_EXT e = (PHW_EXT)DevExt;
    UCHAR status = SRB_STATUS_INVALID_REQUEST;

    switch (Srb->Function) {
    case SRB_FUNCTION_EXECUTE_SCSI:
        if (Srb->PathId || Srb->TargetId || Srb->Lun) { status = SRB_STATUS_NO_DEVICE; break; }
        if (!e->Ready) { status = SRB_STATUS_BUSY; break; }
        if (Srb->DataTransferLength > MAX_XFER) { status = SRB_STATUS_INVALID_REQUEST; break; }
        {
            SEG segs[129];
            ULONG nsegs = 0, i;
            UCHAR st = 0;
            BOOLEAN toDev = (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != 0;
            if (Srb->DataTransferLength) {
                PSTOR_SCATTER_GATHER_LIST sg = StorPortGetScatterGatherList(e, Srb);
                if (!sg) { status = SRB_STATUS_ERROR; break; }
                for (i = 0; i < sg->NumberOfElements && i < 129; i++) {
                    segs[i].Pa = sg->List[i].PhysicalAddress;
                    segs[i].Len = sg->List[i].Length;
                }
                nsegs = i;
            }
            e->LastOp = Srb->Cdb[0]; e->LastLen = Srb->DataTransferLength; e->LastSegs = nsegs;
            e->LastFlags = Srb->SrbFlags;
            if (nsegs) { e->LastPaLo = segs[0].Pa.LowPart; e->LastPaHi = (ULONG)segs[0].Pa.HighPart; }
            status = UfsExec(e, 0, Srb->Cdb, Srb->CdbLength, Srb->DataTransferLength, toDev,
                             segs, nsegs, (PUCHAR)Srb->SenseInfoBuffer, Srb->SenseInfoBufferLength, &st);
            Srb->ScsiStatus = st;
            e->LastSrb = status;
            if ((status & SRB_STATUS_AUTOSENSE_VALID) == 0 && status != SRB_STATUS_SUCCESS)
                Srb->DataTransferLength = 0;
        }
        break;
    case SRB_FUNCTION_RESET_BUS:
    case SRB_FUNCTION_RESET_DEVICE:
    case SRB_FUNCTION_RESET_LOGICAL_UNIT:
    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
        status = SRB_STATUS_SUCCESS;
        break;
    default:
        break;
    }
    Srb->SrbStatus = status;
    StorPortNotification(RequestComplete, e, Srb);
    return TRUE;
}

BOOLEAN HwInterrupt(PVOID DevExt)
{
    /* interrupts stay disabled (IE = 0); if the line ever fires, mask and ack it */
    PHW_EXT e = (PHW_EXT)DevExt;
    ULONG is;
    if (!e->Hci) return FALSE;
    is = Rd(e, REG_IS);
    if (!is) return FALSE;
    Wr(e, REG_IE, 0);
    Wr(e, REG_IS, is);
    return TRUE;
}
BOOLEAN HwResetBus(PVOID DevExt, ULONG PathId) { UNREFERENCED_PARAMETER(DevExt); UNREFERENCED_PARAMETER(PathId); return TRUE; }

SCSI_ADAPTER_CONTROL_STATUS HwAdapterControl(PVOID DevExt, SCSI_ADAPTER_CONTROL_TYPE Type, PVOID Params)
{
    PHW_EXT e = (PHW_EXT)DevExt;
    if (Type == ScsiQuerySupportedControlTypes) {
        PSCSI_SUPPORTED_CONTROL_TYPE_LIST l = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Params;
        if (l->MaxControlType > ScsiStopAdapter) l->SupportedTypeList[ScsiStopAdapter] = TRUE;
        if (l->MaxControlType > ScsiRestartAdapter) l->SupportedTypeList[ScsiRestartAdapter] = TRUE;
        if (l->MaxControlType > ScsiQuerySupportedControlTypes) l->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
        return ScsiAdapterControlSuccess;
    }
    if (Type == ScsiStopAdapter) { e->Ready = FALSE; return ScsiAdapterControlSuccess; }
    if (Type == ScsiRestartAdapter) { HwInitialize(e); return ScsiAdapterControlSuccess; }
    return ScsiAdapterControlUnsuccessful;
}

ULONG DriverEntry(PVOID DriverObject, PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA d;
    ULONG st = 0, best = 0xFFFFFFFF;
    static const INTERFACE_TYPE types[] = { ACPIBus, Internal, PNPBus };
    int i;

    RtlZeroMemory(&d, sizeof(d));
    d.HwInitializationDataSize = sizeof(d);
    d.HwInitialize = HwInitialize;
    d.HwStartIo = HwStartIo;
    d.HwInterrupt = HwInterrupt;
    d.HwFindAdapter = HwFindAdapter;
    d.HwResetBus = HwResetBus;
    d.HwAdapterControl = HwAdapterControl;
    d.DeviceExtensionSize = sizeof(HW_EXT);
    d.NumberOfAccessRanges = 1;
    d.MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    d.NeedPhysicalAddresses = TRUE;
    d.TaggedQueuing = TRUE;
    d.AutoRequestSense = TRUE;
    d.MultipleRequestPerLu = TRUE;

    Diag(L"DriverEntry", g_RelocAnchor ? 1 : 0);
    for (i = 0; i < 3; i++) {
        d.AdapterInterfaceType = types[i];
        st = StorPortInitialize(DriverObject, RegistryPath, &d, NULL);
        if (st == 0) best = 0;
    }
    Diag(L"StorPortInitialize", best == 0 ? 0 : st);
    return best == 0 ? 0 : st;
}
