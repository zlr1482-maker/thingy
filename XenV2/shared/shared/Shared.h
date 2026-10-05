#pragma once

//
// Shared.h - Shared definitions between XenV2-Type2 kernel driver and usermode app
// Include this in both projects.
//

#ifdef _KERNEL_MODE
#include <ntdef.h>
#else
#include <Windows.h>
#endif

// ============================================================
//  Device path
//
//  The visible names in \Driver\, \Device\, and \DosDevices\ are
//  enumerable by anti-cheat kernel drivers (EAC, Vanguard, etc.).
//  We use an innocuous-looking name pattern that mimics ordinary
//  OEM/system services so it doesn't stand out in name-pattern
//  blocklists.  The string isn't security; it's camouflage.
// ============================================================
#define XENTYPE2_DEVICE_NAME    L"\\Device\\WdiSysHost1"
#define XENTYPE2_SYMLINK_NAME   L"\\DosDevices\\WdiSysHost1"
#define XENTYPE2_USERMODE_PATH  L"\\\\.\\WdiSysHost1"

// ============================================================
//  IOCTL codes
// ============================================================
#define XENTYPE2_IOCTL_TYPE  FILE_DEVICE_UNKNOWN

#define IOCTL_READ_PROCESS_MEMORY  \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_WRITE_PROCESS_MEMORY \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_MODULE_BASE      \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Batched read: collapses N single-target reads into one syscall.  Driver
// services every descriptor in the request inside a single
// KeStackAttachProcess scope, packing results contiguously into the
// output buffer.  See BatchReadRequest below.
#define IOCTL_BATCH_READ_PROCESS_MEMORY \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_PROCESS_CR3      \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_VMX_STATUS       \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VMX_START            \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VMX_STOP             \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Remote memory / thread ops — let user mode do a full manual-map + entry
// bootstrap without ever calling OpenProcess, VirtualAllocEx, WriteProcessMemory
// or CreateRemoteThread.  All four syscalls go through the driver instead,
// defeating the hooks most anti-cheats install on them.
#define IOCTL_ALLOC_REMOTE_MEMORY  \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_FREE_REMOTE_MEMORY   \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_PROTECT_REMOTE_MEMORY \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CREATE_REMOTE_THREAD \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)

// --- Kernel-mode memory ops (for DSE bypass / PatchGuard neuter) ---
#define IOCTL_KERNEL_READ          \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_KERNEL_WRITE         \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)

// --- Guest-kernel module / symbol discovery ---
#define IOCTL_DISCOVER_KMODULE     \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_DISCOVER_EXPORT      \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_DISCOVER_SIG         \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)

// One-shot "disable DSE": discover ci.dll, locate g_CiOptions, write 0.
// Optionally pass an explicit address in ExplicitAddr (nonzero overrides).
#define IOCTL_DSE_DISABLE          \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Activate deferred stealth measures.  Call AFTER the protected game is
// running and its anti-cheat init is complete.  Triggers, as applicable:
//   - Self-hide (PsLoadedModuleList unlink, PE scrub) if SCM-loaded
//   - RegFilter install                              if valid DriverObject
//   - (Future) other on-demand stealth
// Input:  none.  Output: ActivateResponse describing what ran.
#define IOCTL_ACTIVATE             \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Install an EPT split-view hook on nt!KeBugCheckEx with a single `ret`
// byte.  PatchGuard DPCs that would trigger a bugcheck return harmlessly.
// Requires VMX started.  Idempotent.
#define IOCTL_PG_NEUTER            \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)

