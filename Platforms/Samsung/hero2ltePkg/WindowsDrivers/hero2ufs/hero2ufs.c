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
#define OFF_BOUNCE 0x10000          /* all I/O goes through this uncached bounce buffer */
#define UNCACHED_SIZE (OFF_BOUNCE + MAX_XFER)

typedef struct {
    PUCHAR Hci;               /* 0x155A0000 */
    PUCHAR Base;              /* uncached extension VA */
    STOR_PHYSICAL_ADDRESS BasePa;
    UTRD  *Utrd;
    UCD   *Ucd;
    ULONG Commands, Errors;
    ULONG LastOcs, LastScsi, LastIs;
    ULONG LastOp, LastSrb, LastLen, LastSegs, LastPaLo, LastPaHi, LastFlags;
    ULONG ErrLog[8], ErrLba[8], ErrN;
    ULONG BlockSize, LastLba, GptFixups;
    PUCHAR Overlay;            /* primary: hdr + entries, then backup: entries + hdr */
    ULONG EntBlocks;
    BOOLEAN OverlayValid;
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
    Cfg->NumberOfPhysicalBreaks = (MAX_XFER / PAGE_SIZE) + 1;
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
        Diag(L"GptFixups", e->GptFixups);
        { ULONG i; WCHAR n1[] = L"Err0", n2[] = L"ErrInfo0";
          for (i = 0; i < 8 && i < e->ErrN; i++) {
              n1[3] = (WCHAR)(L'0' + i); n2[7] = (WCHAR)(L'0' + i);
              Diag(n1, e->ErrLog[i]); Diag(n2, e->ErrLba[i]); } }
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
        if (rc == SRB_STATUS_SUCCESS && Be32(d + 4) && Be32(d + 4) <= 4096) {
            bs = Be32(d + 4); e->BlockSize = bs; e->LastLba = Be32(d); } }
    {   UCHAR rd[10] = { 0x28, 0, 0, 0, 0, 1, 0, 0, 1, 0 };   /* READ(10) LBA 1, 1 block */
        rc = TestIn(e, rd, 10, bs, &st);
        Diag(L"T3_ReadSrb", rc); Diag(L"T3_ReadOcs", e->LastOcs); Diag(L"T3_ReadScsi", st);
        Diag(L"T3_Lba1_0", Be32(d)); Diag(L"T3_Lba1_4", Be32(d + 4));
        if (rc == SRB_STATUS_SUCCESS && RtlCompareMemory(d, "EFI PART", 8) == 8) {
            UCHAR hdr[512];
            ULONG hsz = Le32(d + 12), hcrc = Le32(d + 16);
            ULONG entLba = Le32(d + 72), nEnt = Le32(d + 80), entSz = Le32(d + 84), ecrc = Le32(d + 88);
            RtlCopyMemory(hdr, d, sizeof(hdr));
            Diag(L"G_HeaderSize", hsz);
            Diag(L"G_AltLba", Le32(d + 32));
            Diag(L"G_EntriesLba", entLba);
            Diag(L"G_NumEntries", nEnt);
            if (hsz >= 92 && hsz <= 512) { d[16] = d[17] = d[18] = d[19] = 0; Diag(L"G_HdrCrcOk", Crc32(d, hsz) == hcrc); }
            if (entLba == 2 && nEnt && entSz == 128 && nEnt * entSz <= DATA_SIZE && bs == 4096 && e->LastLba) {
                ULONG bytes = nEnt * entSz, blocks = (bytes + bs - 1) / bs, k, crc;
                UCHAR rd2[10] = { 0x28, 0, 0, 0, 0, 2, 0, 0, (UCHAR)blocks, 0 };
                rc = TestIn(e, rd2, 10, blocks * bs, &st);
                Diag(L"G_EntReadSrb", rc);
                if (rc == SRB_STATUS_SUCCESS && Crc32(d, bytes) == ecrc &&
                    StorPortAllocatePool(e, (blocks + 1) * bs * 2, 'fUoH', (PVOID *)&e->Overlay) == STOR_STATUS_SUCCESS) {
                    PUCHAR P = e->Overlay, B = e->Overlay + (blocks + 1) * bs, ent = P + bs, h;
                    ULONG hidden = 0;
                    RtlZeroMemory(P, (blocks + 1) * bs * 2);
                    RtlCopyMemory(ent, d, blocks * bs);
                    /* hide every Android partition from the Windows mount manager:
                     * GPT attribute bits 62 (hidden) and 63 (no drive letter) */
                    for (k = 0; k < nEnt; k++) {
                        PUCHAR en = ent + k * entSz;
                        ULONG z = 0, j;
                        for (j = 0; j < 16; j++) z |= en[j];
                        if (!z) continue;
                        /* keep the partitions we created for Windows visible */
                        {
                            static const WCHAR keep1[] = L"ESP", keep2[] = L"WINDOWS";
                            PWCHAR nm = (PWCHAR)(en + 56);
                            ULONG l1 = 0, l2 = 0;
                            while (l1 < 3 && nm[l1] == keep1[l1]) l1++;
                            while (l2 < 7 && nm[l2] == keep2[l2]) l2++;
                            if ((l1 == 3 && nm[3] == 0) || (l2 == 7 && nm[7] == 0)) continue;
                        }
                        en[55] |= 0xC0; hidden++;
                    }
                    crc = Crc32(ent, bytes);
                    /* primary header (LBA 1) */
                    h = P;
                    RtlCopyMemory(h, hdr, 92);
                    h[12] = 92; h[13] = h[14] = h[15] = 0;
                    h[88] = (UCHAR)crc; h[89] = (UCHAR)(crc >> 8); h[90] = (UCHAR)(crc >> 16); h[91] = (UCHAR)(crc >> 24);
                    h[16] = h[17] = h[18] = h[19] = 0;
                    k = Crc32(h, 92); h[16] = (UCHAR)k; h[17] = (UCHAR)(k >> 8); h[18] = (UCHAR)(k >> 16); h[19] = (UCHAR)(k >> 24);
                    /* backup: entries at LastLba-blocks, header at LastLba */
                    RtlCopyMemory(B, ent, blocks * bs);
                    h = B + blocks * bs;
                    RtlCopyMemory(h, P, 92);
                    {   ULONGLONG my = e->LastLba, alt = 1, el = e->LastLba - blocks; ULONG j;
                        for (j = 0; j < 8; j++) { h[24 + j] = (UCHAR)(my >> (8 * j)); h[32 + j] = (UCHAR)(alt >> (8 * j)); h[72 + j] = (UCHAR)(el >> (8 * j)); } }
                    h[16] = h[17] = h[18] = h[19] = 0;
                    k = Crc32(h, 92); h[16] = (UCHAR)k; h[17] = (UCHAR)(k >> 8); h[18] = (UCHAR)(k >> 16); h[19] = (UCHAR)(k >> 24);
                    e->EntBlocks = blocks;
                    e->OverlayValid = TRUE;
                    Diag(L"G_Overlay", 1);
                    Diag(L"G_HiddenParts", hidden);
                }
            }
        } }

    {   HANDLE h; OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        if (NT_SUCCESS(PsCreateSystemThread(&h, THREAD_ALL_ACCESS, &oa, NULL, NULL, StatsThread, e)))
            ZwClose(h); }
    return TRUE;
}

