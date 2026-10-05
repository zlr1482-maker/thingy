#pragma once
#include "Intel.h"

// ============================================================
//  EPT table sizes
// ============================================================
#define EPT_PML4_ENTRIES  512
#define EPT_PDPT_ENTRIES  512
#define EPT_PD_ENTRIES    512
#define EPT_PT_ENTRIES    512

// Physical address space we identity-map: first 512GB
// (One PML4 entry covers 512GB, which is enough for all practical RAM)
#define EPT_IDENTITY_MAP_SIZE_GB   512

// ============================================================
//  EPT state — one global instance
//
//  Side-arrays of table VAs are populated at EptInit time so the
//  walk in EptFindPdEntry doesn't rely on MmGetVirtualForPhysical
//  (which is documented-obsolete and returns wrong VAs on Win11 23H2+
//  for certain pool allocations, breaking EPT splitting/cloaking).
// ============================================================
struct EptState {
    EptPml4Entry*  Pml4;             // Non-paged, page-aligned allocation
    UINT64         Pml4Physical;     // Physical address of Pml4
    EptPointer     Eptp;             // EPTP value to write into VMCS

    // VA cache for the walk.  Indexed [pml4Idx][pdptIdx].  We only
    // populate Pml4Idx == 0 (covers 0..512GB of guest physical, which
    // is more than every plausible bare-metal RAM config).
    EptPdptEntry*  PdptVa[1];        // PdptVa[pml4Idx]
    EptPdEntry*    PdVa[512];        // PdVa[pdptIdx]  (only when Pml4Idx == 0)
};

extern EptState* g_Ept;

// ============================================================
//  Public API
// ============================================================

// Build a 2MB-granularity identity map for the first 512GB of physical space.
// Must be called before VmxInit().
NTSTATUS EptInit();

// Free all EPT allocations.
VOID EptShutdown();

// Return the EPTP value to write into VMCS_EPTP.
UINT64 EptGetEptp();

// Allocate one 4KB page of non-paged, zeroed memory and return its VA + PA.
// Used internally to build PDPT/PD/PT tables on demand.
PVOID EptAllocTable(UINT64* physOut);

// ============================================================
//  EPT split + hook framework  (Milestone 1)
// ============================================================
//
// To hook individual 4KB regions we must first split the containing 2MB
// large page into 512 4KB entries (Intel SDM §29.3.3).  Once split, we
// can give each 4KB page independent R/W/X permissions and point it at
// a shadow physical page containing patched code.
//
// The hook "split-view" technique:
//   - Allocate a shadow page, copy the original 4KB guest page into it,
//     apply a patch (e.g. `C3` ret at function prologue) at the target offset.
//   - Flip the EPT entry for the original guest physical page to
//     [R=1, W=1, X=0] → on execute, VM-exit fires.
//   - In the EPT-violation handler:
//       - If exit was caused by execute fetch: point the entry at the
//         SHADOW page + set X=1 R=0 W=0, invalidate TLB, resume.
//         Next instruction reads patched bytes.
//       - If exit was caused by read: point entry at ORIGINAL page +
//         set R=1 W=1 X=0, invalidate, resume.  PatchGuard / integrity
//         scanners see the clean bytes.
//   - On every VM-exit the page flips.  Continuous exec flips between
//     the two views based on access type.

// Public hook handle.  Caller keeps this opaque; pass back to EptHookRemove.
struct EptHook {
    UINT64  GuestPhysPage;   // Target guest physical page (4KB aligned)
    UINT64  ShadowPhys;      // Physical addr of shadow page
    PVOID   ShadowVa;        // Virtual addr of shadow page (in hypervisor)
    PVOID   OriginalVa;      // Mapped view of guest's original page
    UINT8   OriginalMemType; // Preserve source memory type
    BOOLEAN IsCloak;         // TRUE = cloak (shadow=zeros, exec=real, read=shadow);
                             // FALSE = patch (shadow=patched, exec=shadow, read=real)
    struct EptHook* Next;    // Linked list for global registry
};

// Split the 2MB large page covering `guestPhys` into 512 × 4KB PT entries.
// Returns pointer to the PT, or nullptr on allocation failure.  The new 4KB
// entries inherit full R/W/X from the parent large page.  Idempotent: if
// the region is already split, returns the existing PT.
NTSTATUS EptSplitLargePage(UINT64 guestPhys);

// Return the 4KB EPT PT entry covering `guestPhys`.  Requires the 2MB
// region to have been split.  Returns nullptr if not split or out of range.
// (EptPtEntry is defined in Intel.h, pulled in via the Intel.h include above.)
union EptPtEntry;
EptPtEntry* EptFindPtEntryFor4K(UINT64 guestPhys);

// Install a code-hook: shadow the 4KB page containing `guestVa` (in the
// current process context), apply `patchBytes` at the page-relative offset,
// mark the original EPT entry as non-executable.  Returns an EptHook* or
// nullptr on failure.  Must be called at PASSIVE_LEVEL.
EptHook* EptHookInstall(UINT64 guestVa, const void* patchBytes, SIZE_T patchLen);

// Remove a previously installed hook.  Restores the EPT entry and frees
// the shadow page.
VOID EptHookRemove(EptHook* hook);

// VM-exit dispatch for EPT violations / misconfigs.  Called from VmExit.cpp.
// Returns TRUE if the violation was handled (RIP advance not required;
// faulting instruction should be retried).
BOOLEAN EptHandleViolation(UINT64 guestPhys, UINT64 qualification);

// ============================================================
//  EPT cloak — hide our own driver image from kernel scanners
// ============================================================
//
// Cloaking inverts the patch-hook swap: the shadow holds zeros, foreign
// reads (RIP outside our image) are served from shadow → see zeros, exec
// fetches are served from the real page → our code runs normally.  Reads
// from RIP inside our own image (literal pools, jump tables) get the real
// bytes so we don't break ourselves.
//
// Call AFTER vmlaunch succeeds.  imageBase/imageSize come from the PE
// header walk in Driver.cpp (FindOwnImageBase + SizeOfImage).
NTSTATUS EptCloakImage(UINT64 imageBase, ULONG imageSize);

// Test whether a guest RIP falls in any registered cloak range.
// Used by the EPT-violation handler to decide whether to serve reads
// from real (our own code reading) vs shadow (foreign scanner).
BOOLEAN EptIsRipInCloakedRegion(UINT64 rip);
