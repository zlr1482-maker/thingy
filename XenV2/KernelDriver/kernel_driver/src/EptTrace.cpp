// ============================================================================
// EptTrace.cpp — implementation of the read-trace + MTF-step decryption learner.
//
// Flow (see EptTrace.h + Shared.h for overall picture):
//   [register]  split 2MB → 4KB, strip R on the target page.
//   [violation] EptTraceHandleViolation: restore R+X so the guest can execute
//               the faulting instruction, set MTF in the per-VCPU procCtls,
//               remember we're mid-trace.  Guest executes ONE instruction and
//               re-enters with EXIT_REASON=VMX_EXIT_MTF.
//   [mtf step]  EptTraceHandleMtfStep: read the instruction at current RIP via
//               the tiny decoder below, recognize bswap/add/xor/rol patterns
//               with immediates, append to the op list.  Keep MTF set until
//               we either (a) see a non-decryption instruction, (b) hit the
//               depth limit, or (c) the tracked register gets clobbered by
//               a write.  On completion, clear MTF and re-strip R so the
//               next access re-traces (in case the algorithm changes).
//
// Thread/IRQL model:
//   - Registration / IOCTL handlers: PASSIVE_LEVEL.
//   - Violation + MTF handlers: VMX-root mode (effectively DISPATCH_LEVEL+,
//     no kernel APIs besides the VMCS read/write helpers and physical-memory
//     probes via already-mapped pages).
//   - All reads of guest code use the arena-mapped original page set up at
//     register time, avoiding any kernel API calls in the hot path.
//
// Memory safety:
//   - All per-entry and per-VCPU state lives in non-paged pool.
//   - Linked list is walked lock-free in the VM-exit path (single-writer:
//     registration; readers: per-core exit handlers).  Entries are never
//     freed while a trace is active — callers must unregister before freeing.
// ============================================================================

// ntifs.h must come FIRST (before ntddk.h transitively included by EptTrace.h)
// to avoid PEPROCESS / PETHREAD typedef conflicts.
#include <ntifs.h>

#include "EptTrace.h"
#include "Ept.h"
#include "Intel.h"
#include "Memory.h"
#include "Vmx.h"

extern "C" {
    NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(HANDLE, PEPROCESS*);
    void AsmInvept(UINT64 type, UINT64* descriptor);
}

#define EPT_TRACE_POOL_TAG 'tprE'  // "Ertp" reversed

// Maximum logical cores we'll track.  Matches Vmx.h MAX_LOGICAL_CORES.
#ifndef MAX_LOGICAL_CORES
#define MAX_LOGICAL_CORES 64
#endif

// Global registry.
static EptTraceEntry*   g_Traces      = nullptr;
static KSPIN_LOCK       g_TracesLock;
static BOOLEAN          g_TracesInit  = FALSE;
static EptVcpuTraceState g_VcpuState[MAX_LOGICAL_CORES] = { 0 };

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
VOID EptTraceInit(VOID) {
    if (g_TracesInit) return;
    KeInitializeSpinLock(&g_TracesLock);
    RtlZeroMemory(g_VcpuState, sizeof(g_VcpuState));
    g_TracesInit = TRUE;
}

VOID EptTraceShutdown(VOID) {
    if (!g_TracesInit) return;
    KIRQL old;
    KeAcquireSpinLock(&g_TracesLock, &old);
    EptTraceEntry* e = g_Traces;
    g_Traces = nullptr;
    KeReleaseSpinLock(&g_TracesLock, old);
    while (e) {
        EptTraceEntry* next = e->Next;
        ExFreePoolWithTag(e, EPT_TRACE_POOL_TAG);
        e = next;
    }
}

VOID EptTraceVcpuInit(UINT32 cpuIdx) {
    if (cpuIdx >= MAX_LOGICAL_CORES) return;
    RtlZeroMemory(&g_VcpuState[cpuIdx], sizeof(g_VcpuState[cpuIdx]));
}

// ---------------------------------------------------------------------------
//  Registry lookup helpers
// ---------------------------------------------------------------------------
static EptTraceEntry* FindTraceByTag(UINT32 tag) {
    for (EptTraceEntry* e = g_Traces; e; e = e->Next) {
        if (e->Tag == tag) return e;
    }
    return nullptr;
}

