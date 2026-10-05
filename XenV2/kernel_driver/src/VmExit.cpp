/*
 * VmExit.cpp - VM exit handler
 *
 * Called from VmxAsm.asm on every VM exit with a pointer to the saved
 * GuestRegisters on the host stack.  Dispatches by exit reason.
 *
 * Critical exits to handle for Windows stability:
 *   CPUID       — spoof hypervisor presence, pass everything else through
 *   RDMSR/WRMSR — pass through (MSR bitmap set to all-zeros)
 *   CR access   — pass through CR3 writes (context switches)
 *   XSETBV      — pass through
 *   VMCALL      — our internal hypercall interface
 *   EPT violation — should not occur with a correct identity map; log + advance RIP
 */

#include <ntifs.h>
#include <intrin.h>
#include "..\include\Vmx.h"
#include "..\include\Intel.h"
#include "..\include\Ept.h"
#include "..\include\EptTrace.h"

extern "C" void AsmInvept(UINT64 type, UINT64* descriptor);

extern VmxGlobal* g_Vmx;

// Diagnostic counters — incremented on every VM exit so we can see whether
// VmExitHandler was ever reached between VMLAUNCH and a triple fault.
extern "C" volatile UINT64 g_VmExitCounter[MAX_LOGICAL_CORES] = { 0 };
extern "C" volatile UINT64 g_VmExitLastReason[MAX_LOGICAL_CORES] = { 0 };

// ============================================================
//  Minimal Hyper-V synthetic MSR state (one shared array).
//  Indexed by low byte of MSR number (Hv MSRs we care about are
//  0x40000000-0x400003FF; we store low byte-addressable slice).
//  Populated in two ways:
//   1. VmxSnapshotHvMsrs() at driver init (BEFORE vmlaunch) reads values
//      from VMware-Hv emulation and caches them — so post-vmxstart reads
//      of MSRs the guest never re-wrote return the correct "was this MSR
//      set by Hv at boot?" state.  This avoids NULL-derefs in the HAL
//      (specifically seen on MSR 0x40000105 CRASH_CTL).
//   2. HandleWrmsr updates a slot whenever the guest writes.
// ============================================================
static volatile UINT64 g_HvMsrStore[256] = { 0 };
// First time Hv MSRs are touched, log every access for diagnostics
static volatile LONG   g_HvMsrLogCount = 0;
#define HV_MSR_LOG_LIMIT 80


// ============================================================
//  Helpers
// ============================================================

