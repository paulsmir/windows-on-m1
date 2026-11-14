/* SPDX-License-Identifier: MIT */
/* Storport shim harness: the real miniport sources (driver.c, native.c, direct.c) run on the
 * host against a fake Storport and a simulated ANS2 controller with a small RAM disk.
 * Invariants (EXP2447: an optional pool failed FindAdapter of the boot disk):
 *   - no failure of a bounce-pool resource fails FindAdapter, HwInitialize or passive init;
 *   - FindAdapter asks Storport for exactly the 0.4.8.3 uncached extension;
 *   - without a pool a misaligned request is passed through as in 0.4.8.3, and no path uses
 *     a pool buffer; a partial pool works;
 *   - a bounced request round-trips its data through the driver's own copy code.
 * HARNESS_OLD builds the interface-only subset against an older tree (the RED run). */
#include "../adapter.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

sp_DRIVER_INITIALIZE DriverEntry;
#define EXT_083_BYTES 385024u /* 6 x 16 KiB queues + 66 metadata pages + 16 KiB slack */
#define DISK_BLOCKS 256u
#define PAGE 4096u

/* ---- fake physical memory ------------------------------------------------------------ */
struct region { uint8_t *va; size_t len; uint64_t ipa; int contiguous_alloc; };
static struct region regions[128];
static unsigned nregions;
static void region_add(void *va, size_t len, uint64_t ipa, int ca)
{
    assert(nregions < 128);
    regions[nregions++] = (struct region){va, len, ipa, ca};
}
static void region_del(void *va)
{
    for (unsigned i = 0; i < nregions; i++)
        if (regions[i].va == va) { regions[i] = regions[--nregions]; return; }
    assert(!"freeing unknown region");
}
static struct region *region_of(const void *va)
{
    for (unsigned i = 0; i < nregions; i++)
        if ((const uint8_t *)va >= regions[i].va && (const uint8_t *)va < regions[i].va + regions[i].len)
            return &regions[i];
    return NULL;
}
static uint8_t *pa_to_va(uint64_t pa, size_t bytes)
{
    /* Every test address lies in identity guest RAM, so controller PA == IPA. */
    for (unsigned i = 0; i < nregions; i++)
        if (pa >= regions[i].ipa && pa + bytes <= regions[i].ipa + regions[i].len)
            return regions[i].va + (pa - regions[i].ipa);
    return NULL;
}

/* ---- knobs and observations ---------------------------------------------------------- */
enum phase { PH_NONE, PH_FIND, PH_INIT, PH_PASSIVE, PH_IO };
static struct {
    /* faults */
    ULONG uncached_limit;          /* larger requests return NULL (0: none) */
    uint64_t uncached_ipa;
    ULONG uncached_phys_fail_from; /* extension offsets >= this have no physical address */
    ULONG alloc_fail_mask;         /* bit n: n-th contiguous allocation fails */
    ULONG alloc_phys_fail_mask;    /* bit n: its pages have no physical address */
    uint64_t alloc_ipa[32];        /* placement override for the n-th allocation */
    int sysaddr_fail, sysaddr_wrong;
    int hold;                      /* simulated controller keeps commands outstanding */
    /* observations */
    enum phase phase;
    int lock_held;
    ULONG uncached_calls, uncached_bytes, allocs, alloc_ok, frees, device_busy, completed;
    ULONG log_ids[256], nlogs;
    uint64_t io_prp1[64]; unsigned nio; /* PRP1 of every READ/WRITE the controller executed */
    unsigned held_tags[64], nheld;
} F;
static HW_INITIALIZATION_DATA init_data;
static HW_PASSIVE_INITIALIZE_ROUTINE *passive_routine;
static void *adapter_mem, *ctrl_base, *mbox_base, *uncached_mem;
static uint8_t disk[DISK_BLOCKS * PAGE];
#define A ((PAPPLE_ANS_ADAPTER)adapter_mem)