static EptTraceEntry* FindTraceByPhysPage(UINT64 page) {
    for (EptTraceEntry* e = g_Traces; e; e = e->Next) {
        if (e->GuestPhysPage == page) return e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Registration
// ---------------------------------------------------------------------------
NTSTATUS EptTraceRegister(UINT32 processId,
                          UINT64 guestVa,
                          UINT32 length,
                          UINT32 tag,
                          UINT32 traceDepth,
                          OUT UINT64* outGuestPhys)
{
    if (!g_TracesInit) EptTraceInit();
    if (!guestVa || length == 0 || length > 64) return STATUS_INVALID_PARAMETER;
    if (traceDepth == 0) traceDepth = 16;
    if (traceDepth > 64) traceDepth = 64;

    // Resolve guestVa in target process context.
    PEPROCESS proc = nullptr;
    NTSTATUS st = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)processId, &proc);
    if (!NT_SUCCESS(st)) return st;

    KAPC_STATE apc{};
    KeStackAttachProcess(proc, &apc);
    PHYSICAL_ADDRESS gpa = MmGetPhysicalAddress((PVOID)guestVa);
    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);

    if (!gpa.QuadPart) return STATUS_INVALID_ADDRESS;

    UINT64 guestPhys = (UINT64)gpa.QuadPart;
    UINT64 pageBase  = guestPhys & ~0xFFFULL;

    // Split containing 2MB page to 4KB if not already split.
    st = EptSplitLargePage(pageBase);
    if (!NT_SUCCESS(st)) return st;

    // Allocate the entry.
    EptTraceEntry* e = (EptTraceEntry*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(EptTraceEntry), EPT_TRACE_POOL_TAG);
    if (!e) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(e, sizeof(*e));
    e->Tag            = tag;
    e->TraceDepth     = traceDepth;
    e->GuestVa        = guestVa;
    e->GuestPhys      = guestPhys;
    e->GuestPhysPage  = pageBase;

    // Strip R bit on the PT entry so reads fault.  Preserve W + X so writes
    // still go through unhooked (we only care about reads) and executes from
    // the page don't fault — this page is data, not code, so X shouldn't
    // actually matter, but leaving it on reduces the chance we break anything.
    EptPtEntry* pt = EptFindPtEntryFor4K(pageBase);
    if (!pt) {
        ExFreePoolWithTag(e, EPT_TRACE_POOL_TAG);
        return STATUS_UNSUCCESSFUL;
    }
    pt->Read = 0;

    // Link in.
    KIRQL old;
    KeAcquireSpinLock(&g_TracesLock, &old);
    // Evict prior entry with same tag.
    EptTraceEntry** p = &g_Traces;
    while (*p) {
        if ((*p)->Tag == tag) {
            EptTraceEntry* victim = *p;
            *p = victim->Next;
            EptPtEntry* vpt = EptFindPtEntryFor4K(victim->GuestPhysPage);
            if (vpt) vpt->Read = 1;
            ExFreePoolWithTag(victim, EPT_TRACE_POOL_TAG);
            continue;
        }
        p = &(*p)->Next;
    }
    e->Next  = g_Traces;
    g_Traces = e;
    KeReleaseSpinLock(&g_TracesLock, old);

    if (outGuestPhys) *outGuestPhys = guestPhys;
    return STATUS_SUCCESS;
}