// --- EPT read-trace: decryption algorithm extraction for obfuscated globals ---
//
// Architecture for decrypting externally-obfuscated UE data (e.g., Fortnite's
// GObjects / FUObjectItem.Object / FNamePool pointers):
//
//   1. IOCTL_EPT_TRACE_REGISTER: user mode passes a guest VA and length of
//      an encrypted global.  Driver resolves to guest-physical, splits the
//      containing 2MB EPT page to 4KB, clears the R bit so any guest read
//      triggers an EPT violation (instead of the existing exec-trap hooks).
//
//   2. On each such violation: driver records the faulting RIP, enables MTF
//      (Monitor Trap Flag) in CPU_BASED_VM_EXEC_CONTROL, restores R so the
//      guest can execute the load.  The CPU executes ONE instruction (the
//      read), then traps back with EXIT_REASON=VMX_EXIT_MTF.
//
//   3. In the MTF handler: disassemble forward N instructions from the new
//      RIP to catch the decryption sequence (bswap / xor / add / rol with
//      constants).  Record an ordered op list in a shadow map keyed by the
//      tracked RVA.
//
//   4. IOCTL_EPT_TRACE_QUERY: user mode asks "what's the plaintext value of
//      encrypted global at RVA X?"  Driver applies the learned algorithm to
//      the current raw value and returns the result.
//
// Stock UE games (no encryption) observe exactly zero decryption ops — the
// shadow map records an empty algorithm and IOCTL_EPT_TRACE_QUERY returns
// the raw value unchanged.  Universally safe.
//
// Detection envelope: adds EPT violations + MTF exits to the existing exit
// workload.  TSC smoother already handles per-exit cycle hiding; extending
// to the new exits is a one-line change.

#define IOCTL_EPT_TRACE_REGISTER   \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x850, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_EPT_TRACE_UNREGISTER \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x851, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_EPT_TRACE_QUERY      \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x852, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_EPT_TRACE_DUMP       \
    CTL_CODE(XENTYPE2_IOCTL_TYPE, 0x853, METHOD_BUFFERED, FILE_ANY_ACCESS)

// ============================================================
//  Request / Response structures
// ============================================================

#pragma pack(push, 1)

// --- Read process memory ---
// In:  ReadMemoryRequest  (fixed header)
// Out: ReadMemoryResponse (fixed header, data follows in same buffer)

struct ReadMemoryRequest {
    ULONG64 ProcessId;
    ULONG64 Address;        // Target virtual address
    ULONG64 Size;           // Bytes to read (must fit in output buffer)
};

struct ReadMemoryResponse {
    ULONG64 BytesRead;
    UCHAR   Buffer[1];      // Actual data starts here; caller allocates Size extra bytes
};

// --- Batch read (one syscall, N reads) ---
//
// Layout:
//   [BatchReadRequest header]
//   [BatchReadDesc descs[Count]]
// Output:
//   [BatchReadResponse header]
//   [BatchReadResult  results[Count]]
//   [packed result bytes — each desc's bytes start at result.OutOffset]
//
// OutOffset is measured from the start of the output buffer (i.e. it
// includes the response header + result table size).  The driver fills
// it in and the caller uses it to slice results out.
//
// Per-descriptor failure is signaled by Status != 0 + BytesRead = 0;
// the call as a whole returns NTSTATUS_SUCCESS even if some reads fail
// (so a single bad pointer doesn't poison the whole batch).
//
// Limits: Count <= XENTYPE2_BATCH_MAX (256).  Total payload (request +
// response) stays under 64 KB which fits comfortably in METHOD_BUFFERED.

#define XENTYPE2_BATCH_MAX 256

struct BatchReadDesc {
    ULONG64 Address;        // target VA
    ULONG32 Size;           // bytes requested
    ULONG32 _pad;
};

struct BatchReadRequest {
    ULONG64 ProcessId;
    ULONG32 Count;          // number of descriptors that follow
    ULONG32 _pad;
    // BatchReadDesc Descs[Count] follows immediately
};

struct BatchReadResult {
    ULONG32 OutOffset;      // offset into output buffer where this read's bytes live
    ULONG32 BytesRead;      // 0 on failure
    ULONG32 Status;         // 0 on success
    ULONG32 _pad;
};

struct BatchReadResponse {
    ULONG32 Count;          // mirrors request count
    ULONG32 _pad;
    // BatchReadResult Results[Count] follows immediately,
    // then a packed bytes region addressed by Results[i].OutOffset.
};

