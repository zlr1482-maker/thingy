#pragma once
#include "Intel.h"

// ============================================================
//  Limits
// ============================================================
// Pool tag value chosen to mimic Windows Diagnostic Infrastructure
// allocations.  The 4-byte tag appears in poolmon scans and Driver
// Verifier output as "WdiH".  Identifier name kept as XENTYPE2_POOL_TAG
// to avoid touching every allocation site.
#define XENTYPE2_POOL_TAG       'HidW'  // "WdiH" little-endian
#define VMX_HOST_STACK_PAGES    8       // 8 * 4KB = 32KB per-core host stack
#define VMX_HOST_STACK_SIZE     (VMX_HOST_STACK_PAGES * PAGE_SIZE)
#define MAX_LOGICAL_CORES       64

// ============================================================
//  Guest register save area — pushed by VmxAsm.asm on every VM exit
//  Order MUST match the push sequence in VmxAsm.asm.
// ============================================================
struct GuestRegisters {
    UINT64 R15;
    UINT64 R14;
    UINT64 R13;
    UINT64 R12;
    UINT64 R11;
    UINT64 R10;
    UINT64 R9;
    UINT64 R8;
    UINT64 RDI;
    UINT64 RSI;
    UINT64 RBP;
    UINT64 _RspPlaceholder; // RSP at exit is in VMCS_GUEST_RSP
    UINT64 RBX;
    UINT64 RDX;
    UINT64 RCX;
    UINT64 RAX;
};

// ============================================================
//  Per-logical-core VMX state
// ============================================================
struct VmxCore {
    PVOID   VmxonRegion;        // Non-paged VA of VMXON region (4KB aligned)
    PVOID   VmcsRegion;         // Non-paged VA of VMCS region  (4KB aligned)
    PVOID   MsrBitmap;          // Non-paged VA of MSR bitmap   (4KB aligned)
    PVOID   IoBitmapA;          // Non-paged VA of IO bitmap A (ports 0x0000-0x7FFF)
    PVOID   IoBitmapB;          // Non-paged VA of IO bitmap B (ports 0x8000-0xFFFF)
    PVOID   HostStack;          // Non-paged VA of host stack base
    UINT64  HostStackTop;       // RSP value written into VMCS_HOST_RSP (top of stack)
    UINT64  VmxonPhys;          // Precomputed physical addr (cached at PASSIVE_LEVEL)
    UINT64  VmcsPhys;
    UINT64  MsrBitmapPhys;
    UINT64  IoBitmapAPhys;
    UINT64  IoBitmapBPhys;
    UINT32  CoreIndex;
    BOOLEAN Launched;
    BOOLEAN VmxEnabled;         // VMXON succeeded on this core
};

// ============================================================
//  Global VMX state — one instance, allocated in DriverEntry
// ============================================================
struct VmxGlobal {
    VmxCore  Cores[MAX_LOGICAL_CORES];
    UINT32   CoreCount;
    BOOLEAN  Initialized;
    UINT64   LastVmlaunchError;   // VMCS_VM_INSTR_ERROR from the last failed VMLAUNCH
    UINT32   LastFailedCore;      // Core index where the last VMLAUNCH failed
    UINT64   HostCr3;             // Captured at PASSIVE_LEVEL from a stable kernel thread;
                                  // used for VMCS_HOST_CR3 on every core to avoid per-IPI
                                  // thread CR3 becoming dangling if that thread's process dies.
};

extern VmxGlobal* g_Vmx;   // Allocated in Driver.cpp, freed in DriverUnload

// Per-core VM-exit counters written by VmExitStub before anything else happens.
// If VmxInit fails before VMLAUNCH these stay 0.  If VMLAUNCH succeeds and a VM
// exit fires, the counter increments.  Used for triple-fault diagnostics.
extern "C" volatile UINT64 g_VmExitCounter[MAX_LOGICAL_CORES];
extern "C" volatile UINT64 g_VmExitLastReason[MAX_LOGICAL_CORES];

// ============================================================
//  Public API
// ============================================================

// Check CPU support (CPUID + IA32_FEATURE_CONTROL)
BOOLEAN VmxCheckSupport();

// Initialize VMX on all logical processors
NTSTATUS VmxInit();

// Tear down VMX on all processors — called from DriverUnload
VOID VmxShutdown();

// Per-core init (called via IPI from VmxInit)
// Returns STATUS_SUCCESS or an error code
NTSTATUS VmxInitCore(VmxCore* core);

// Per-core shutdown (called via IPI from VmxShutdown)
VOID VmxShutdownCore(VmxCore* core);

// ============================================================
//  ASM stubs (defined in VmxAsm.asm)
// ============================================================
extern "C" {
    // Entry point for every VM exit — saves registers, calls VmExitHandler, vmresumes
    VOID VmExitStub();

    // Captures current RSP + sets VMCS_GUEST_RSP, sets VMCS_GUEST_RIP to resume point,
    // then executes VMLAUNCH.
    // Returns 0 if VMLAUNCH succeeded (we are now in guest mode).
    // Returns 1 if VMLAUNCH failed.
    UINT64 VmxLaunchAndEnter();

    // Label whose address is the guest resume point after a successful VMLAUNCH.
    // Write &VmxGuestResume to VMCS_GUEST_RIP before calling VmxLaunchAndEnter().
    extern UINT8 VmxGuestResume;

    // Segment register reads (no inline asm on x64 MSVC)
    UINT16 AsmGetCs();
    UINT16 AsmGetDs();
    UINT16 AsmGetEs();
    UINT16 AsmGetFs();
    UINT16 AsmGetGs();
    UINT16 AsmGetSs();
    UINT16 AsmGetLdtr();
    UINT16 AsmGetTr();
    UINT64 AsmGetRflags();
    VOID   AsmGetGdtr(GdtRegister* gdt);
    VOID   AsmGetIdtr(IdtRegister* idt);
    UINT64 AsmGetSegmentBase(UINT64 gdtBase, UINT16 selector);
    UINT32 AsmGetSegmentLimit(UINT16 selector);

    // Issue VMCALL (used from guest to trigger a hypercall VM exit)
    VOID AsmVmcall(UINT64 code, UINT64 arg1, UINT64 arg2, UINT64 arg3);
}