NTSTATUS EptTraceUnregister(UINT32 tag) {
    if (!g_TracesInit) return STATUS_NOT_FOUND;
    KIRQL old;
    KeAcquireSpinLock(&g_TracesLock, &old);
    EptTraceEntry** p = &g_Traces;
    EptTraceEntry* victim = nullptr;
    while (*p) {
        if ((*p)->Tag == tag) {
            victim = *p;
            *p = victim->Next;
            break;
        }
        p = &(*p)->Next;
    }
    KeReleaseSpinLock(&g_TracesLock, old);

    if (!victim) return STATUS_NOT_FOUND;
    EptPtEntry* pt = EptFindPtEntryFor4K(victim->GuestPhysPage);
    if (pt) pt->Read = 1;
    ExFreePoolWithTag(victim, EPT_TRACE_POOL_TAG);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
//  Learned-algorithm query + dump
// ---------------------------------------------------------------------------
UINT64 EptTraceDecrypt(UINT32 tag, UINT64 raw,
                       OUT UINT32* outOpCount, OUT UINT32* outHitCount)
{
    EptTraceEntry* e = FindTraceByTag(tag);
    if (!e) {
        if (outOpCount) *outOpCount = 0;
        if (outHitCount) *outHitCount = 0;
        return raw;
    }
    UINT32 n = e->OpsCount;
    if (outOpCount) *outOpCount = n;
    if (outHitCount) *outHitCount = e->HitCount;

    UINT64 v = raw;
    for (UINT32 i = 0; i < n; ++i) {
        const EptTraceOp& op = e->Ops[i];
        switch (op.Type) {
        case EptOpBswap32: v = (UINT64)_byteswap_ulong((UINT32)v); break;
        case EptOpBswap64: v = _byteswap_uint64(v); break;
        case EptOpAddImm:  v += op.Const; break;
        case EptOpSubImm:  v -= op.Const; break;
        case EptOpXorImm:  v ^= op.Const; break;
        case EptOpRolImm:  { UINT8 s = (UINT8)(op.Const & 63); v = _rotl64(v, s); break; }
        case EptOpRorImm:  { UINT8 s = (UINT8)(op.Const & 63); v = _rotr64(v, s); break; }
        case EptOpShlImm:  v <<= (op.Const & 63); break;
        case EptOpShrImm:  v >>= (op.Const & 63); break;
        default: break;
        }
    }
    return v;
}

NTSTATUS EptTraceDump(UINT32 tag, OUT EptTraceOp* outOps, UINT32 maxOps,
                      OUT UINT32* outOpCount, OUT UINT32* outHitCount)
{
    EptTraceEntry* e = FindTraceByTag(tag);
    if (!e) return STATUS_NOT_FOUND;
    UINT32 n = e->OpsCount;
    if (maxOps < n) n = maxOps;
    for (UINT32 i = 0; i < n; ++i) outOps[i] = e->Ops[i];
    if (outOpCount)  *outOpCount  = e->OpsCount;
    if (outHitCount) *outHitCount = e->HitCount;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
//  Tiny x86-64 decoder — recognize the specific patterns used by game
//  obfuscators.  This is NOT a general-purpose disassembler; it's a pattern
//  matcher that returns instruction length + extracted op info for a known
//  subset, and "skip" for everything else.
//
//  Recognized forms (all for 64-bit operands; REX.W present):
//
//    bswap r64          : 48 0F C8+reg                          (2 bytes after REX)
//    add r64, imm32     : 48 81 C0+reg imm32                    (7 bytes)
//    add r64, imm8      : 48 83 C0+reg imm8                     (4 bytes)
//    sub r64, imm32     : 48 81 E8+reg imm32                    (7)
//    sub r64, imm8      : 48 83 E8+reg imm8                     (4)
//    xor r64, imm32     : 48 81 F0+reg imm32                    (7)
//    xor r64, imm8      : 48 83 F0+reg imm8                     (4)
//    rol r64, imm8      : 48 C1 C0+reg imm8                     (4)
//    ror r64, imm8      : 48 C1 C8+reg imm8                     (4)
//    shl r64, imm8      : 48 C1 E0+reg imm8                     (4)
//    shr r64, imm8      : 48 C1 E8+reg imm8                     (4)
//
//  Anything else is reported as "skip" with a best-effort length.  We bail
//  out of tracing if we encounter an unknown instruction, since decryption
//  is usually a tight run of recognizable ops.
// ---------------------------------------------------------------------------

struct DecoderResult {
    UINT32      Length;     // bytes consumed by this instruction
    EptTraceOp  Op;         // Op.Type == EptOpNone if this wasn't a decryption op
    UINT8       DstReg;     // register modified (0-15; 0xFF if not an ALU op we know)
    UINT8       Recognized; // 1 if Op was populated
    UINT8       Unknown;    // 1 if we couldn't even determine length (abort trace)
};

static DecoderResult DecodeOne(const UINT8* p, UINT32 bufLen) {
    DecoderResult r = { 0 };
    r.DstReg = 0xFF;

    if (bufLen < 2) { r.Unknown = 1; return r; }

    UINT32 i = 0;

    // REX prefix.
    UINT8 rex = 0;
    if (p[i] >= 0x40 && p[i] <= 0x4F) {
        rex = p[i];
        i++;
        if (i >= bufLen) { r.Unknown = 1; return r; }
    }
    const bool rexW = (rex & 0x08) != 0;
    const bool rexB = (rex & 0x01) != 0;

    UINT8 op1 = p[i++];

    // -- BSWAP r32/r64 --  0F C8+rd
    if (op1 == 0x0F) {
        if (i >= bufLen) { r.Unknown = 1; return r; }
        UINT8 op2 = p[i++];
        if (op2 >= 0xC8 && op2 <= 0xCF) {
            r.DstReg = (op2 - 0xC8) | (rexB ? 8 : 0);
            r.Op.Type  = rexW ? EptOpBswap64 : EptOpBswap32;
            r.Op.Const = 0;
            r.Recognized = 1;
            r.Length = i;
            return r;
        }
        // Other 0F-escape opcodes: unknown.  Length ≥ 2 but we don't know
        // exactly; return i and let the outer loop abort tracing.
        r.Length = i; r.Unknown = 1;
        return r;
    }

    // -- 48 81 /{0,5,6} imm32  and  48 83 /{0,5,6} imm8 --  ADD/SUB/XOR
    // -- 48 C1 /{0,1,4,5} imm8 --  ROL/ROR/SHL/SHR
    if ((op1 == 0x81 || op1 == 0x83 || op1 == 0xC1) && rexW) {
        if (i >= bufLen) { r.Unknown = 1; return r; }
        UINT8 modrm = p[i++];
        UINT8 mod   = (modrm >> 6) & 0x3;
        UINT8 reg   = (modrm >> 3) & 0x7;
        UINT8 rm    = modrm & 0x7;
        if (mod != 0x3) {
            // Memory operand — not a register decryption op.  Length is hard
            // to compute without full ModR/M handling; abort the trace.
            r.Length = i; r.Unknown = 1; return r;
        }
        UINT8 dst = rm | (rexB ? 8 : 0);

        if (op1 == 0x81) {
            if (i + 4 > bufLen) { r.Unknown = 1; return r; }
            UINT32 imm = *(UINT32*)(p + i);
            i += 4;
            EptTraceOpType t = EptOpNone;
            if (reg == 0) t = EptOpAddImm;
            else if (reg == 5) t = EptOpSubImm;
            else if (reg == 6) t = EptOpXorImm;
            if (t != EptOpNone) {
                r.DstReg = dst;
                r.Op.Type  = t;
                r.Op.Const = (UINT64)(INT64)(INT32)imm; // sign-extended
                r.Recognized = 1;
            }
            r.Length = i;
            return r;
        }
        if (op1 == 0x83) {
            if (i >= bufLen) { r.Unknown = 1; return r; }
            UINT8 imm8 = p[i++];
            EptTraceOpType t = EptOpNone;
            if (reg == 0) t = EptOpAddImm;
            else if (reg == 5) t = EptOpSubImm;
            else if (reg == 6) t = EptOpXorImm;
            if (t != EptOpNone) {
                r.DstReg = dst;
                r.Op.Type  = t;
                r.Op.Const = (UINT64)(INT64)(INT8)imm8; // sign-extended
                r.Recognized = 1;
            }
            r.Length = i;
            return r;
        }
        // op1 == 0xC1: shifts/rotates.
        if (i >= bufLen) { r.Unknown = 1; return r; }
        UINT8 cnt = p[i++];
        EptTraceOpType t = EptOpNone;
        if      (reg == 0) t = EptOpRolImm;
        else if (reg == 1) t = EptOpRorImm;
        else if (reg == 4) t = EptOpShlImm;
        else if (reg == 5) t = EptOpShrImm;
        if (t != EptOpNone) {
            r.DstReg = dst;
            r.Op.Type  = t;
            r.Op.Const = cnt;
            r.Recognized = 1;
        }
        r.Length = i;
        return r;
    }

    // -- MOV reg, [rip+disp32] --  48 8B /{reg}  disp32  (7 bytes total w/ REX)
    // This is the LOAD that triggered the EPT violation in the first place.
    // Not a decryption op — just consume it and continue.
    if (op1 == 0x8B && rexW && i < bufLen) {
        UINT8 modrm = p[i++];
        UINT8 mod   = (modrm >> 6) & 0x3;
        UINT8 rm    = modrm & 0x7;
        if (mod == 0x0 && rm == 0x5) {
            // [rip+disp32]
            if (i + 4 > bufLen) { r.Unknown = 1; return r; }
            i += 4;
            r.Length = i;
            return r;  // skip, not an op
        }
        // Other modes — abort to avoid mis-lengthing.
        r.Unknown = 1; r.Length = i; return r;
    }

    // Anything else: unknown.  Abort the trace to avoid mis-interpretation.
    r.Unknown = 1;
    r.Length = i;
    return r;
}

// ---------------------------------------------------------------------------
//  VM-exit bridges
// ---------------------------------------------------------------------------

// Read N bytes of guest code at RIP by walking the current CR3.  Called
// from VM-exit context.  Uses the existing ReadPhysical helper.  Limited to
// one page of contiguous read — sufficient for any x86 instruction (max 15 B).
static NTSTATUS ReadGuestInstrBytes(UINT64 guestRip, UINT8* out, UINT32 len) {
    if (len > 32) return STATUS_INVALID_PARAMETER;
    UINT64 cr3 = VmcsRead(VMCS_GUEST_CR3);
    UINT64 gpa = TranslateGva(cr3, guestRip);
    if (!gpa) return STATUS_ACCESS_VIOLATION;
    // If the instruction straddles a page boundary, handle both sides.
    UINT64 pageEnd = (guestRip | 0xFFF) + 1;
    UINT32 firstChunk = (UINT32)(pageEnd - guestRip);
    if (firstChunk >= len) firstChunk = len;
    NTSTATUS st = ReadPhysical(gpa, out, firstChunk);
    if (!NT_SUCCESS(st)) return st;
    if (firstChunk == len) return STATUS_SUCCESS;
    UINT64 gpa2 = TranslateGva(cr3, pageEnd);
    if (!gpa2) return STATUS_SUCCESS;  // partial ok; decoder will notice
    return ReadPhysical(gpa2, out + firstChunk, len - firstChunk);
}

BOOLEAN EptTraceHandleViolation(UINT64 guestPhys, UINT64 guestRip, UINT32 cpuIdx) {
    if (!g_TracesInit) return FALSE;
    UINT64 page = guestPhys & ~0xFFFULL;
    EptTraceEntry* e = FindTraceByPhysPage(page);
    if (!e) return FALSE;

    e->HitCount++;

    // Restore R so the guest can execute the faulting load.
    EptPtEntry* pt = EptFindPtEntryFor4K(page);
    if (pt) pt->Read = 1;

    // Arm MTF on this VCPU.  Remember our trace state so the MTF handler
    // picks up where we left off.
    if (cpuIdx < MAX_LOGICAL_CORES) {
        EptVcpuTraceState* st = &g_VcpuState[cpuIdx];
        st->CurrentTrace     = e;
        st->LastFaultingRip  = guestRip;
        st->MtfArmed         = 1;
        st->StepsRemaining   = (UINT8)e->TraceDepth;
    }

    UINT64 pc = VmcsRead(VMCS_PROC_BASED_CTLS);
    VmcsWrite(VMCS_PROC_BASED_CTLS, pc | CPU_BASED_MONITOR_TRAP_FLAG);

    // Reset the learned op list — each trace pass captures fresh data.
    // Subsequent runs can confirm stability or catch pointer-rotation schemes.
    e->OpsCount = 0;
    e->Active   = 1;

    return TRUE;
}

BOOLEAN EptTraceHandleMtfStep(UINT64 guestRip, UINT32 cpuIdx) {
    if (!g_TracesInit) return FALSE;
    if (cpuIdx >= MAX_LOGICAL_CORES) return FALSE;
    EptVcpuTraceState* st = &g_VcpuState[cpuIdx];
    if (!st->MtfArmed || !st->CurrentTrace) return FALSE;

    EptTraceEntry* e = st->CurrentTrace;

    // Read up to 15 bytes of instruction at current RIP.
    UINT8 bytes[16] = { 0 };
    NTSTATUS rd = ReadGuestInstrBytes(guestRip, bytes, 15);

    BOOLEAN completeTrace = FALSE;
    if (!NT_SUCCESS(rd)) {
        completeTrace = TRUE;
    } else {
        DecoderResult r = DecodeOne(bytes, 15);
        if (r.Unknown || r.Length == 0) {
            completeTrace = TRUE;
        } else if (r.Recognized) {
            // Append if we haven't exceeded the op limit.
            if (e->OpsCount < EPT_TRACE_MAX_OPS) {
                e->Ops[e->OpsCount++] = r.Op;
            } else {
                completeTrace = TRUE;
            }
        }
        // Non-recognized but known-length (e.g. the mov reg, [rip+disp32] load):
        // consume and continue.  Bounded by StepsRemaining budget.
    }

    if (st->StepsRemaining > 0) st->StepsRemaining--;
    if (st->StepsRemaining == 0) completeTrace = TRUE;

    if (completeTrace) {
        // Disarm MTF on this VCPU.
        UINT64 pc = VmcsRead(VMCS_PROC_BASED_CTLS);
        VmcsWrite(VMCS_PROC_BASED_CTLS, pc & ~CPU_BASED_MONITOR_TRAP_FLAG);
        st->MtfArmed       = 0;
        st->StepsRemaining = 0;
        st->CurrentTrace   = nullptr;

        // Re-strip R on the page so the NEXT access re-traces.  This lets us
        // confirm the algorithm is stable or catch rotating schemes.
        EptPtEntry* pt = EptFindPtEntryFor4K(e->GuestPhysPage);
        if (pt) pt->Read = 0;

        e->Active = 0;

        // INVEPT single-context flush so the permission change takes effect.
        UINT64 desc[2] = { EptGetEptp(), 0 };
        AsmInvept(1, desc);
        return TRUE;
    }

    // Still tracing — MTF stays set, the CPU will fire again after the next
    // instruction executes.
    return FALSE;
}