// ============================================================
//  VmxSnapshotHvMsrs — read a set of Hyper-V MSRs from VMware's Hv
//  emulation while we're still running as an L2 guest (before our
//  vmlaunch) and cache them in g_HvMsrStore.  After vmxstart, guest
//  reads of these MSRs get the correct "pre-existing Hv state" values
//  instead of 0, which avoids NULL-derefs in the HAL code path.
//
//  Called from VmxInit() at PASSIVE_LEVEL — where VMware's Hv MSR
//  emulation is still live in our execution context.
// ============================================================
extern "C" VOID VmxSnapshotHvMsrs() {
    // MSRs we've observed Windows's HAL reading, plus partition-wide
    // control MSRs whose default values matter.  Each wrapped in
    // __try/__except so a non-existent MSR (#GP) doesn't crash us.
    static const UINT32 snapList[] = {
        0x40000000, 0x40000001, 0x40000002, 0x40000003,
        0x40000010,
        0x40000020, 0x40000021, 0x40000022, 0x40000023,
        0x40000070, 0x40000071, 0x40000072, 0x40000073,
        0x40000080, 0x40000081, 0x40000082, 0x40000083, 0x40000084,
        0x40000090, 0x40000091, 0x40000092, 0x40000093,
        0x40000094, 0x40000095, 0x40000096, 0x40000097,
        0x400000B0, 0x400000B1, 0x400000B2, 0x400000B3,
        0x400000B4, 0x400000B5, 0x400000B6, 0x400000B7,
        0x400000D0, 0x400000D1, 0x400000D2, 0x400000D3,
        0x400000E0, 0x400000E1, 0x400000E2, 0x400000E3,
        0x400000F0, 0x400000F1, 0x400000F2, 0x400000F3,
        0x40000100, 0x40000101, 0x40000102, 0x40000103,
        0x40000104, 0x40000105,
    };
    for (int i = 0; i < (int)(sizeof(snapList) / sizeof(snapList[0])); i++) {
        UINT32 msr = snapList[i];
        UINT64 v   = 0;
        __try {
            v = __readmsr(msr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            v = 0;
        }
        g_HvMsrStore[msr & 0xFF] = v;
        DbgPrint("[HV-snap] 0x%08X = 0x%llX\n", msr, v);
    }
}

// Advance guest RIP past the instruction that caused the exit
static VOID SkipInstruction() {
    UINT64 rip    = VmcsRead(VMCS_GUEST_RIP);
    UINT64 len    = VmcsRead(VMCS_EXIT_INSTR_LEN);
    VmcsWrite(VMCS_GUEST_RIP, rip + len);
}

// (InjectGP() removed — tried injecting #GP on Hv MSR exits, caused 0x3D
//  INTERRUPT_EXCEPTION_NOT_HANDLED because the HAL has no SEH around
//  those writes.  Keeping swallow as the working-but-buggy default.)

// ============================================================
//  Exit handlers
// ============================================================

static VOID HandleCpuid(GuestRegisters* regs) {
    UINT32 leaf    = (UINT32)regs->RAX;
    UINT32 subleaf = (UINT32)regs->RCX;

    // Hypervisor leaves (0x40000000-0x400000FF): zero everything.
    // If we let the host __cpuidex run for these, nested VMware/Hyper-V
    // data leaks through and Windows configures its HAL to use Hv
    // synthetic-timer paths (HalpHvWatchdogArm) that expect SynIC
    // message slots we don't implement — causing NULL deref in the
    // clock interrupt path.
    if (leaf >= 0x40000000 && leaf <= 0x400000FF) {
        regs->RAX = 0;
        regs->RBX = 0;
        regs->RCX = 0;
        regs->RDX = 0;
        SkipInstruction();
        return;
    }

    int info[4] = {};
    __cpuidex(info, (int)leaf, (int)subleaf);

    // Clear Hypervisor-Present bit (CPUID.1:ECX[31]) so guest doesn't
    // probe hypervisor leaves at all.
    if (leaf == 1) {
        info[2] &= ~(1 << 31);
    }

    regs->RAX = (UINT64)(UINT32)info[0];
    regs->RBX = (UINT64)(UINT32)info[1];
    regs->RCX = (UINT64)(UINT32)info[2];
    regs->RDX = (UINT64)(UINT32)info[3];
    SkipInstruction();
}

// Set in Vmx.cpp before vmlaunch.
extern "C" BOOLEAN g_BareMetal;

static VOID HandleRdmsr(GuestRegisters* regs) {
    UINT32 msr = (UINT32)regs->RCX;
    UINT64 val = 0;
    const char* tag = "OTHER";

    if (msr >= 0x40000000 && msr <= 0x400003FF) {
        // Hyper-V synthetic MSRs.  Some of these (STIMER counts, reference
        // TSC, nested-virt MSRs like 0x400000F0) expect LIVE values from the
        // host Hv emulation that continue to advance.  Returning a cached
        // zero freezes the guest in a polling loop (observed on VMware).
        //
        // BARE METAL caveat: those MSRs don't exist on the CPU.  __readmsr
        // raises #GP, and the __try guard CANNOT catch it here because
        // VM-exit handlers run on the per-core host stack — the kernel's
        // SEH dispatcher rejects non-thread stacks and bugchecks 0x1AA
        // (EXCEPTION_ON_INVALID_STACK) instead of unwinding.  So on bare
        // metal we always serve from cache (zero by default) and never
        // attempt the live read.  The first time the guest reads
        // 0x40000000 on bare metal post-vmlaunch will hit this path —
        // returning 0 is correct because no Hv emulation actually exists.
        tag = "HV";
        if (msr == 0x40000002) {
            val = KeGetCurrentProcessorNumberEx(nullptr);
        } else if (msr == 0x400000F0 || msr == 0x400000F1 ||
                   msr == 0x400000F2 || msr == 0x400000F3) {
            // Unknown Hv MSRs that Windows polls in a tight loop on VMware.
            // Both host passthrough and our cache return 0, causing a hang.
            // Hand back a TSC-derived value so any "has this changed?" check
            // sees motion and the loop exits.
            val = __rdtsc();
        } else if (g_BareMetal) {
            // No live Hv emulation under us — cached store is the only sane
            // value.  Will be 0 unless a prior WRMSR populated this slot.
            val = g_HvMsrStore[msr & 0xFF];
        } else {
            __try {
                val = __readmsr(msr);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                val = g_HvMsrStore[msr & 0xFF];
            }
        }
    } else if (msr <= 0x00001FFF || (msr >= 0xC0000000 && msr <= 0xC0001FFF)) {
        // Architectural MSR ranges on every Intel VT-x host.
        tag = "ARCH";
        // Only passthrough MSRs in the 0xC0000000-0xC0001FFF range
        // (EFER, STAR, LSTAR, FS/GS_BASE, etc.) — these exist on every
        // x64 CPU.  MSRs in 0x0000-0x1FFF are model-specific; a #GP here
        // kills the VM-exit host stack with no SEH recovery possible.
        if (msr >= 0xC0000000) {
            val = __readmsr(msr);
        }
        else {
            val = 0;
        }
        // Stealth: mask VMX-enabled bits so guest tools like vmware-detect
        // can't read our own bit from IA32_FEATURE_CONTROL.
        if (msr == 0x3A) {
            val &= ~((UINT64)0x4 | (UINT64)0x8000);  // clear VMXON-in-SMX / VMXON
        }
        // Report SMM / DMA protection as untouched — if needed we can spoof.
    } else {
        tag = "UNKNOWN";
    }

    // Log first N reads regardless of range so we see every probe.
    LONG n = InterlockedIncrement(&g_HvMsrLogCount);
    if (n <= HV_MSR_LOG_LIMIT) {
        DbgPrint("[RD:%s] 0x%08X -> 0x%llX  (rip=0x%llX)\n",
                 tag, msr, val, VmcsRead(VMCS_GUEST_RIP));
    }

    regs->RAX = val & 0xFFFFFFFF;
    regs->RDX = val >> 32;
    SkipInstruction();
}

static VOID HandleWrmsr(GuestRegisters* regs) {
    UINT32 msr = (UINT32)regs->RCX;
    UINT64 val = (regs->RAX & 0xFFFFFFFF) | (regs->RDX << 32);

    if (msr >= 0x40000000 && msr <= 0x400000FF) {
        // Store Hv MSR value so subsequent reads return what was written.
        // The HAL's HalpHvWatchdogArm path reads back MSRs it wrote a moment
        // earlier; returning 0 caused NULL-deref crashes.
        g_HvMsrStore[msr & 0xFF] = val;
        LONG n = InterlockedIncrement(&g_HvMsrLogCount);
        if (n <= HV_MSR_LOG_LIMIT) {
            DbgPrint("[HV] WRMSR 0x%X = 0x%llX  (rip=0x%llX)\n",
                     msr, val, VmcsRead(VMCS_GUEST_RIP));
        }
    } else {
        // Other MSRs we don't have a bitmap trap for — log once, swallow.
        DbgPrint("[VMX] WRMSR 0x%X = 0x%llX (swallowed)\n", msr, val);
    }
    SkipInstruction();
}

// ============================================================
//  HandleIoInstruction — VMware-backdoor (port 0x5658/0x5659) stealth.
//
//  We trap all IO on these ports via the IO bitmap.  If the calling
//  process is a known VMware Tools helper, we passthrough — Tools keeps
//  working.  Otherwise we synthesise a "nothing responded" result:
//    IN  → RAX set to 0xFFFFFFFF
//    OUT → dropped silently
//  This defeats EAC's VMware-detect probe (write magic, read response,
//  compare against VMware signature) while keeping clipboard / shared
//  folders / drag-drop functional.
// ============================================================
extern "C" PCHAR PsGetProcessImageFileName(PEPROCESS Process);

static BOOLEAN CurrentProcessIsVMwareTools() {
    PEPROCESS proc = PsGetCurrentProcess();
    if (!proc) return FALSE;
    PCHAR name = PsGetProcessImageFileName(proc);
    if (!name) return FALSE;
    // EPROCESS.ImageFileName is a 15-byte field (+ NUL terminator).
    // Compare lowercase against the known VMware Tools binaries.
    static const char* allowed[] = {
        "vmtoolsd.exe",
        "vmwaretray.exe",
        "vmacthlp.exe",
        "vm3dservice.exe",
        "vgauthservice.",  // 15-char truncation of "VGAuthService.exe"
        "vmwareresoluti",  // 15-char truncation of "VMwareResolutionSet.exe"
    };
    // Build a lowercase copy of the 15-byte name
    char lower[16] = {};
    for (int i = 0; i < 15; i++) {
        char c = name[i]; if (!c) break;
        if (c >= 'A' && c <= 'Z') c += 32;
        lower[i] = c;
    }
    for (const char* s : allowed) {
        SIZE_T n = 0; while (s[n]) n++;
        if (n > 15) n = 15;
        bool match = true;
        for (SIZE_T i = 0; i < n; i++) if (lower[i] != s[i]) { match = false; break; }
        if (match) return TRUE;
    }
    return FALSE;
}

static VOID HandleIoInstruction(GuestRegisters* regs) {
    UINT64 qual = VmcsRead(VMCS_EXIT_QUAL);
    UINT32 size      = (UINT32)(qual & 0x7) + 1;   // 1, 2, or 4 bytes
    BOOLEAN isIn     = (qual & (1ULL << 3)) != 0;
    BOOLEAN isString = (qual & (1ULL << 4)) != 0;
    BOOLEAN isRep    = (qual & (1ULL << 5)) != 0;
    UINT16  port     = (UINT16)((qual >> 16) & 0xFFFF);

    if (port != 0x5658 && port != 0x5659) {
        // Shouldn't happen — bitmap only traps these — but pass through anyway.
        SkipInstruction();
        return;
    }
    if (isString || isRep) {
        // REP OUTS / REP INS variants — drop silently.  VMware Tools
        // uses single-instruction form; EAC detect code does too.
        SkipInstruction();
        return;
    }

    BOOLEAN allow = CurrentProcessIsVMwareTools();
    if (allow) {
        // Passthrough to the real hardware port.
        if (isIn) {
            UINT32 val = 0;
            switch (size) {
                case 1: val = __inbyte(port); break;
                case 2: val = __inword(port); break;
                default: val = __indword(port); break;
            }
            regs->RAX = (regs->RAX & ~(UINT64)0xFFFFFFFF) | val;
        } else {
            UINT32 val = (UINT32)(regs->RAX & 0xFFFFFFFF);
            switch (size) {
                case 1: __outbyte(port, (UINT8)val); break;
                case 2: __outword(port, (UINT16)val); break;
                default: __outdword(port, val); break;
            }
        }
    } else {
        // Block — synthesize "no device" response.
        if (isIn) {
            regs->RAX = (regs->RAX & ~(UINT64)0xFFFFFFFF) | 0xFFFFFFFF;
        }
        // OUT dropped.
    }
    SkipInstruction();
}

static VOID HandleXsetbv(GuestRegisters* regs) {
    UINT32 xcr = (UINT32)regs->RCX;
    UINT64 val = (regs->RAX & 0xFFFFFFFF) | (regs->RDX << 32);

    // Only XCR0 (xcr==0) is architectural; other indices #GP.  Guest must
    // keep bit 0 (x87) set.  Anything the host doesn't support in XCR0 will
    // also #GP.  Mask val against the host's XCR0 capabilities to prevent
    // host #GP → triple fault.
    if (xcr == 0 && (val & 1)) {
        UINT64 hostMask = _xgetbv(0) | 1ULL;
        _xsetbv(0, val & hostMask);
    }
    SkipInstruction();
}

static VOID HandleCrAccess(GuestRegisters* regs) {
    // Exit qualification encodes which CR and the access type
    UINT64 qual = VmcsRead(VMCS_EXIT_QUAL);
    UINT32 cr   = (UINT32)(qual & 0xF);
    UINT32 type = (UINT32)((qual >> 4) & 0x3);  // 0=MOV to CR, 1=MOV from CR
    UINT32 reg  = (UINT32)((qual >> 8) & 0xF);

    if (type == 0) {
        // MOV to CR — get value from the source register
        // Register encoding: 0=RAX,1=RCX,2=RDX,3=RBX,4=RSP,5=RBP,6=RSI,7=RDI,8-15=R8-R15
        // Map to GuestRegisters offsets (see Vmx.h for layout)
        UINT64 val = 0;
        switch (reg) {
            case 0:  val = regs->RAX; break;
            case 1:  val = regs->RCX; break;
            case 2:  val = regs->RDX; break;
            case 3:  val = regs->RBX; break;
            case 4:  val = VmcsRead(VMCS_GUEST_RSP); break;
            case 5:  val = regs->RBP; break;
            case 6:  val = regs->RSI; break;
            case 7:  val = regs->RDI; break;
            case 8:  val = regs->R8;  break;
            case 9:  val = regs->R9;  break;
            case 10: val = regs->R10; break;
            case 11: val = regs->R11; break;
            case 12: val = regs->R12; break;
            case 13: val = regs->R13; break;
            case 14: val = regs->R14; break;
            case 15: val = regs->R15; break;
        }
        // IMPORTANT: update ONLY the VMCS guest field.  __writecrN would modify
        // the host's CR in VMX root mode, which trashes host state.
        if (cr == 0) { VmcsWrite(VMCS_GUEST_CR0, val); }
        if (cr == 3) { VmcsWrite(VMCS_GUEST_CR3, val); }
        if (cr == 4) { VmcsWrite(VMCS_GUEST_CR4, val); }
    }
    SkipInstruction();
}

static BOOLEAN HandleVmcall(GuestRegisters* regs) {
    UINT64 code = regs->RCX; // Hypercall code passed in RCX

    switch (code) {
    case HYPERCALL_PING:
        regs->RAX = 0xDEADBEEFCAFEBABE; // Acknowledge
        break;

    case HYPERCALL_VMXOFF:
        // Graceful VMXOFF-from-guest is hard to do correctly (must restore guest
        // CR3/RSP/RIP by hand after leaving VMX root).  We never unload the driver
        // while the hypervisor is running, so this codepath is not used.  Silently
        // acknowledge and return so the guest continues normally.
        regs->RAX = 0;
        break;

    default:
        regs->RAX = (UINT64)-1; // Unknown hypercall
        break;
    }

    SkipInstruction();
    return FALSE; // Always FALSE — VmExitStub always vmresumes, never returns.
}

static VOID HandleEptViolation(GuestRegisters* /*regs*/) {
    UINT64 qual     = VmcsRead(VMCS_EXIT_QUAL);
    UINT64 guestPA  = VmcsRead(VMCS_GUEST_PHYSICAL_ADDR);
    UINT64 guestRIP = VmcsRead(VMCS_GUEST_RIP);

    // Decryption-learner first: if the fault hit a page we've registered
    // for read-tracing, arm MTF and return to retry the instruction.
    // Only honor READ qualifications (bit 0).  Execute/write faults fall
    // through to the standard hook framework.
    if (qual & 0x1) {
        UINT32 cpuIdx = (UINT32)KeGetCurrentProcessorNumberEx(nullptr);
        if (EptTraceHandleViolation(guestPA, guestRIP, cpuIdx)) {
            UINT64 desc[2] = { EptGetEptp(), 0 };
            AsmInvept(1, desc);
            return;
        }
    }

    // Milestone 1: try hook framework first.  Returns TRUE if this
    // fault corresponds to a registered hook — handler has already
    // flipped the EPT entry; we must NOT advance RIP (the instruction
    // needs to re-execute, now seeing the correct view).
    if (EptHandleViolation(guestPA, qual)) {
        // Invalidate cached EPT mappings so the next access observes
        // the entry swap immediately.  Single-context flush (type 1).
        UINT64 desc[2] = { EptGetEptp(), 0 };
        AsmInvept(1, desc);
        return; // retry the same instruction
    }

    // Unexpected EPT violation — log and advance so the guest doesn't wedge.
    DbgPrint("[VMX] EPT violation (unhooked)! RIP=0x%llX GPA=0x%llX Qual=0x%llX\n",
             guestRIP, guestPA, qual);
    SkipInstruction();
}

// MTF (Monitor Trap Flag) exit — fires after the guest executes one instruction
// while we have MTF armed.  Route to the decryption learner if it owns the
// trap, otherwise disarm silently.
static VOID HandleMtfStep(GuestRegisters* /*regs*/) {
    UINT64  guestRIP = VmcsRead(VMCS_GUEST_RIP);
    UINT32  cpuIdx   = (UINT32)KeGetCurrentProcessorNumberEx(nullptr);

    (void)EptTraceHandleMtfStep(guestRIP, cpuIdx);
    // No RIP advance — the guest's RIP is already past the single-stepped
    // instruction.  Tracer internally handles clearing MTF when done.
}

// ============================================================
//  Main VM exit dispatcher — called from VmxAsm.asm
// ============================================================
extern "C"
BOOLEAN VmExitHandler(GuestRegisters* regs) {
    UINT32 reason = (UINT32)VmcsRead(VMCS_EXIT_REASON) & 0xFFFF;

    // Log the first 40 exits unconditionally so we can see the sequence
    // leading up to any crash.  If the triple fault is before ANY exit
    // dispatches, we'll see nothing; otherwise we'll know which exit
    // reason was last handled.
    static volatile LONG s_firstExits = 0;
    LONG n = InterlockedIncrement(&s_firstExits);
    if (n <= 40) {
        DbgPrint("[VMX] exit#%ld reason=%u rip=0x%llX\n",
                 n, reason, VmcsRead(VMCS_GUEST_RIP));
    }

    // Diagnostic: record that we reached this handler and what exit reason.
    UINT32 coreIdx = KeGetCurrentProcessorNumberEx(nullptr);
    if (coreIdx < MAX_LOGICAL_CORES) {
        g_VmExitCounter[coreIdx]++;
        g_VmExitLastReason[coreIdx] = reason;
    }

    // (Historical GDB-probe breadcrumb that used to stamp R15=reason and
    //  R14=guest_RIP was HERE — it was clobbering r14 which the Windows
    //  HAL relies on as a struct pointer in HalpTimerClockArm.  Every VM
    //  exit corrupted r14 → bugcheck 0xA with P1 pointing at whatever
    //  RIP the exit fired from.  Removed.  Use g_VmExitCounter /
    //  g_VmExitLastReason for diagnostics instead — they don't touch
    //  guest GPRs.)

    switch (reason) {
    case VMX_EXIT_CPUID:
        HandleCpuid(regs);
        break;

    case VMX_EXIT_RDMSR:
        HandleRdmsr(regs);
        break;

    case VMX_EXIT_WRMSR:
        HandleWrmsr(regs);
        break;

    case VMX_EXIT_IO_INSTR:
        HandleIoInstruction(regs);
        break;

    case VMX_EXIT_XSETBV:
        HandleXsetbv(regs);
        break;

    case VMX_EXIT_CR_ACCESS:
        HandleCrAccess(regs);
        break;

    case VMX_EXIT_VMCALL:
        return HandleVmcall(regs);

    case VMX_EXIT_EPT_VIOLATION:
        HandleEptViolation(regs);
        break;

    case VMX_EXIT_MTF:
        HandleMtfStep(regs);
        break;

    case VMX_EXIT_EPT_MISCONFIG:
        DbgPrint("[VMX] EPT misconfiguration at RIP=0x%llX\n", VmcsRead(VMCS_GUEST_RIP));
        SkipInstruction();
        break;

    case VMX_EXIT_INVD:
    case VMX_EXIT_WBINVD:
    case VMX_EXIT_INVPCID:
    case VMX_EXIT_MONITOR:
    case VMX_EXIT_MWAIT:
    case VMX_EXIT_PAUSE:
        SkipInstruction();
        break;

    case VMX_EXIT_RDTSC:
    case VMX_EXIT_RDTSCP: {
        // Stealth: return a TSC that subtracts a constant per-core offset to
        // mask out the VM-exit cycle cost, so rdtsc delta between two points
        // in guest code approximates native timing.  Monotonic per core.
        static volatile UINT64 s_tscOffset[8] = {0};
        UINT32 cpu = KeGetCurrentProcessorNumberEx(nullptr);
        UINT64 raw = __rdtsc();
        UINT64 off = s_tscOffset[cpu & 7];
        // On every exit we accumulate ~2000 cycles of "hidden" time.
        s_tscOffset[cpu & 7] = off + 2000;
        UINT64 shown = raw - off;
        regs->RAX = shown & 0xFFFFFFFF;
        regs->RDX = shown >> 32;
        if (reason == VMX_EXIT_RDTSCP) {
            regs->RCX = __readmsr(0xC0000103);  // IA32_TSC_AUX
        }
        SkipInstruction();
        break;
    }

    default:
        // Unknown exit reason — log it and ADVANCE RIP so the guest doesn't
        // re-execute the same instruction forever.  Without the skip, all cores
        // spin on the faulting instruction and the VM freezes.
        DbgPrint("[VMX] Unhandled exit reason %u at RIP=0x%llX (skipping)\n",
                 reason, VmcsRead(VMCS_GUEST_RIP));
        SkipInstruction();
        break;
    }

    return FALSE; // FALSE = continue with VMRESUME
}
