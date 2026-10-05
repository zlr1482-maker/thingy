/*
 * Vmx.cpp - Intel VT-x initialization and teardown
 *
 * Hyperjacking strategy:
 *   1. Check CPU VMX support (CPUID + IA32_FEATURE_CONTROL).
 *   2. On each logical core (via IPI):
 *        a. Allocate VMXON region, VMCS region, MSR bitmap, host stack.
 *        b. Enable CR4.VMXE, execute VMXON.
 *        c. VMCLEAR + VMPTRLD the VMCS.
 *        d. Write host/guest VMCS fields from current CPU state.
 *        e. VMLAUNCH — Windows continues in VMX non-root (guest) mode.
 *           Host (hypervisor) wakes on the first VM exit.
 *   3. On unload: VMCALL HYPERCALL_VMXOFF on each core → VmxShutdownCore.
 */

#include <ntifs.h>
#include <intrin.h>
#include "..\include\Vmx.h"
#include "..\include\Ept.h"
#include "..\include\Intel.h"

// Declared in Driver.cpp
extern VmxGlobal* g_Vmx;
extern EptState*  g_Ept;

// Defined in VmExit.cpp — caches VMware-Hv MSR values at driver init so
// the guest reads get sensible values after vmxstart.
extern "C" VOID VmxSnapshotHvMsrs();

// TRUE when the host is bare metal (no parent hypervisor).  Set in
// VmxInit before vmlaunch via CPUID.1:ECX[31].  Read by HandleRdmsr
// in the VM-exit path — on bare metal we MUST skip __readmsr for the
// Hv MSR range because (a) those MSRs don't exist on the CPU and #GP,
// and (b) the __try/__except guard cannot catch in VM-exit context
// (handler runs on the per-core host stack, which the kernel's SEH
// dispatcher rejects as "invalid stack" → 0x1AA bugcheck).
extern "C" BOOLEAN g_BareMetal = FALSE;

// Defined in VmxAsm.asm.
extern "C" void AsmInvept(UINT64 type, UINT64* descriptor);

// Defined in Driver.cpp — used by the self-cloak step in VmxInit.
extern "C" ULONG64 g_OwnImageBase;
extern "C" ULONG   g_OwnImageSize;

// ============================================================
//  CPUID / Feature check
// ============================================================

BOOLEAN VmxCheckSupport() {
    // CPUID.1:ECX[5] = VMX support
    int cpuInfo[4] = {};
    __cpuid(cpuInfo, 1);
    if (!(cpuInfo[2] & (1 << 5))) {
        DbgPrint("[VMX] CPUID VMX bit not set.\n");
        return FALSE;
    }

    // IA32_FEATURE_CONTROL must have LOCK bit set and VMX-outside-SMX enabled.
    UINT64 featureCtl = __readmsr(MSR_IA32_FEATURE_CONTROL);
    if (!(featureCtl & FEATURE_CONTROL_LOCK_BIT)) {
        // Firmware didn't lock; we can write the bits ourselves.
        featureCtl |= FEATURE_CONTROL_LOCK_BIT | FEATURE_CONTROL_VMX_OUTSIDE_SMX;
        __writemsr(MSR_IA32_FEATURE_CONTROL, featureCtl);
    } else if (!(featureCtl & FEATURE_CONTROL_VMX_OUTSIDE_SMX)) {
        DbgPrint("[VMX] VMX disabled by BIOS (IA32_FEATURE_CONTROL).\n");
        return FALSE;
    }

    return TRUE;
}

// ============================================================
//  IPI callback — runs VmxInitCore on a single logical processor
// ============================================================
static ULONG_PTR VmxInitIpiCallback(ULONG_PTR context) {
    UNREFERENCED_PARAMETER(context);
    UINT32    coreIndex = KeGetCurrentProcessorNumberEx(nullptr);
    VmxCore*  core      = &g_Vmx->Cores[coreIndex];
    core->CoreIndex     = coreIndex;

    NTSTATUS status = VmxInitCore(core);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[VMX] Core %u init failed: 0x%X\n", coreIndex, status);
    }
    return (ULONG_PTR)status;
}

// IPI callback for shutdown
static ULONG_PTR VmxShutdownIpiCallback(ULONG_PTR context) {
    UNREFERENCED_PARAMETER(context);
    UINT32   coreIndex = KeGetCurrentProcessorNumberEx(nullptr);
    VmxCore* core      = &g_Vmx->Cores[coreIndex];
    VmxShutdownCore(core);
    return 0;
}