/* ---- simulated ANS2 controller (register model as common/tests/test_direct.c) --------- */
static struct { ans_u32 cc, csts; unsigned cq_tail[2], cq_phase[2]; ans_u64 send0; } S;
static ans_u64 le(const ans_u8 *p, unsigned n) { ans_u64 v = 0; while (n--) v = (v << 8) | p[n]; return v; }
static void sim_post(unsigned q, unsigned tag, ans_u32 result)
{
    struct ansd_queue *qq = &A->Ansd.q[q];
    volatile struct ansd_cqe *e = &qq->cq[S.cq_tail[q]];
    e->result = result;
    e->tail = tag | ((ans_u32)S.cq_phase[q] << 16);
    if (++S.cq_tail[q] == qq->depth) { S.cq_tail[q] = 0; S.cq_phase[q] ^= 1; }
}
static void sim_data(const ans_u8 *sqe, int write)
{
    uint64_t prp1 = le(sqe + 24, 8), prp2 = le(sqe + 32, 8), lba = le(sqe + 40, 8);
    size_t bytes = ((size_t)le(sqe + 48, 2) + 1) * PAGE, off = prp1 & (PAGE - 1), done = 0;
    unsigned entries = (unsigned)((off + bytes + PAGE - 1) / PAGE);
    assert(lba * PAGE + bytes <= sizeof(disk));
    assert(F.nio < 64); F.io_prp1[F.nio++] = prp1;
    for (unsigned i = 0; i < entries; i++) {
        uint64_t pa = prp1;
        if (i) {
            if (entries == 2) pa = prp2;
            else { uint8_t *list = pa_to_va(prp2, PAGE); assert(list); pa = le(list + (i - 1) * 8, 8); }
            assert(!(pa & (PAGE - 1)));
        }
        size_t n = PAGE - (pa & (PAGE - 1));
        if (n > bytes - done) n = bytes - done;
        uint8_t *va = pa_to_va(pa, n);
        assert(va && "controller was given an address that is not guest memory");
        if (write) memcpy(disk + lba * PAGE + done, va, n); else memcpy(va, disk + lba * PAGE + done, n);
        done += n;
    }
    assert(done == bytes);
}
static void sim_doorbell(unsigned q, unsigned tag)
{
    const ans_u8 *sqe = A->Ansd.q[q].sq + tag * 64u;
    ans_u32 result = 0;
    assert(sqe[2] == tag && A->Ansd.q[q].tcb[tag].command_id == tag);
    if (q == 0) {
        if (sqe[0] == 6) {
            uint8_t *page = pa_to_va(le(sqe + 24, 8), PAGE);
            assert(page);
            memset(page, 0, PAGE);
            if ((sqe[40] & 0xff) == 1) { /* controller */
                memcpy(page + 24, "APPLE SSD SHIM  ", 16); memcpy(page + 64, "SHIM0001", 8);
                page[516] = 1; page[520] = 0x04; page[525] = 1;
            } else { /* namespace: 4 KiB LBA format 0 */
                page[0] = (uint8_t)DISK_BLOCKS; page[1] = (uint8_t)(DISK_BLOCKS >> 8);
                page[8] = page[0]; page[9] = page[1]; page[16] = page[0]; page[17] = page[1];
                page[130] = 12;
            }
        } else if (sqe[0] == 0x0a) result = 1;
    } else if (sqe[0] == 1 || sqe[0] == 2) sim_data(sqe, sqe[0] == 1);
    if (q == 1 && F.hold) { assert(F.nheld < 64); F.held_tags[F.nheld++] = tag; return; }
    sim_post(q, tag, result);
}
ULONG StorPortReadRegisterUlong(PVOID e, PULONG r)
{
    (void)e;
    if ((uint8_t *)r >= (uint8_t *)mbox_base && (uint8_t *)r < (uint8_t *)mbox_base + ANSD_MBOX_SIZE) {
        ans_u32 off = (ans_u32)((uint8_t *)r - (uint8_t *)mbox_base);
        return off == ANSD_MBOX_I2A_CONTROL ? ANSD_MBOX_EMPTY : 0;
    }
    switch ((ans_u32)((uint8_t *)r - (uint8_t *)ctrl_base)) {
    case ANSD_CC: return S.cc;
    case ANSD_CSTS: if (S.cc & ANSD_CC_EN) S.csts |= ANSD_CSTS_RDY; return S.csts;
    case ANSD_BOOT_STATUS: return ANSD_BOOT_STATUS_OK;
    case ANSD_NVMMU_TCB_STAT: return 0;
    }
    assert(!"unexpected read32"); return 0;
}
ULONG64 StorPortReadRegisterUlong64(PVOID e, PULONG64 r)
{
    (void)e;
    assert((uint8_t *)r == (uint8_t *)ctrl_base + ANSD_CAP);
    return 63ULL | (20ULL << 24);
}
VOID StorPortWriteRegisterUlong(PVOID e, PULONG r, ULONG v)
{
    (void)e;
    switch ((ans_u32)((uint8_t *)r - (uint8_t *)ctrl_base)) {
    case ANSD_CC:
        S.cc = v;
        if (!(v & ANSD_CC_EN)) { S.csts &= ~(ANSD_CSTS_RDY | ANSD_CSTS_SHST_MASK); S.cq_tail[0] = S.cq_tail[1] = 0; S.cq_phase[0] = S.cq_phase[1] = 1; }
        if ((v & ANSD_CC_SHN_MASK) == ANSD_CC_SHN_NORMAL) S.csts = (S.csts & ~ANSD_CSTS_SHST_MASK) | ANSD_CSTS_SHST_DONE;
        return;
    case ANSD_AQA: case ANSD_LINEAR_SQ_CTRL: case ANSD_MAX_PEND: case ANSD_NVMMU_NUM:
    case ANSD_DB_ACQ: case ANSD_DB_IOCQ: case ANSD_NVMMU_TCB_INVAL: return;
    case ANSD_DB_LINEAR_ASQ: sim_doorbell(0, v); return;
    case ANSD_DB_LINEAR_IOSQ: sim_doorbell(1, v); return;
    }
    assert(!"unexpected write32");
}
VOID StorPortWriteRegisterUlong64(PVOID e, PULONG64 r, ULONG64 v)
{
    (void)e; (void)v;
    if ((uint8_t *)r >= (uint8_t *)mbox_base && (uint8_t *)r < (uint8_t *)mbox_base + ANSD_MBOX_SIZE) return;
    switch ((ans_u32)((uint8_t *)r - (uint8_t *)ctrl_base)) {
    case ANSD_ASQ: case ANSD_ACQ: case ANSD_NVMMU_ASQ_TCB: case ANSD_NVMMU_IOSQ_TCB: return;
    }
    assert(!"unexpected write64");
}