/*
 * Samsung writes GPT headers with HeaderSize = 512. UEFI and Linux accept it,
 * Windows only accepts 92. Patch the copy that goes up the stack (primary at
 * LBA 1 and backup at the last LBA): HeaderSize = 92, header CRC recomputed.
 * The disk itself is not modified.
 */
static void GptFixup(PHW_EXT e, const UCHAR *c, PUCHAR buf, ULONG len, BOOLEAN write)
{
    ULONGLONG lba, blk, pStart, pEnd, bStart, bEnd;
    ULONG cnt, bs = 4096;

    if (c[0] == 0x28 || c[0] == 0x2A) { lba = Be32((PUCHAR)c + 2); cnt = ((ULONG)c[7] << 8) | c[8]; }
    else if (c[0] == 0x88 || c[0] == 0x8A) { lba = ((ULONGLONG)Be32((PUCHAR)c + 2) << 32) | Be32((PUCHAR)c + 6); cnt = Be32((PUCHAR)c + 10); }
    else return;
    if (!e->OverlayValid) return;

    pStart = 1; pEnd = 1 + e->EntBlocks;                         /* inclusive */
    bStart = e->LastLba - e->EntBlocks; bEnd = e->LastLba;

    if (write) {
        if (!(lba + cnt - 1 < pStart || lba > pEnd) || !(lba + cnt - 1 < bStart || lba > bEnd))
            e->OverlayValid = FALSE;                             /* Windows rewrote the GPT */
        return;
    }
    for (blk = lba; blk < lba + cnt; blk++) {
        PUCHAR src;
        if ((blk - lba + 1) * bs > len) break;
        if (blk >= pStart && blk <= pEnd) src = e->Overlay + (blk - pStart) * bs;
        else if (blk >= bStart && blk <= bEnd) src = e->Overlay + (e->EntBlocks + 1) * bs + (blk - bStart) * bs;
        else continue;
        RtlCopyMemory(buf + (blk - lba) * bs, src, bs);
        e->GptFixups++;
    }
}

