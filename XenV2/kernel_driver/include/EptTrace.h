// ============================================================================
// EptTrace.h — read-trace + MTF single-step infrastructure for learning
// guest-side decryption algorithms applied to encrypted global data.
//
// Strategy (see Shared.h IOCTL_EPT_TRACE_REGISTER comment for full overview):
//   1. Mark the target page execute-only via EPT (strip R bit).
//   2. Guest read → EPT violation → driver records RIP, restores R, sets MTF.
//   3. Guest executes the one read instruction; MTF fires → disassemble N
//      following instructions → extract bswap/xor/add/rol with constants.
//   4. Store the captured op sequence in a shadow map keyed by the caller's
//      Tag.  Usermode can then apply the algorithm to any raw value to get
//      plaintext without invoking another VM-exit.
//
// Stock UE games produce an empty op sequence (no decryption), so the feature
// is safely a no-op on non-obfuscated builds.
// ============================================================================
#pragma once

// Caller is expected to have included <ntifs.h> (or <ntddk.h>) already.
// We avoid pulling either in here to sidestep the PEPROCESS/PETHREAD
// redefinition conflict that occurs when both are included by a TU.

// Maximum decryption ops captured per tracked RVA.  Fortnite-style schemes
// are typically 2-6 ops (bswap + add, or xor + rol + add).  32 is a generous
// cap that covers any plausible algorithm while bounding memory.
#define EPT_TRACE_MAX_OPS          32

// Types of decryption ops we can recognize.  Matches Shared.h DumpResponse.
typedef enum _EptTraceOpType {
    EptOpNone      = 0,
    EptOpBswap32   = 1,
    EptOpBswap64   = 2,
    EptOpAddImm    = 3,
    EptOpSubImm    = 4,
    EptOpXorImm    = 5,
    EptOpRolImm    = 6,
    EptOpRorImm    = 7,
    EptOpShlImm    = 8,
    EptOpShrImm    = 9,
} EptTraceOpType;

typedef struct _EptTraceOp {
    UINT32 Type;      // EptTraceOpType
    UINT64 Const;     // immediate operand for ADD/SUB/XOR, shift count for ROL/ROR/SHL/SHR
} EptTraceOp;

// Per-tag state: the encrypted global we're tracking, plus the learned
// algorithm once MTF tracing completes at least one pass.
typedef struct _EptTraceEntry {
    struct _EptTraceEntry* Next;

    UINT32 Tag;
    UINT32 TraceDepth;        // # instructions to trace past access

    UINT64 GuestVa;
    UINT64 GuestPhys;
    UINT64 GuestPhysPage;     // GuestPhys & ~0xFFF

    UINT32 HitCount;          // # EPT violations seen on this tag
    UINT32 OpsCount;          // # recognized ops (0 until MTF pass completes)
    EptTraceOp Ops[EPT_TRACE_MAX_OPS];

    // Snapshot of the tracked register across MTF steps.  Populated in the
    // EPT-violation handler, consumed in the MTF handler.  One-shot per step.
    UINT8 TrackedGprIdx;      // GPR index (0-15, ModR/M reg field)
    UINT8 TraceBytesLeft;     // instructions remaining in the current trace
    UINT8 Active;             // 1 = currently tracing, 0 = idle
    UINT8 _pad;
} EptTraceEntry;

// Per-VCPU state: which tag (if any) is mid-trace, the instruction-length
// budget remaining, whether MTF is armed for our benefit vs. something else.
typedef struct _EptVcpuTraceState {
    EptTraceEntry* CurrentTrace;   // non-null during an MTF trace pass
    UINT64  LastFaultingRip;       // RIP at the EPT violation
    UINT8   MtfArmed;              // 1 = we set MTF for tracing; 0 = others
    UINT8   StepsRemaining;        // instructions left to disassemble this pass
    UINT8   _pad[6];
} EptVcpuTraceState;

// Lifecycle.
VOID EptTraceInit(VOID);
VOID EptTraceShutdown(VOID);

// Register a new read-trace.  Attaches to `processId` to resolve `guestVa` to
// guest-physical, then splits the containing 2MB EPT page to 4KB and strips
// the R bit so reads fault.
NTSTATUS EptTraceRegister(UINT32 processId,
                          UINT64 guestVa,
                          UINT32 length,
                          UINT32 tag,
                          UINT32 traceDepth,
                          OUT UINT64* outGuestPhys);

// Remove a registered trace (restores the EPT entry to R/W/X).
NTSTATUS EptTraceUnregister(UINT32 tag);

// Apply the learned algorithm for `tag` to `raw`, producing plaintext.
// If no algorithm was learned yet, returns `raw` unchanged.
UINT64 EptTraceDecrypt(UINT32 tag, UINT64 raw, OUT UINT32* outOpCount, OUT UINT32* outHitCount);

// Dump the learned op sequence for `tag` into caller-provided array.
NTSTATUS EptTraceDump(UINT32 tag, OUT EptTraceOp* outOps, UINT32 maxOps,
                      OUT UINT32* outOpCount, OUT UINT32* outHitCount);

// VM-exit bridge: called from HandleEptViolation when a fault matches a
// registered trace page.  Returns TRUE if handled (caller must re-execute).
BOOLEAN EptTraceHandleViolation(UINT64 guestPhys, UINT64 guestRip, UINT32 cpuIdx);

// VM-exit bridge: called from the MTF handler after the guest executed the
// single instruction that read the tracked global.  Disassembles and advances
// the trace state.  Returns TRUE when the trace completes (MTF should be
// disarmed).
BOOLEAN EptTraceHandleMtfStep(UINT64 guestRip, UINT32 cpuIdx);

// Called once per VCPU when VMX starts, to clear per-VCPU state.
VOID EptTraceVcpuInit(UINT32 cpuIdx);