/* ---- fake Storport --------------------------------------------------------------------- */
void shim_yield(void) { assert(!"terminal containment reached"); abort(); }
LONG InterlockedIncrement(volatile LONG *v) { return ++*v; }
LONG InterlockedExchange(volatile LONG *v, LONG n) { LONG o = *v; *v = n; return o; }
PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID va)
{
    PHYSICAL_ADDRESS p = {.QuadPart = 0};
    struct region *r = region_of(va);
    if (r) p.QuadPart = (int64_t)(r->ipa + (uint64_t)((uint8_t *)va - r->va));
    return p;
}
ULONG StorPortInitialize(PVOID a, PVOID b, PHW_INITIALIZATION_DATA d, PVOID c)
{
    (void)a; (void)b; (void)c; init_data = *d; return 0;
}
PVOID StorPortGetDeviceBase(PVOID e, INTERFACE_TYPE t, ULONG bus, STOR_PHYSICAL_ADDRESS addr, ULONG len, BOOLEAN io)
{
    (void)e; (void)t; (void)bus; (void)len; (void)io;
    assert(F.phase == PH_FIND);
    return (uint64_t)addr.QuadPart == ANSD_CTRL_BASE ? ctrl_base : mbox_base;
}
VOID StorPortFreeDeviceBase(PVOID e, PVOID p) { (void)e; (void)p; }
PVOID StorPortGetUncachedExtension(PVOID e, PPORT_CONFIGURATION_INFORMATION c, ULONG bytes)
{
    (void)e; (void)c;
    assert(F.phase == PH_FIND && "uncached extension is requested only in FindAdapter");
    F.uncached_calls++; F.uncached_bytes = bytes;
    if (F.uncached_limit && bytes > F.uncached_limit) return NULL;
    /* Virtual and guest-physical addresses congruent modulo 16 KiB, as on the machine. */
    uncached_mem = aligned_alloc(16384, (bytes + 16383) & ~(size_t)16383);
    assert(uncached_mem);
    memset(uncached_mem, 0xa5, bytes);
    region_add(uncached_mem, bytes, F.uncached_ipa, 0);
    return uncached_mem;
}
STOR_PHYSICAL_ADDRESS StorPortGetPhysicalAddress(PVOID e, PSCSI_REQUEST_BLOCK srb, PVOID va, ULONG *length)
{
    STOR_PHYSICAL_ADDRESS p = {.QuadPart = 0};
    struct region *r = region_of(va);
    (void)e; (void)srb;
    *length = 0;
    if (!r) return p;
    size_t off = (size_t)((uint8_t *)va - r->va);
    if (r->va == uncached_mem && F.uncached_phys_fail_from && off >= F.uncached_phys_fail_from) return p;
    if (r->contiguous_alloc) {
        if (r->contiguous_alloc < 0) return p;
        /* Documented: for this memory the returned *Length is to be ignored. Return garbage. */
        *length = 1;
    } else *length = (ULONG)(r->len - off);
    p.QuadPart = (int64_t)(r->ipa + off);
    return p;
}
ULONG StorPortAllocateContiguousMemorySpecifyCacheNode(PVOID e, SIZE_T bytes, PHYSICAL_ADDRESS lo,
    PHYSICAL_ADDRESS hi, PHYSICAL_ADDRESS boundary, MEMORY_CACHING_TYPE cache, NODE_REQUIREMENT node, PVOID *out)
{
    (void)e; (void)lo; (void)hi; (void)boundary; (void)node;
    assert(F.phase == PH_PASSIVE && !F.lock_held && "pool memory is allocated in passive initialization");
    assert(cache == MmCached && bytes && !(bytes & (PAGE - 1)));
    unsigned n = F.allocs++;
    *out = NULL;
    if (n < 32 && (F.alloc_fail_mask & (1u << n))) return STOR_STATUS_INSUFFICIENT_RESOURCES;
    void *va = aligned_alloc(PAGE, bytes);
    assert(va);
    memset(va, 0xc3, bytes);
    uint64_t ipa = (n < 32 && F.alloc_ipa[n]) ? F.alloc_ipa[n] : 0x840000000ULL + (uint64_t)n * 0x100000u;
    region_add(va, bytes, ipa, (n < 32 && (F.alloc_phys_fail_mask & (1u << n))) ? -1 : 1);
    F.alloc_ok++;
    *out = va;
    return STOR_STATUS_SUCCESS;
}
ULONG StorPortFreeContiguousMemorySpecifyCache(PVOID e, PVOID va, SIZE_T bytes, MEMORY_CACHING_TYPE cache)
{
    (void)e;
    struct region *r = region_of(va);
    assert(r && r->va == va && r->len == bytes && r->contiguous_alloc && cache == MmCached);
    region_del(va); free(va); F.frees++;
    return STOR_STATUS_SUCCESS;
}
VOID StorPortStallExecution(ULONG us) { (void)us; }
VOID StorPortAcquireSpinLock(PVOID e, STOR_SPINLOCK l, PVOID c, PSTOR_LOCK_HANDLE h)
{
    (void)e; (void)c; (void)h;
    assert(l == InterruptLock && !F.lock_held && "InterruptLock acquired recursively");
    F.lock_held = 1;
}
VOID StorPortReleaseSpinLock(PVOID e, PSTOR_LOCK_HANDLE h) { (void)e; (void)h; assert(F.lock_held); F.lock_held = 0; }
VOID StorPortNotification(SCSI_NOTIFICATION_TYPE t, PVOID e, ...)
{
    (void)e;
    if (t == RequestComplete) {
        va_list ap; va_start(ap, e);
        PSCSI_REQUEST_BLOCK srb = va_arg(ap, PSCSI_REQUEST_BLOCK);
        va_end(ap);
        assert(srb->SrbStatus != SRB_STATUS_PENDING);
        F.completed++;
    }
}
VOID StorPortLogError(PVOID e, PSCSI_REQUEST_BLOCK s, UCHAR p, UCHAR t, UCHAR l, ULONG code, ULONG id)
{
    (void)e; (void)s; (void)p; (void)t; (void)l; (void)code;
    if (F.nlogs < 256) F.log_ids[F.nlogs++] = id;
}
BOOLEAN StorPortIssueDpc(PVOID e, PSTOR_DPC d, PVOID a, PVOID b) { (void)e; (void)d; (void)a; (void)b; return TRUE; }
VOID StorPortInitializeDpc(PVOID e, PSTOR_DPC d, HW_DPC_ROUTINE *r) { (void)e; d->routine = (PVOID)(uintptr_t)r; }
BOOLEAN StorPortEnablePassiveInitialization(PVOID e, HW_PASSIVE_INITIALIZE_ROUTINE *r)
{
    (void)e; assert(F.phase == PH_INIT); passive_routine = r; return TRUE;
}
ULONG StorPortQueryPerformanceCounter(PVOID e, PLARGE_INTEGER f, PLARGE_INTEGER c)
{
    static int64_t now;
    (void)e; if (f) f->QuadPart = 24000000; if (c) c->QuadPart = (now += 100);
    return STOR_STATUS_SUCCESS;
}
struct req { SCSI_REQUEST_BLOCK srb; APPLE_ANS_SRB ext; STOR_SCATTER_GATHER_LIST sg; uint8_t *page_base, *data; };
PSTOR_SCATTER_GATHER_LIST StorPortGetScatterGatherList(PVOID e, PSCSI_REQUEST_BLOCK srb)
{
    (void)e; return &((struct req *)srb)->sg;
}
ULONG StorPortGetSystemAddress(PVOID e, PSCSI_REQUEST_BLOCK srb, PVOID *va)
{
    (void)e;
    assert(F.phase == PH_IO && !F.lock_held);
    if (F.sysaddr_fail) { *va = NULL; return STOR_STATUS_INSUFFICIENT_RESOURCES; }
    *va = F.sysaddr_wrong ? (PVOID)((struct req *)srb)->page_base : (PVOID)((struct req *)srb)->data;
    return STOR_STATUS_SUCCESS;
}
BOOLEAN StorPortDeviceBusy(PVOID e, UCHAR p, UCHAR t, UCHAR l, ULONG n) { (void)e; (void)p; (void)t; (void)l; (void)n; F.device_busy++; return TRUE; }
ULONG StorPortMarkDumpMemory(PVOID e, PVOID a, ULONG b, ULONG c) { (void)e; (void)a; (void)b; (void)c; return 0; }
PUCHAR StorPortAllocateRegistryBuffer(PVOID e, PULONG l) { (void)e; (void)l; return NULL; }
VOID StorPortFreeRegistryBuffer(PVOID e, PUCHAR b) { (void)e; (void)b; }
BOOLEAN StorPortRegistryRead(PVOID e, PUCHAR n, ULONG g, ULONG t, PUCHAR b, PULONG l) { (void)e; (void)n; (void)g; (void)t; (void)b; (void)l; return FALSE; }