// Per-core resource allocation — runs at PASSIVE_LEVEL before the IPI.
// MmAllocateContiguousMemorySpecifyCache, ExAllocatePool2, and MmGetPhysicalAddress
// all require IRQL <= DISPATCH_LEVEL, so they cannot live inside KeIpiGenericCall
// callbacks (which run at IPI_LEVEL).
static NTSTATUS VmxAllocateCoreResources(VmxCore* core) {
    PHYSICAL_ADDRESS lo = {}, hi = {}, boundary = {};
    hi.QuadPart = 0xFFFFFFFFLL;  // Keep regions below 4GB for nested-VT-x compatibility

    core->VmxonRegion = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, lo, hi, boundary, MmNonCached);
    if (!core->VmxonRegion) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->VmxonRegion, PAGE_SIZE);

    core->VmcsRegion = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, lo, hi, boundary, MmNonCached);
    if (!core->VmcsRegion) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->VmcsRegion, PAGE_SIZE);

    core->MsrBitmap = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, XENTYPE2_POOL_TAG);
    if (!core->MsrBitmap) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->MsrBitmap, PAGE_SIZE);

    // IO bitmaps — A covers ports 0x0000-0x7FFF, B covers 0x8000-0xFFFF.
    // Zero-fill = passthrough; we only set the bit for 0x5658 (VMware
    // backdoor) so we can selectively filter EAC's VM-detect probe
    // without breaking VMware Tools.
    core->IoBitmapA = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, XENTYPE2_POOL_TAG);
    if (!core->IoBitmapA) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->IoBitmapA, PAGE_SIZE);
    // Port 0x5658 is in bitmap A (bit indexed by port number).
    ((UCHAR*)core->IoBitmapA)[0x5658 / 8] |= (UCHAR)(1u << (0x5658 % 8));
    // Port 0x5659 too — VMware sometimes uses the high-bandwidth backdoor
    // (VMware Workstation calls it VMXh-HB).  Single bit, same bitmap.
    ((UCHAR*)core->IoBitmapA)[0x5659 / 8] |= (UCHAR)(1u << (0x5659 % 8));

    core->IoBitmapB = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, XENTYPE2_POOL_TAG);
    if (!core->IoBitmapB) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->IoBitmapB, PAGE_SIZE);

    core->HostStack = ExAllocatePool2(POOL_FLAG_NON_PAGED, VMX_HOST_STACK_SIZE, XENTYPE2_POOL_TAG);
    if (!core->HostStack) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(core->HostStack, VMX_HOST_STACK_SIZE);
    core->HostStackTop = ((UINT64)core->HostStack + VMX_HOST_STACK_SIZE) & ~0xFULL;

    core->VmxonPhys     = MmGetPhysicalAddress(core->VmxonRegion).QuadPart;
    core->VmcsPhys      = MmGetPhysicalAddress(core->VmcsRegion).QuadPart;
    core->MsrBitmapPhys = MmGetPhysicalAddress(core->MsrBitmap).QuadPart;
    core->IoBitmapAPhys = MmGetPhysicalAddress(core->IoBitmapA).QuadPart;
    core->IoBitmapBPhys = MmGetPhysicalAddress(core->IoBitmapB).QuadPart;

    // Write VMX revision ID into VMXON and VMCS regions (required by the CPU)
    UINT64 vmxBasic = __readmsr(MSR_IA32_VMX_BASIC);
    UINT32 revId    = (UINT32)(vmxBasic & 0x7FFFFFFF);
    *(UINT32*)core->VmxonRegion = revId;
    *(UINT32*)core->VmcsRegion  = revId;
    return STATUS_SUCCESS;
}

static VOID VmxFreeCoreResources(VmxCore* core) {
    if (core->HostStack)   { ExFreePoolWithTag(core->HostStack,  XENTYPE2_POOL_TAG); core->HostStack  = nullptr; }
    if (core->MsrBitmap)   { ExFreePoolWithTag(core->MsrBitmap,  XENTYPE2_POOL_TAG); core->MsrBitmap  = nullptr; }
    if (core->IoBitmapA)   { ExFreePoolWithTag(core->IoBitmapA,  XENTYPE2_POOL_TAG); core->IoBitmapA  = nullptr; }
    if (core->IoBitmapB)   { ExFreePoolWithTag(core->IoBitmapB,  XENTYPE2_POOL_TAG); core->IoBitmapB  = nullptr; }
    if (core->VmcsRegion)  { MmFreeContiguousMemory(core->VmcsRegion);  core->VmcsRegion  = nullptr; }
    if (core->VmxonRegion) { MmFreeContiguousMemory(core->VmxonRegion); core->VmxonRegion = nullptr; }
    core->VmxonPhys = core->VmcsPhys = core->MsrBitmapPhys = 0;
}