static void LogError(PHW_EXT e, PSCSI_REQUEST_BLOCK Srb, UCHAR status)
{
    ULONG i = e->ErrN++ % 8;
    PUCHAR c = Srb->Cdb;
    PUCHAR sn = (PUCHAR)Srb->SenseInfoBuffer;
    ULONG key = (sn && (status & SRB_STATUS_AUTOSENSE_VALID)) ? ((ULONG)(sn[2] & 0xF) << 16 | (ULONG)sn[12] << 8 | sn[13]) : 0;
    e->ErrLog[i] = (ULONG)c[0] | (ULONG)(status & 0x3F) << 8 | (ULONG)e->LastOcs << 14 | (ULONG)Srb->ScsiStatus << 18 | (ULONG)c[2] << 26;
    e->ErrLba[i] = key ? key : (c[0] == 0x28 ? Be32(c + 2) : ((ULONG)c[1] << 8 | c[2]));
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
            SEG seg;
            UCHAR st = 0;
            ULONG len = Srb->DataTransferLength;
            BOOLEAN toDev = (Srb->SrbFlags & SRB_FLAGS_DATA_OUT) != 0;
            PUCHAR va = NULL, bounce = e->Base + OFF_BOUNCE;
            if (len) {
                if (StorPortGetSystemAddress(e, Srb, (PVOID *)&va) != STOR_STATUS_SUCCESS || !va) {
                    status = SRB_STATUS_ERROR; break;
                }
                if (toDev) RtlCopyMemory(bounce, va, len);
                else if (len <= 4096) RtlZeroMemory(bounce, len);
            }
            seg.Pa.QuadPart = e->BasePa.QuadPart + OFF_BOUNCE;
            seg.Len = len;
            e->LastOp = Srb->Cdb[0]; e->LastLen = len; e->LastSegs = len ? 1 : 0;
            e->LastFlags = Srb->SrbFlags;
            status = UfsExec(e, 0, Srb->Cdb, Srb->CdbLength, len, toDev,
                             &seg, len ? 1 : 0, (PUCHAR)Srb->SenseInfoBuffer, Srb->SenseInfoBufferLength, &st);
            Srb->ScsiStatus = st;
            if (status == SRB_STATUS_ERROR && Srb->Cdb[0] != 0x28 && Srb->Cdb[0] != 0x2A &&
                Srb->Cdb[0] != 0x88 && Srb->Cdb[0] != 0x8A && Srb->SenseInfoBuffer &&
                Srb->SenseInfoBufferLength >= 14) {
                /* the host rejected the UPIU (OCS error) for a command the device does
                 * not implement (e.g. GET LBA STATUS): report ILLEGAL REQUEST /
                 * INVALID COMMAND OPERATION CODE so Windows stops retrying it */
                PUCHAR sn = (PUCHAR)Srb->SenseInfoBuffer;
                RtlZeroMemory(sn, Srb->SenseInfoBufferLength);
                sn[0] = 0x70; sn[2] = 0x05; sn[7] = 10; sn[12] = 0x20; sn[13] = 0x00;
                Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
                status = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
            }
            e->LastSrb = status;
            if (status == SRB_STATUS_SUCCESS && toDev) GptFixup(e, Srb->Cdb, NULL, len, TRUE);
            if (status == SRB_STATUS_SUCCESS && !toDev && len) {
                if (Srb->Cdb[0] == 0x25 && len >= 8) { e->LastLba = Be32(bounce); e->BlockSize = Be32(bounce + 4); }
                GptFixup(e, Srb->Cdb, bounce, len, FALSE);
                RtlCopyMemory(va, bounce, len);
            }
            if (status != SRB_STATUS_SUCCESS) LogError(e, Srb, status);
            if ((status & SRB_STATUS_AUTOSENSE_VALID) == 0 && status != SRB_STATUS_SUCCESS)
                Srb->DataTransferLength = 0;
        }
        break;
    case SRB_FUNCTION_RESET_BUS:
    case SRB_FUNCTION_RESET_DEVICE:
    case SRB_FUNCTION_RESET_LOGICAL_UNIT:
    case SRB_FUNCTION_FLUSH:
    case SRB_FUNCTION_SHUTDOWN:
    case SRB_FUNCTION_POWER:          /* D-state changes: the link stays up, nothing to do */
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