/* ---- port-driver side of the harness --------------------------------------------------- */
static void world_reset(void)
{
    for (unsigned i = 0; i < nregions; i++) free(regions[i].va);
    nregions = 0;
    free(adapter_mem); free(ctrl_base); free(mbox_base);
    memset(&F, 0, sizeof(F)); memset(&S, 0, sizeof(S)); memset(disk, 0, sizeof(disk));
    S.cq_phase[0] = S.cq_phase[1] = 1;
    F.uncached_ipa = 0x820000000ULL;
    uncached_mem = NULL; passive_routine = NULL;
    assert(DriverEntry(NULL, NULL) == 0);
    adapter_mem = calloc(1, init_data.DeviceExtensionSize);
    ctrl_base = malloc(ANSD_CTRL_SIZE); mbox_base = malloc(ANSD_MBOX_SIZE);
    assert(adapter_mem && ctrl_base && mbox_base);
    assert(init_data.SrbExtensionSize == sizeof(APPLE_ANS_SRB));
}
static ULONG port_find(void)
{
    static ACCESS_RANGE ranges[2];
    PORT_CONFIGURATION_INFORMATION c = {0};
    BOOLEAN again = FALSE;
    ranges[0] = (ACCESS_RANGE){{.QuadPart = (int64_t)ANSD_CTRL_BASE}, ANSD_CTRL_SIZE, TRUE};
    ranges[1] = (ACCESS_RANGE){{.QuadPart = (int64_t)ANSD_MBOX_BASE}, ANSD_MBOX_SIZE, TRUE};
    c.Length = sizeof(c); c.NumberOfAccessRanges = 2; c.AccessRanges = (ACCESS_RANGE (*)[])&ranges;
    c.BusInterruptVector = 960; c.BusInterruptLevel = 960; c.InterruptMode = LevelSensitive;
    F.phase = PH_FIND;
    ULONG r = ((HW_FIND_ADAPTER *)init_data.HwFindAdapter)(adapter_mem, NULL, NULL, NULL, &c, &again);
    F.phase = PH_NONE;
    return r;
}
/* FindAdapter -> HwInitialize (interrupt lock held, as Storport calls it) -> passive init. */
static int port_start(void)
{
    ULONG found = port_find();
    if (found != SP_RETURN_FOUND) { fprintf(stderr, "FindAdapter returned %u (stage %u)\n", found, A->Stage); return 0; }
    F.phase = PH_INIT; F.lock_held = 1;
    BOOLEAN ok = init_data.HwInitialize(adapter_mem);
    F.lock_held = 0;
    if (!ok) { fprintf(stderr, "HwInitialize failed (stage %u, engine stage %u)\n", A->FailureStage, A->Ansd.stage); return 0; }
    assert(passive_routine);
    F.phase = PH_PASSIVE;
    ok = passive_routine(adapter_mem);
    F.phase = PH_IO;
    return ok;
}
static void port_interrupt(void)
{
    assert(!F.lock_held);
    F.lock_held = 1; (void)init_data.HwInterrupt(adapter_mem); F.lock_held = 0;
}
static uint64_t next_data_ipa = 0x900000000ULL;
/* A READ(10)/WRITE(10) of `pages` blocks whose buffer starts `offset` bytes into a page. */
static struct req *req_new(int write, unsigned lba, unsigned pages, unsigned offset)
{
    struct req *r = calloc(1, sizeof(*r));
    size_t bytes = (size_t)pages * PAGE, span = (offset + bytes + PAGE - 1) & ~(size_t)(PAGE - 1);
    assert(r);
    r->page_base = aligned_alloc(PAGE, span); assert(r->page_base);
    memset(r->page_base, 0xee, span);
    r->data = r->page_base + offset;
    region_add(r->page_base, span, next_data_ipa, 0);
    r->sg.NumberOfElements = 1;
    r->sg.List[0].PhysicalAddress.QuadPart = (int64_t)(next_data_ipa + offset);
    r->sg.List[0].Length = (ULONG)bytes;
    next_data_ipa += span + PAGE;
    r->srb.Function = SRB_FUNCTION_EXECUTE_SCSI; r->srb.CdbLength = 10;
    r->srb.Cdb[0] = write ? 0x2a : 0x28;
    r->srb.Cdb[4] = (UCHAR)(lba >> 8); r->srb.Cdb[5] = (UCHAR)lba; r->srb.Cdb[8] = (UCHAR)pages;
    r->srb.SrbFlags = (write ? SRB_FLAGS_DATA_OUT : SRB_FLAGS_DATA_IN) | SRB_FLAGS_QUEUE_ACTION_ENABLE;
    r->srb.QueueAction = SRB_SIMPLE_TAG_REQUEST;
    r->srb.DataTransferLength = (ULONG)bytes; r->srb.DataBuffer = r->data;
    r->srb.SrbExtension = &r->ext;
    return r;
}
static void req_free(struct req *r) { region_del(r->page_base); free(r->page_base); free(r); }
static void port_submit(struct req *r)
{
    r->srb.SrbStatus = SRB_STATUS_PENDING;
    r->srb.DataTransferLength = r->sg.List[0].Length;
    assert(init_data.HwBuildIo(adapter_mem, &r->srb));
    assert(init_data.HwStartIo(adapter_mem, &r->srb));
}
static void pattern(uint8_t *p, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed * 131u + i * 7u + (i >> 8));
}
static int in_pool(uint64_t pa)
{
    for (unsigned i = 0; i < nregions; i++)
        if (regions[i].contiguous_alloc && pa >= regions[i].ipa && pa < regions[i].ipa + regions[i].len) return 1;
    return 0;
}
/* One write and one read-back of the same blocks through a buffer at `offset` in a page. */
static void round_trip(unsigned lba, unsigned pages, unsigned offset, int expect_bounce, unsigned seed)
{
    size_t bytes = (size_t)pages * PAGE;
    struct req *w = req_new(1, lba, pages, offset), *r = req_new(0, lba, pages, offset);
    uint8_t *want = malloc(bytes);
    assert(want);
    pattern(want, bytes, seed); memcpy(w->data, want, bytes);
    unsigned first = F.nio;
    port_submit(w); port_interrupt();
    assert(w->srb.SrbStatus == SRB_STATUS_SUCCESS && w->srb.DataTransferLength == bytes);
    assert(!memcmp(disk + (size_t)lba * PAGE, want, bytes));
    port_submit(r); port_interrupt();
    assert(r->srb.SrbStatus == SRB_STATUS_SUCCESS && r->srb.DataTransferLength == bytes);
    assert(!memcmp(r->data, want, bytes));
    assert(r->page_base[offset + bytes - 1] == want[bytes - 1]);
    if (offset) assert(r->page_base[offset - 1] == 0xee); /* nothing written before the buffer */
    assert(F.nio == first + 2);
    for (unsigned i = first; i < first + 2; i++) {
        if (expect_bounce) assert(in_pool(F.io_prp1[i]) && !(F.io_prp1[i] & (PAGE - 1)));
        else assert(!in_pool(F.io_prp1[i]) &&
                    F.io_prp1[i] == (uint64_t)(i == first ? w : r)->sg.List[0].PhysicalAddress.QuadPart);
    }
    free(want); req_free(w); req_free(r);
}