// ============================================================
//  VmxInit — top-level, called from DriverEntry
// ============================================================
NTSTATUS VmxInit() {
    g_Vmx->CoreCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (g_Vmx->CoreCount > MAX_LOGICAL_CORES) {
        DbgPrint("[VMX] Too many logical cores (%u > %u).\n", g_Vmx->CoreCount, MAX_LOGICAL_CORES);
        return STATUS_NOT_SUPPORTED;
    }

    // HostCr3 was captured at DriverEntry (System process context) — stable
    // across xentype2.exe's lifetime.  Don't recapture here or we'd grab the
    // IOCTL-caller's CR3 which gets freed when that process exits.

    // Allocate per-core resources at PASSIVE_LEVEL before issuing the IPI.
    for (UINT32 i = 0; i < g_Vmx->CoreCount; i++) {
        VmxCore* core = &g_Vmx->Cores[i];
        core->CoreIndex = i;
        NTSTATUS allocStatus = VmxAllocateCoreResources(core);
        if (!NT_SUCCESS(allocStatus)) {
            g_Vmx->LastVmlaunchError = 0xDEAD0010;
            g_Vmx->LastFailedCore    = i;
            DbgPrint("[VMX] VmxAllocateCoreResources failed on core %u: 0x%X\n", i, allocStatus);
            for (UINT32 j = 0; j <= i; j++) VmxFreeCoreResources(&g_Vmx->Cores[j]);
            return allocStatus;
        }
    }

    // Detect bare-metal vs nested-under-hypervisor.  CPUID.1:ECX[31] is the
    // standard "hypervisor present" hint — set by Hyper-V/VMware/KVM etc.,
    // clear on bare metal.  Set BEFORE vmlaunch (so we observe the actual
    // host CPU state, not our own post-vmlaunch CPUID emulation).  Used by
    // HandleRdmsr to decide whether passthrough __readmsr on Hv MSRs is
    // safe.  See the comment on g_BareMetal at file scope.
    int hvHint[4] = {};
    __cpuid(hvHint, 1);
    g_BareMetal = (hvHint[2] & (1u << 31)) ? FALSE : TRUE;
    DbgPrint("[VMX] Host environment: %s\n",
             g_BareMetal ? "BARE METAL" : "NESTED (hypervisor present)");

    // Snapshot Hv MSR values only when running under another hypervisor.
    // On bare metal those MSRs don't exist and RDMSR #GPs; the __try in
    // VmxSnapshotHvMsrs can't catch because manual-mapped SEH dispatch
    // is partial.  Cached store stays zero in bare-metal mode, which
    // matches the "no Hv emulation present" reality.
    if (!g_BareMetal) {
        VmxSnapshotHvMsrs();
    }

    // Run per-core VMX enablement on every logical processor simultaneously.
    KeIpiGenericCall(VmxInitIpiCallback, 0);

    // Check all cores succeeded
    BOOLEAN anyFailed = FALSE;
    for (UINT32 i = 0; i < g_Vmx->CoreCount; i++) {
        if (!g_Vmx->Cores[i].VmxEnabled) {
            anyFailed = TRUE;
        }
    }

    if (anyFailed) {
        // Tear down successfully-started cores before returning error
        KeIpiGenericCall(VmxShutdownIpiCallback, 0);
        for (UINT32 i = 0; i < g_Vmx->CoreCount; i++) VmxFreeCoreResources(&g_Vmx->Cores[i]);
        return STATUS_UNSUCCESSFUL;
    }

    g_Vmx->Initialized = TRUE;

    // Initialize the decryption-learner subsystem (no-op if already init'd).
    // Must run AFTER VMX is up since it will use VMCS read/write helpers in
    // the exit handlers.
    extern VOID EptTraceInit(VOID);
    extern VOID EptTraceVcpuInit(UINT32);
    EptTraceInit();
    for (UINT32 i = 0; i < g_Vmx->CoreCount && i < 64; ++i) {
        EptTraceVcpuInit(i);
    }

    // Cloak our own image from kernel scanners (EAC/Hyperion/etc.).
    // This must run AFTER vmlaunch — the EPT entry rewrites only take
    // effect once EPT is being walked by the CPU, and we need to
    // broadcast invept to flush stale cached translations on every core.
    if (g_OwnImageBase && g_OwnImageSize) {
        NTSTATUS cloakStatus = EptCloakImage(g_OwnImageBase, g_OwnImageSize);
        if (NT_SUCCESS(cloakStatus)) {
            // Broadcast a single-context EPT TLB flush so every core
            // observes the post-cloak entries on its next memory access.
            // The IPI callback runs at IPI_LEVEL where AsmInvept is safe.
            KeIpiGenericCall([](ULONG_PTR) -> ULONG_PTR {
                UINT64 desc[2] = { EptGetEptp(), 0 };
                AsmInvept(1, desc);
                return 0;
            }, 0);
            DbgPrint("[VMX] Self-cloak complete; INVEPT broadcast on %u cores.\n",
                     g_Vmx->CoreCount);
        } else {
            DbgPrint("[VMX] Self-cloak failed: 0x%X (continuing without cloak).\n",
                     cloakStatus);
        }
    } else {
        DbgPrint("[VMX] Self-cloak skipped: image bounds not discovered "
                 "(base=0x%llX size=0x%X).\n",
                 (UINT64)g_OwnImageBase, g_OwnImageSize);
    }

    return STATUS_SUCCESS;
}