// --- Write process memory ---
// In:  WriteMemoryRequest (fixed header + inline Buffer)
// Out: WriteMemoryResponse

struct WriteMemoryRequest {
    ULONG64 ProcessId;
    ULONG64 Address;        // Target virtual address
    ULONG64 Size;           // Bytes to write
    UCHAR   Buffer[1];      // Data to write; caller sizes the struct accordingly
};

struct WriteMemoryResponse {
    ULONG64 BytesWritten;
};

// --- Get module base address ---
// In:  ModuleBaseRequest
// Out: ModuleBaseResponse

struct ModuleBaseRequest {
    ULONG64 ProcessId;
    WCHAR   ModuleName[64]; // e.g. L"game.exe" or L"engine.dll"
};

struct ModuleBaseResponse {
    ULONG64 BaseAddress;
    ULONG64 ModuleSize;
};

// --- Get process CR3 (DirectoryTableBase) ---
// In:  Cr3Request
// Out: Cr3Response

struct Cr3Request {
    ULONG64 ProcessId;
};

struct Cr3Response {
    ULONG64 Cr3Value;
};

// --- VMX status ---
// In:  (empty — just send the IOCTL)
// Out: VmxStatusResponse

struct VmxStatusResponse {
    BOOLEAN VmxInitialized;
    ULONG32 LogicalCoreCount;
    ULONG64 VmxBasicMsr;
    ULONG64 EptpValue;
    ULONG64 LastVmlaunchError;  // VMCS VM-instruction error field (0 = no error recorded)
    ULONG32 LastFailedCore;
    ULONG64 VmExitCount[8];     // per-core VM exit counter (max 8 cores reported)
    ULONG64 VmExitLastReason[8];
};

// --- Remote memory alloc / free / protect ---
//
// Driver attaches to the target process via KeStackAttachProcess and issues
// ZwAllocateVirtualMemory / ZwFreeVirtualMemory / ZwProtectVirtualMemory in
// that context.  No handle is ever exposed to user mode; the syscall that
// anti-cheats hook (NtOpenProcess) is never made on the attacker side.

struct AllocRemoteRequest {
    ULONG64 ProcessId;
    ULONG64 PreferredAddress; // 0 = let the kernel choose
    ULONG64 Size;
    ULONG32 Protection;       // PAGE_READWRITE, PAGE_EXECUTE_READWRITE, etc.
    ULONG32 _pad;
};
struct AllocRemoteResponse {
    ULONG64 Address;
    ULONG64 Size;             // Rounded-up actual size
};

struct FreeRemoteRequest {
    ULONG64 ProcessId;
    ULONG64 Address;
    ULONG64 Size;             // 0 = MEM_RELEASE the whole allocation
};

struct ProtectRemoteRequest {
    ULONG64 ProcessId;
    ULONG64 Address;
    ULONG64 Size;
    ULONG32 NewProtection;
    ULONG32 _pad;
};
struct ProtectRemoteResponse {
    ULONG32 OldProtection;
};

// --- Create remote thread via ZwCreateThreadEx in target's context ---
//
// CreateFlags = 0 runs immediately; THREAD_CREATE_FLAGS_CREATE_SUSPENDED (1)
// creates it suspended.

struct CreateThreadRequest {
    ULONG64 ProcessId;
    ULONG64 StartAddress;     // Target VA; usually our bootstrap shellcode
    ULONG64 Argument;
    ULONG32 CreateFlags;
    ULONG32 _pad;
};
struct CreateThreadResponse {
    ULONG64 ThreadId;
};

// --- Kernel read / write ---
struct KernelReadRequest {
    ULONG64 Address;
    ULONG64 Size;
};
struct KernelReadResponse {
    ULONG64 BytesRead;
    UCHAR   Buffer[1];  // Variable length
};
struct KernelWriteRequest {
    ULONG64 Address;
    ULONG64 Size;
    UCHAR   Buffer[1];  // Variable length
};