/* ---- tests ----------------------------------------------------------------------------- */
/* RED on 0.4.8.5: each of these made FindAdapter return SP_RETURN_ERROR. */
static void test_pool_failure_never_fails_boot(int only)
{
    /* 1. Storport refuses any uncached extension larger than the 0.4.8.3 one, and every
     *    contiguous allocation. */
    if (!only || only == 1) {
        world_reset();
        F.uncached_limit = EXT_083_BYTES; F.alloc_fail_mask = ~0u;
        assert(port_start() && "boot must survive an unavailable bounce pool");
        round_trip(3, 2, 0x78, 0, 1);   /* misaligned: passed through as in 0.4.8.3 */
        round_trip(9, 4, 0, 0, 2);
    }
    /* 2. Bytes past the 0.4.8.3 size have no physical address. */
    if (!only || only == 2) {
        world_reset();
        F.uncached_phys_fail_from = EXT_083_BYTES; F.alloc_fail_mask = ~0u;
        assert(port_start() && "boot must survive unresolvable pool pages");
    }
    /* 3. The 0.4.8.3 extension fits below the alias backing; anything larger crosses into it. */
    if (!only || only == 3) {
        world_reset();
        F.uncached_ipa = ANSD_LOW_PA - 0x60000u; F.alloc_fail_mask = ~0u;
        assert(port_start() && "boot must survive pool pages in the alias backing");
        round_trip(1, 1, 0xfc0, 0, 3);
    }
    /* 4. ... and at the end of guest RAM. */
    if (!only || only == 4) {
        world_reset();
        F.uncached_ipa = ANSD_RAM_END - 0x60000u; F.alloc_fail_mask = ~0u;
        assert(port_start() && "boot must survive pool pages past the DMA window");
    }
}
#ifndef HARNESS_OLD
static void sim_release_held(void)
{
    for (unsigned i = 0; i < F.nheld; i++) sim_post(1, F.held_tags[i], 0);
    F.nheld = 0;
}
static struct { ans_u8 b[ANS_BOUNCE_IOCTL_TOTAL]; } receipt_buf;
static const ans_u8 *receipt(void)
{
    SCSI_REQUEST_BLOCK srb = {0};
    ans_u8 *b = receipt_buf.b;
    memset(b, 0, sizeof(receipt_buf.b));
    b[0] = 28; memcpy(b + 4, "APANS001", 8); b[12] = 10;
    b[16] = (ans_u8)ANS_BOUNCE_IOCTL_QUERY; b[17] = (ans_u8)(ANS_BOUNCE_IOCTL_QUERY >> 8);
    b[18] = (ans_u8)(ANS_BOUNCE_IOCTL_QUERY >> 16); b[19] = (ans_u8)(ANS_BOUNCE_IOCTL_QUERY >> 24);
    b[24] = (ans_u8)ANS_BOUNCE_IOCTL_PAYLOAD;
    b[28] = ANS_BOUNCE_IOCTL_VERSION; b[32] = (ans_u8)ANS_BOUNCE_IOCTL_PAYLOAD;
    srb.Function = SRB_FUNCTION_IO_CONTROL; srb.DataBuffer = b; srb.DataTransferLength = sizeof(receipt_buf.b);
    assert(init_data.HwBuildIo(adapter_mem, &srb) && init_data.HwStartIo(adapter_mem, &srb));
    assert(srb.SrbStatus == SRB_STATUS_SUCCESS);
    return b + 28;
}
static int logged(ULONG code)
{
    for (unsigned i = 0; i < F.nlogs; i++) if ((F.log_ids[i] >> 24) == code) return (int)(F.log_ids[i] & 0xffffff);
    return -1;
}
static void test_boot_contract(void)
{
    world_reset();
    assert(port_find() == SP_RETURN_FOUND);
    /* FindAdapter is the 0.4.8.3 one: one extension of the 0.4.8.3 size, no pool memory. */
    assert(F.uncached_calls == 1 && F.uncached_bytes == EXT_083_BYTES && F.allocs == 0);
    assert(APPLE_ANS_DIRECT_BYTES == EXT_083_BYTES);
    assert(A->Bounce.count == 0 && A->Stage == AppleAnsStageResourcesAccepted);
    /* The carve touches nothing past the 0.4.8.3 arrays (the slack keeps its fill). */
    world_reset();
    assert(port_start());
    assert(F.allocs == ANS_BOUNCE_COUNT && F.alloc_ok == ANS_BOUNCE_COUNT && F.frees == 0);
    assert(A->Bounce.count == ANS_BOUNCE_COUNT);
    const ans_u8 *p = receipt();
    assert(le(p + 8, 4) == ANS_BOUNCE_COUNT && le(p + 28, 4) == ANS_BOUNCE_COUNT);
    assert(le(p + 112, 4) == ANS_BOUNCE_STEP_NONE && le(p + 128, 4) == 0);
    assert(logged(0xAB) == (int)(ANS_BOUNCE_COUNT << 16));
}
static void test_bounced_round_trip(void)
{
    world_reset();
    assert(port_start());
    static const unsigned offsets[] = {16, 0x20, 0x78, 0xee8, 0xfc0};
    static const unsigned sizes[] = {1, 2, 3, 17, 32};
    unsigned seed = 10;
    for (unsigned o = 0; o < 5; o++)
        for (unsigned s = 0; s < 5; s++) round_trip(o * 40 + s, sizes[s], offsets[o], 1, seed++);
    round_trip(250, 3, 0, 0, seed++); /* aligned: never bounced */
    const ans_u8 *p = receipt();
    assert(le(p + 32, 8) == 25 && le(p + 40, 8) == 25 && le(p + 80, 8) == 50);
    assert(le(p + 20, 4) == 0 && le(p + 24, 4) == 1 && le(p + 48, 8) == 0);
    assert(A->Bounce.claimed == 0);
}
static void test_partial_pool(void)
{
    /* Allocation 0 fails, 1 has no physical address, 2 lies in the alias backing, 3 crosses
     * the end of guest RAM, 4 and 5 are good, the rest fail: two buffers. */
    world_reset();
    F.alloc_fail_mask = ~0x3eu; F.alloc_phys_fail_mask = 1u << 1;
    F.alloc_ipa[2] = ANSD_LOW_PA + 0x10000u; F.alloc_ipa[3] = ANSD_RAM_END - 0x8000u;
    assert(port_start());
    assert(A->Bounce.count == 2 && F.alloc_ok == 5 && F.frees == 3);
    const ans_u8 *p = receipt();
    assert(le(p + 8, 4) == 2 && le(p + 28, 4) == ANS_BOUNCE_COUNT && le(p + 128, 4) == 14);
    assert(le(p + 112, 4) == ANS_BOUNCE_STEP_ALLOCATE && le(p + 116, 4) == 0);
    assert(logged(0xAB) == (int)((2u << 16) | (ANS_BOUNCE_STEP_ALLOCATE << 12) | 14u));
    round_trip(5, 32, 0x78, 1, 40);
    /* Exhaustion: two bounced writes outstanding, the third is BUSY and pauses the LU; after
     * the completions it is resubmitted and succeeds. */
    F.hold = 1;
    struct req *w[3];
    for (unsigned i = 0; i < 3; i++) { w[i] = req_new(1, 60 + i * 4, 2, 0x20); pattern(w[i]->data, 2 * PAGE, 50 + i); port_submit(w[i]); }
    assert(w[0]->srb.SrbStatus == SRB_STATUS_PENDING && w[1]->srb.SrbStatus == SRB_STATUS_PENDING);
    assert(w[2]->srb.SrbStatus == SRB_STATUS_BUSY && F.device_busy == 1);
    assert(A->Bounce.claimed == 3);
    F.hold = 0; sim_release_held(); port_interrupt();
    assert(w[0]->srb.SrbStatus == SRB_STATUS_SUCCESS && w[1]->srb.SrbStatus == SRB_STATUS_SUCCESS && A->Bounce.claimed == 0);
    port_submit(w[2]); port_interrupt();
    assert(w[2]->srb.SrbStatus == SRB_STATUS_SUCCESS);
    for (unsigned i = 0; i < 3; i++) {
        uint8_t want[2 * PAGE]; pattern(want, sizeof(want), 50 + i);
        assert(!memcmp(disk + (size_t)(60 + i * 4) * PAGE, want, sizeof(want)));
        req_free(w[i]);
    }
    /* First failing step is reported with its address: translation of the alias backing. */
    world_reset();
    F.alloc_ipa[0] = ANSD_LOW_PA + 0x10000u;
    assert(port_start() && A->Bounce.count == ANS_BOUNCE_COUNT - 1 && F.frees == 1);
    p = receipt();
    assert(le(p + 112, 4) == ANS_BOUNCE_STEP_TRANSLATE && le(p + 116, 4) == 0 &&
           le(p + 120, 8) == ANSD_LOW_PA + 0x10000u && le(p + 128, 4) == 1);
    world_reset();
    F.alloc_phys_fail_mask = 1u << 3;
    assert(port_start() && A->Bounce.count == ANS_BOUNCE_COUNT - 1);
    p = receipt();
    assert(le(p + 112, 4) == ANS_BOUNCE_STEP_PHYSICAL && le(p + 116, 4) == 3);
}
static void test_no_pool_is_083(void)
{
    world_reset();
    F.alloc_fail_mask = ~0u;
    assert(port_start());
    assert(A->Bounce.count == 0 && F.alloc_ok == 0 && F.allocs == ANS_BOUNCE_COUNT);
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) assert(A->BounceBuffer[i] == NULL && A->BouncePa[i] == 0);
    round_trip(7, 32, 0xee8, 0, 70);
    round_trip(70, 1, 16, 0, 71);
    /* Mapping is never asked for, nothing is counted as bounced, deferred or busy. */
    F.sysaddr_fail = 1;
    round_trip(90, 2, 0x20, 0, 72);
    const ans_u8 *p = receipt();
    assert(le(p + 8, 4) == 0 && le(p + 112, 4) == ANS_BOUNCE_STEP_ALLOCATE && le(p + 128, 4) == ANS_BOUNCE_COUNT);
    for (unsigned off = 32; off < 112; off += 8) assert(le(p + off, 8) == 0);
    assert(logged(0xAB) == (int)((ANS_BOUNCE_STEP_ALLOCATE << 12) | ANS_BOUNCE_COUNT) && F.device_busy == 0);
}
static void test_mapping_failures(void)
{
    world_reset();
    assert(port_start());
    struct req *w = req_new(1, 4, 1, 0x40);
    F.sysaddr_fail = 1;
    /* The port driver resubmits a BUSY request without preparing it again. */
    assert(init_data.HwBuildIo(adapter_mem, &w->srb));
    for (unsigned i = 0; i < ANS_BOUNCE_DEFER_LIMIT; i++) {
        w->srb.SrbStatus = SRB_STATUS_PENDING;
        assert(init_data.HwStartIo(adapter_mem, &w->srb) && w->srb.SrbStatus == SRB_STATUS_BUSY);
    }
    w->srb.SrbStatus = SRB_STATUS_PENDING;
    assert(init_data.HwStartIo(adapter_mem, &w->srb) && w->srb.SrbStatus == SRB_STATUS_ERROR);
    F.sysaddr_fail = 0; F.sysaddr_wrong = 1;
    port_submit(w);
    assert(w->srb.SrbStatus == SRB_STATUS_ERROR && F.nio == 0 && A->Bounce.claimed == 0);
    req_free(w);
}
static void test_dump_seal_is_083(void)
{
    world_reset();
    assert(port_start());
    MINIPORT_DUMP_POINTERS p = {0};
    SCSI_REQUEST_BLOCK srb = {0};
    srb.Function = SRB_FUNCTION_DUMP_POINTERS; srb.DataBuffer = &p; srb.DataTransferLength = sizeof(p);
    assert(init_data.HwBuildIo(adapter_mem, &srb) && init_data.HwStartIo(adapter_mem, &srb));
    assert(srb.SrbStatus == SRB_STATUS_SUCCESS && p.MiniportPrivateDumpData == &A->DumpContext);
    assert(A->DumpContext.common_bytes == EXT_083_BYTES && A->DumpContext.common_pa == F.uncached_ipa);
    /* Same seal with no pool at all. */
    world_reset();
    F.alloc_fail_mask = ~0u;
    assert(port_start());
    srb.SrbStatus = SRB_STATUS_PENDING; srb.DataTransferLength = sizeof(p);
    assert(init_data.HwBuildIo(adapter_mem, &srb) && init_data.HwStartIo(adapter_mem, &srb));
    assert(srb.SrbStatus == SRB_STATUS_SUCCESS && A->DumpContext.common_bytes == EXT_083_BYTES);
}
#endif
int main(int argc, char **argv)
{
    test_pool_failure_never_fails_boot(argc > 1 ? atoi(argv[1]) : 0);
#ifndef HARNESS_OLD
    test_boot_contract();
    test_bounced_round_trip();
    test_partial_pool();
    test_no_pool_is_083();
    test_mapping_failures();
    test_dump_seal_is_083();
#endif
    puts("storport shim harness: ok");
    return 0;
}