// ============================================================
//  VmxShutdown — top-level, called from DriverUnload
// ============================================================
VOID VmxShutdown() {
    KeIpiGenericCall(VmxShutdownIpiCallback, 0);
    for (UINT32 i = 0; i < g_Vmx->CoreCount; i++) VmxFreeCoreResources(&g_Vmx->Cores[i]);
    g_Vmx->Initialized = FALSE;
}

// ============================================================
//  Segment helpers
// ============================================================

static UINT32 GetSegmentAccessRights(UINT64 gdtBase, UINT16 selector) {
    if (selector == 0 || (selector & 0x4)) {
        // Null selector or LDT-based — mark unusable
        SegmentAccessRights ar = {};
        ar.Unusable = 1;
        return ar.Value;
    }
    // Read directly from GDT descriptor (no __lar needed)
    // Byte 5 = access byte (Type[3:0], S, DPL[1:0], P)
    // Byte 6 = limit[19:16][3:0], AVL, L, D/B, G  — upper nibble is flags
    UINT8* desc      = (UINT8*)(gdtBase + (selector & ~0x7u));
    UINT32 accessByte = desc[5];
    UINT32 flags      = (desc[6] >> 4) & 0xF; // G, D/B, L, AVL → bits 15:12 of VMCS field
    return accessByte | (flags << 12);
}

static UINT64 GetSegmentBase(UINT64 gdtBase, UINT16 selector) {
    if (selector == 0) return 0;
    return AsmGetSegmentBase(gdtBase, selector);
}

static UINT32 GetSegmentLimit(UINT16 selector) {
    if (selector == 0) return 0;
    return AsmGetSegmentLimit(selector);
}

// ============================================================
//  VMCS host/guest state write helpers
// ============================================================

static VOID VmcsWriteGuestSegment(
    UINT32 selField, UINT32 baseField, UINT32 limitField, UINT32 arField,
    UINT16 sel, UINT64 gdtBase)
{
    VmcsWrite(selField,   sel);
    VmcsWrite(baseField,  GetSegmentBase(gdtBase, sel));
    VmcsWrite(limitField, GetSegmentLimit(sel));
    VmcsWrite(arField,    GetSegmentAccessRights(gdtBase, sel));
}