// --- Kernel module lookup ---
struct DiscoverKModuleRequest {
    WCHAR   BaseName[64];   // e.g. L"nt", L"ci"
};
struct DiscoverKModuleResponse {
    ULONG64 Base;
    ULONG64 Size;
};

struct DiscoverExportRequest {
    WCHAR   ModuleBaseName[64];
    CHAR    SymbolName[128];
};
struct DiscoverExportResponse {
    ULONG64 Address;
};

struct DiscoverSigRequest {
    WCHAR   ModuleBaseName[64];
    UCHAR   Pattern[64];
    CHAR    Mask[64];
    ULONG32 PatternLen;
};
struct DiscoverSigResponse {
    ULONG64 Address;
};

struct DseDisableRequest {
    ULONG64 ExplicitAddr;   // 0 = auto-discover
};
struct DseDisableResponse {
    ULONG64 GCiOptionsVa;   // address that was zeroed
    ULONG32 OriginalValue;  // pre-patch value
};

struct ActivateResponse {
    ULONG32 HideScrubs;       // # of list scrubs performed (0 if skipped)
    ULONG32 RegFilterStatus;  // NTSTATUS from RegFilterInstall, 0 if skipped
    ULONG32 Flags;            // bit0 = self-hide attempted, bit1 = regfilter attempted
    ULONG32 _pad;
};

// --- EPT read-trace ---
// Register an encrypted global for read-tracing.  After this call, any guest
// read of the range triggers an EPT violation → MTF single-step → algorithm
// learning.  Algorithm is stored in a driver-side shadow map keyed by Tag.
struct EptTraceRegisterRequest {
    ULONG64 ProcessId;        // target process (for CR3 attach when resolving VA)
    ULONG64 GuestVa;           // address of the encrypted value in the target's VA space
    ULONG32 Length;            // bytes covered (typically 8 for a pointer, up to 64)
    ULONG32 Tag;               // operator-chosen ID to query later (e.g. 1=GObjects, 2=FNamePool)
    ULONG32 TraceDepth;        // # instructions to trace past the access (default 16, max 64)
    ULONG32 _pad;
};
struct EptTraceRegisterResponse {
    ULONG64 GuestPhys;         // resolved guest-physical address of the tracked page
    ULONG32 Status;            // 0 = OK, nonzero = failure code
    ULONG32 _pad;
};

// Unregister a previously-registered trace (removes the EPT hook).
struct EptTraceUnregisterRequest {
    ULONG32 Tag;
    ULONG32 _pad;
};

// Query the learned decryption algorithm for a given Tag and compute plaintext
// from the current raw value.  If no algorithm was captured yet, returns raw.
struct EptTraceQueryRequest {
    ULONG32 Tag;
    ULONG32 _pad;
    ULONG64 RawValue;          // the encrypted 8-byte value the operator just read
};
struct EptTraceQueryResponse {
    ULONG64 PlaintextValue;    // result of applying the learned algorithm
    ULONG32 OpCount;           // # of decryption ops in the learned sequence
    ULONG32 HitCount;           // # of guest accesses observed on this Tag
};

// Dump the full learned algorithm for a Tag as a human-readable op list.
// Useful for diagnostics: "what did we observe the game doing to decrypt X?"
struct EptTraceDumpRequest {
    ULONG32 Tag;
    ULONG32 _pad;
};
struct EptTraceDumpResponse {
    ULONG32 OpCount;           // number of ops observed
    ULONG32 HitCount;
    // Each op: Type (1=BSWAP32 2=BSWAP64 3=ADD_IMM 4=SUB_IMM 5=XOR_IMM 6=ROL_IMM 7=ROR_IMM 8=SHL_IMM 9=SHR_IMM)
    // followed by the constant (int64 for ADD/SUB/XOR, uint8 for ROL/ROR/SHL/SHR, 0 for BSWAP).
    struct { ULONG32 Type; ULONG32 _pad; ULONG64 Const; } Ops[32];
};

#pragma pack(pop)