// ============================================================
//  VmxInitCore — allocate regions, enable VMX, VMCS setup, VMLAUNCH
// ============================================================
NTSTATUS VmxInitCore(VmxCore* core) {
    // Declare these early to avoid C4533 "initialization skipped by goto"
    GdtRegister gdt = {};
    IdtRegister idt = {};

    // Memory allocation and MmGetPhysicalAddress already happened at PASSIVE_LEVEL
    // in VmxAllocateCoreResources.  We only do VMX instructions and CPU-state reads
    // here, all of which are safe at IPI_LEVEL.
    UINT64 vmxBasic     = __readmsr(MSR_IA32_VMX_BASIC);
    BOOLEAN useTrueCtls = (vmxBasic >> 55) & 1; // Bit 55: TRUE controls MSRs available

    // --- Enable CR4.VMXE ---
    __writecr4(__readcr4() | (1ULL << 13));

    // --- VMXON ---
    UINT64 vmxonPhys = core->VmxonPhys;
    unsigned char vmxonResult = __vmx_on(&vmxonPhys);
    if (vmxonResult != 0) {
        g_Vmx->LastVmlaunchError = 0xDEAD0001 | ((UINT64)vmxonResult << 16);
        g_Vmx->LastFailedCore    = core->CoreIndex;
        DbgPrint("[VMX] VMXON failed on core %u (result=%u, phys=0x%llX)\n",
                 core->CoreIndex, vmxonResult, vmxonPhys);
        goto cleanup;
    }
    core->VmxEnabled = TRUE;

    // --- VMCLEAR ---
    UINT64 vmcsPhys = core->VmcsPhys;
    if (__vmx_vmclear(&vmcsPhys) != 0) {
        g_Vmx->LastVmlaunchError = 0xDEAD0002;
        g_Vmx->LastFailedCore    = core->CoreIndex;
        DbgPrint("[VMX] VMCLEAR failed on core %u\n", core->CoreIndex);
        goto cleanup_vmxoff;
    }

    // --- VMPTRLD ---
    if (__vmx_vmptrld(&vmcsPhys) != 0) {
        g_Vmx->LastVmlaunchError = 0xDEAD0003;
        g_Vmx->LastFailedCore    = core->CoreIndex;
        DbgPrint("[VMX] VMPTRLD failed on core %u\n", core->CoreIndex);
        goto cleanup_vmxoff;
    }

    // ============================================================
    //  Write VMCS fields
    // ============================================================

    // --- Control fields ---
    UINT32 pinCtlsMsr  = useTrueCtls ? MSR_IA32_VMX_TRUE_PINBASED  : MSR_IA32_VMX_PINBASED_CTLS;
    UINT32 procCtlsMsr = useTrueCtls ? MSR_IA32_VMX_TRUE_PROCBASED : MSR_IA32_VMX_PROCBASED_CTLS;
    UINT32 exitCtlsMsr = useTrueCtls ? MSR_IA32_VMX_TRUE_EXIT_CTLS : MSR_IA32_VMX_EXIT_CTLS;
    UINT32 entryCtlsMsr= useTrueCtls ? MSR_IA32_VMX_TRUE_ENTRY_CTLS: MSR_IA32_VMX_ENTRY_CTLS;

    UINT64 pinCtls  = AdjustControls(0, pinCtlsMsr);
    UINT64 procCtls = AdjustControls(
        CPU_BASED_USE_MSR_BITMAP | CPU_BASED_USE_IO_BITMAP | CPU_BASED_ACTIVATE_CTLS2,
        procCtlsMsr
    );
    // Enable EPT + VPID + XSAVES + RDTSCP + INVPCID.  VMware's nested VT-x
    // appears to require EPT for stable operation; without it VM exits from
    // level-2 guests cause triple faults in VMware's emulator.
    UINT64 procCtls2 = AdjustControls(
        CPU_BASED2_ENABLE_EPT    | CPU_BASED2_ENABLE_VPID |
        CPU_BASED2_ENABLE_RDTSCP | CPU_BASED2_ENABLE_INVPCID |
        CPU_BASED2_XSAVE_XRSTOR,
        MSR_IA32_VMX_PROCBASED_CTLS2
    );
    UINT64 exitCtls  = AdjustControls(
        VM_EXIT_HOST_ADDR_SPACE_SIZE | VM_EXIT_LOAD_IA32_EFER | VM_EXIT_SAVE_IA32_EFER,
        exitCtlsMsr
    );
    UINT64 entryCtls = AdjustControls(
        VM_ENTRY_IA32E_MODE_GUEST | VM_ENTRY_LOAD_IA32_EFER,
        entryCtlsMsr
    );

    VmcsWrite(VMCS_PIN_BASED_CTLS,    pinCtls);
    VmcsWrite(VMCS_PROC_BASED_CTLS,   procCtls);
    VmcsWrite(VMCS_PROC_BASED_CTLS2,  procCtls2);
    VmcsWrite(VMCS_VMEXIT_CTLS,       exitCtls);
    VmcsWrite(VMCS_VMENTRY_CTLS,      entryCtls);

    DbgPrint("[VMX] core %u adjusted ctls: proc=0x%llX proc2=0x%llX exit=0x%llX entry=0x%llX\n",
             core->CoreIndex, procCtls, procCtls2, exitCtls, entryCtls);
    UINT64 liveCr4 = __readcr4();
    DbgPrint("[VMX] core %u CR4 live=0x%llX (CET=%u PKS=%u UINTR=%u LAM_SUP=%u VMXE=%u PCIDE=%u LA57=%u)\n",
             core->CoreIndex, liveCr4,
             (UINT32)((liveCr4 >> 23) & 1),
             (UINT32)((liveCr4 >> 24) & 1),
             (UINT32)((liveCr4 >> 25) & 1),
             (UINT32)((liveCr4 >> 28) & 1),
             (UINT32)((liveCr4 >> 13) & 1),
             (UINT32)((liveCr4 >> 17) & 1),
             (UINT32)((liveCr4 >> 12) & 1));

    VmcsWrite(VMCS_EXCEPTION_BITMAP, 0);          // No exception exits
    VmcsWrite(VMCS_CR3_TARGET_COUNT,  0);          // No CR3 filtering
    VmcsWrite(VMCS_VMENTRY_INT_INFO_FIELD, 0);
    VmcsWrite(VMCS_VMENTRY_INSTR_LEN, 0);
    VmcsWrite(VMCS_VMENTRY_XCPT_ERR_CODE, 0);

    // MSR bitmap physical address
    VmcsWrite(VMCS_MSR_BITMAP, core->MsrBitmapPhys);

    // IO bitmaps (used with CPU_BASED_USE_IO_BITMAP)
    VmcsWrite(VMCS_IO_BITMAP_A, core->IoBitmapAPhys);
    VmcsWrite(VMCS_IO_BITMAP_B, core->IoBitmapBPhys);

    // EPTP: only write when EPT is actually enabled in adjusted procCtls2
    if (procCtls2 & CPU_BASED2_ENABLE_EPT)
        VmcsWrite(VMCS_EPTP, g_Ept ? g_Ept->Eptp.Value : 0);

    // VPID: writing a non-zero value when VPID is not enabled causes VMLAUNCH error 7
    if (procCtls2 & CPU_BASED2_ENABLE_VPID)
        VmcsWrite(VMCS_VPID, core->CoreIndex + 1);

    // Guest VMCS link pointer = 0xFFFFFFFFFFFFFFFF (not in SMM dual-monitor)
    VmcsWrite(VMCS_GUEST_VMCS_LINK_PTR, ~0ULL);

    // --- Capture current CPU state for guest ---
    AsmGetGdtr(&gdt);
    AsmGetIdtr(&idt);

    UINT16 cs   = AsmGetCs();
    UINT16 ss   = AsmGetSs();
    UINT16 ds   = AsmGetDs();
    UINT16 es   = AsmGetEs();
    UINT16 fs   = AsmGetFs();
    UINT16 gs   = AsmGetGs();
    UINT16 ldtr = AsmGetLdtr();
    UINT16 tr   = AsmGetTr();

    // Guest segment state
    VmcsWriteGuestSegment(VMCS_GUEST_CS_SEL,   VMCS_GUEST_CS_BASE,   VMCS_GUEST_CS_LIMIT,   VMCS_GUEST_CS_ACCESS,   cs,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_SS_SEL,   VMCS_GUEST_SS_BASE,   VMCS_GUEST_SS_LIMIT,   VMCS_GUEST_SS_ACCESS,   ss,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_DS_SEL,   VMCS_GUEST_DS_BASE,   VMCS_GUEST_DS_LIMIT,   VMCS_GUEST_DS_ACCESS,   ds,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_ES_SEL,   VMCS_GUEST_ES_BASE,   VMCS_GUEST_ES_LIMIT,   VMCS_GUEST_ES_ACCESS,   es,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_FS_SEL,   VMCS_GUEST_FS_BASE,   VMCS_GUEST_FS_LIMIT,   VMCS_GUEST_FS_ACCESS,   fs,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_GS_SEL,   VMCS_GUEST_GS_BASE,   VMCS_GUEST_GS_LIMIT,   VMCS_GUEST_GS_ACCESS,   gs,   gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_LDTR_SEL, VMCS_GUEST_LDTR_BASE, VMCS_GUEST_LDTR_LIMIT, VMCS_GUEST_LDTR_ACCESS, ldtr, gdt.Base);
    VmcsWriteGuestSegment(VMCS_GUEST_TR_SEL,   VMCS_GUEST_TR_BASE,   VMCS_GUEST_TR_LIMIT,   VMCS_GUEST_TR_ACCESS,   tr,   gdt.Base);

    // FS/GS bases come from MSRs on x64
    VmcsWrite(VMCS_GUEST_FS_BASE, __readmsr(MSR_IA32_FS_BASE));
    VmcsWrite(VMCS_GUEST_GS_BASE, __readmsr(MSR_IA32_GS_BASE));

    VmcsWrite(VMCS_GUEST_GDTR_BASE,  gdt.Base);
    VmcsWrite(VMCS_GUEST_GDTR_LIMIT, gdt.Limit);
    VmcsWrite(VMCS_GUEST_IDTR_BASE,  idt.Base);
    VmcsWrite(VMCS_GUEST_IDTR_LIMIT, idt.Limit);

    // CR4 bits that need special VMCS save/restore we don't provide:
    //   bit 23 CR4.CET     — Control-flow Enforcement / shadow stack
    //   bit 24 CR4.PKS     — Protection Keys Supervisor
    //   bit 25 CR4.UINTR   — User Interrupts
    //   bit 28 CR4.LAM_SUP — Linear Address Masking (supervisor)
    //
    // We mask these OUT of HOST_CR4 so our host code (which doesn't use any of
    // the CET/PKS/UINTR/LAM instructions) runs without those checks.  We KEEP
    // them in GUEST_CR4 so Windows kernel's CET instructions (RSTORSSP etc)
    // don't #UD.  The per-feature MSR state (IA32_S_CET, PL0_SSP, PKRS...) is
    // per-CPU, not per-VMX-mode, so it persists across exits intact.
    const UINT64 CR4_NEW_FEATURES_MASK =
        (1ULL << 23) | (1ULL << 24) | (1ULL << 25) | (1ULL << 28);
    UINT64 hostCr4  = liveCr4 & ~CR4_NEW_FEATURES_MASK;  // host with CET/etc off
    UINT64 guestCr4 = liveCr4;                            // guest keeps the features

    VmcsWrite(VMCS_GUEST_CR0, __readcr0());
    VmcsWrite(VMCS_GUEST_CR3, __readcr3());
    VmcsWrite(VMCS_GUEST_CR4, guestCr4);
    VmcsWrite(VMCS_GUEST_DR7, __readdr(7));

    VmcsWrite(VMCS_CR0_GUEST_HOST_MASK, 0);
    VmcsWrite(VMCS_CR0_READ_SHADOW,     __readcr0());
    VmcsWrite(VMCS_CR4_GUEST_HOST_MASK, 0);
    VmcsWrite(VMCS_CR4_READ_SHADOW,     guestCr4);

    VmcsWrite(VMCS_GUEST_RFLAGS, AsmGetRflags());
    VmcsWrite(VMCS_GUEST_IA32_SYSENTER_CS,  __readmsr(MSR_IA32_SYSENTER_CS));
    VmcsWrite(VMCS_GUEST_IA32_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
    VmcsWrite(VMCS_GUEST_IA32_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));
    VmcsWrite(VMCS_GUEST_IA32_EFER,         __readmsr(MSR_IA32_EFER));
    VmcsWrite(VMCS_GUEST_IA32_DEBUGCTL,     __readmsr(MSR_IA32_DEBUGCTL));
    VmcsWrite(VMCS_GUEST_ACTIVITY_STATE, 0); // Active
    VmcsWrite(VMCS_GUEST_INT_STATE,      0); // No blocking

    // Guest RIP and RSP are set by VmxLaunchAndEnter() in VmxAsm.asm
    // (it captures the live RSP and sets RIP = &VmxGuestResume before VMLAUNCH)
    VmcsWrite(VMCS_GUEST_RIP, (UINT64)&VmxGuestResume);

    // --- Host state (hypervisor will run here after every VM exit) ---
    VmcsWrite(VMCS_HOST_CS_SEL, cs  & ~0x7); // RPL=0, TI=0
    VmcsWrite(VMCS_HOST_SS_SEL, ss  & ~0x7);
    VmcsWrite(VMCS_HOST_DS_SEL, ds  & ~0x7);
    VmcsWrite(VMCS_HOST_ES_SEL, es  & ~0x7);
    VmcsWrite(VMCS_HOST_FS_SEL, fs  & ~0x7);
    VmcsWrite(VMCS_HOST_GS_SEL, gs  & ~0x7);
    VmcsWrite(VMCS_HOST_TR_SEL, tr  & ~0x7);

    VmcsWrite(VMCS_HOST_FS_BASE,   __readmsr(MSR_IA32_FS_BASE));
    VmcsWrite(VMCS_HOST_GS_BASE,   __readmsr(MSR_IA32_GS_BASE));
    VmcsWrite(VMCS_HOST_TR_BASE,   GetSegmentBase(gdt.Base, tr));
    VmcsWrite(VMCS_HOST_GDTR_BASE, gdt.Base);
    VmcsWrite(VMCS_HOST_IDTR_BASE, idt.Base);

    VmcsWrite(VMCS_HOST_CR0, __readcr0());
    VmcsWrite(VMCS_HOST_CR3, g_Vmx->HostCr3);  // stable CR3 captured at PASSIVE_LEVEL
    VmcsWrite(VMCS_HOST_CR4, hostCr4);          // with CET/PKS/UINTR/LAM bits cleared
    VmcsWrite(VMCS_HOST_RSP, core->HostStackTop);
    VmcsWrite(VMCS_HOST_RIP, (UINT64)VmExitStub);

    VmcsWrite(VMCS_HOST_IA32_SYSENTER_CS,  __readmsr(MSR_IA32_SYSENTER_CS));
    VmcsWrite(VMCS_HOST_IA32_SYSENTER_ESP, __readmsr(MSR_IA32_SYSENTER_ESP));
    VmcsWrite(VMCS_HOST_IA32_SYSENTER_EIP, __readmsr(MSR_IA32_SYSENTER_EIP));
    VmcsWrite(VMCS_HOST_IA32_EFER,         __readmsr(MSR_IA32_EFER));

    // --- VMLAUNCH ---
    // VmxLaunchAndEnter() captures live RSP into VMCS_GUEST_RSP and executes VMLAUNCH.
    // Returns 0 in the guest (Windows continues), non-zero on failure.
    {
        UINT64 result = VmxLaunchAndEnter();
        if (result == 2) {
            // VMfailInvalid: CF=1, no current VMCS — VMCS pointer was invalid
            g_Vmx->LastVmlaunchError = 0xDEAD0004;
            g_Vmx->LastFailedCore    = core->CoreIndex;
            DbgPrint("[VMX] VMLAUNCH VMfailInvalid on core %u (VMCS not current)\n", core->CoreIndex);
            goto cleanup_vmxoff;
        } else if (result == 1) {
            // VMfailValid: ZF=1, VMCS_VM_INSTR_ERROR has the reason
            UINT64 errCode = VmcsRead(VMCS_VM_INSTR_ERROR);
            g_Vmx->LastVmlaunchError = errCode ? errCode : 0xDEAD0005;
            g_Vmx->LastFailedCore    = core->CoreIndex;
            DbgPrint("[VMX] VMLAUNCH VMfailValid on core %u, error code: %llu\n",
                     core->CoreIndex, errCode);
            goto cleanup_vmxoff;
        }
    }

    // If we reach here in the guest path, VMLAUNCH succeeded.
    // The core->Launched flag is set by VmExitHandler on the first VM exit.
    core->Launched = TRUE;
    return STATUS_SUCCESS;

cleanup_vmxoff:
    __vmx_off();
    __writecr4(__readcr4() & ~(1ULL << 13));
    core->VmxEnabled = FALSE;
cleanup:
    // Memory-freeing is deferred to VmxInit, which runs at PASSIVE_LEVEL.
    // ExFreePoolWithTag and MmFreeContiguousMemory require IRQL <= DISPATCH_LEVEL
    // and we are at IPI_LEVEL here.
    return STATUS_UNSUCCESSFUL;
}

// ============================================================
//  VmxShutdownCore — tear down VMX on the current logical processor
// ============================================================
VOID VmxShutdownCore(VmxCore* core) {
    if (!core->VmxEnabled) return;

    // If the core is running as a guest, issue a VMCALL to signal VMXOFF.
    // VmExitHandler handles HYPERCALL_VMXOFF by calling VMXOFF and returning here.
    if (core->Launched) {
        AsmVmcall(HYPERCALL_VMXOFF, 0, 0, 0);
    } else {
        __vmx_off();
        __writecr4(__readcr4() & ~(1ULL << 13));
    }

    core->VmxEnabled = FALSE;
    core->Launched   = FALSE;

    // Memory is freed by VmxShutdown at PASSIVE_LEVEL, not here at IPI_LEVEL.
}
