/*
 * Ept.cpp - Extended Page Table (EPT) setup
 *
 * Builds a 2MB-granularity identity map over the first 512GB of physical
 * address space.  This is sufficient to cover all practical RAM on a
 * Windows 10/11 gaming machine.
 *
 * Layout:
 *   One PML4 (512 entries) →
 *     512 PDPTs (each covers 1GB) →
 *       Each PDPT entry points to a PD with 512 × 2MB large-page entries
 *
 * Memory type for each 2MB region comes from the MTRR-to-memory-type
 * heuristic: if the region is below 1MB, mark as UC; everything else WB.
 * A proper implementation would walk the MTRR MSRs — simplified here for
 * educational clarity.
 */

#include <ntifs.h>
#include "..\include\Ept.h"
#include "..\include\Vmx.h"

// ============================================================
//  EptAllocTable — allocate one 4KB zeroed non-paged page
// ============================================================
PVOID EptAllocTable(UINT64* physOut) {
    PVOID va = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, XENTYPE2_POOL_TAG);
    if (!va) return nullptr;
    RtlZeroMemory(va, PAGE_SIZE);
    if (physOut) {
        *physOut = MmGetPhysicalAddress(va).QuadPart;
    }
    return va;
}

// ============================================================
//  EptInit — build identity map
// ============================================================
NTSTATUS EptInit() {
    g_Ept = (EptState*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(EptState), XENTYPE2_POOL_TAG);
    if (!g_Ept) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_Ept, sizeof(EptState));

    // Allocate PML4 table
    UINT64 pml4Phys = 0;
    g_Ept->Pml4 = (EptPml4Entry*)EptAllocTable(&pml4Phys);
    if (!g_Ept->Pml4) {
        ExFreePoolWithTag(g_Ept, XENTYPE2_POOL_TAG);
        g_Ept = nullptr;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    g_Ept->Pml4Physical = pml4Phys;

    // Cover the full 512GB of the first PML4 slot (all 512 PDPT entries).
    // Total allocation: 1 PML4 + 1 PDPT + 512 PDs = 514 pages = ~2MB.
    // Covers all physical addresses any modern host would plausibly use,
    // including BIOS/ACPI/MMIO regions that may sit above typical RAM.
    const UINT32 MAX_PML4 = 1;
    const UINT32 MAX_PDPT_ENTRIES = 512;

    for (UINT32 pml4Idx = 0; pml4Idx < MAX_PML4; pml4Idx++) {

        // Allocate PDPT
        UINT64 pdptPhys = 0;
        EptPdptEntry* pdpt = (EptPdptEntry*)EptAllocTable(&pdptPhys);
        if (!pdpt) {
            break;
        }
        g_Ept->PdptVa[pml4Idx] = pdpt;   // Cache VA for fast walk later.

        // Wire PML4E → PDPT
        g_Ept->Pml4[pml4Idx].Value = 0;
        g_Ept->Pml4[pml4Idx].Read  = 1;
        g_Ept->Pml4[pml4Idx].Write = 1;
        g_Ept->Pml4[pml4Idx].Execute = 1;
        g_Ept->Pml4[pml4Idx].Pfn   = pdptPhys >> 12;

        // For each 1GB region (one PD per PDPT entry)
        for (UINT32 pdptIdx = 0; pdptIdx < MAX_PDPT_ENTRIES; pdptIdx++) {

            UINT64 pdPhys = 0;
            EptPdEntry* pd = (EptPdEntry*)EptAllocTable(&pdPhys);
            if (!pd) break;
            // Only PML4[0] is wired up; cache PD VAs for split/find.
            if (pml4Idx == 0) g_Ept->PdVa[pdptIdx] = pd;

            pdpt[pdptIdx].Value   = 0;
            pdpt[pdptIdx].Read    = 1;
            pdpt[pdptIdx].Write   = 1;
            pdpt[pdptIdx].Execute = 1;
            pdpt[pdptIdx].Pfn     = pdPhys >> 12;

            // Fill 512 × 2MB large-page entries for this 1GB region
            for (UINT32 pdIdx = 0; pdIdx < 512; pdIdx++) {
                UINT64 physAddr = ((UINT64)pml4Idx  << 39) |
                                  ((UINT64)pdptIdx  << 30) |
                                  ((UINT64)pdIdx    << 21);

                UINT8 memType = EPT_MT_WB;
                // Low 1MB is typically UC/WC — keep as UC to be safe
                if (physAddr < 0x100000ULL) {
                    memType = EPT_MT_UC;
                }

                pd[pdIdx].Value     = 0;
                pd[pdIdx].Read      = 1;
                pd[pdIdx].Write     = 1;
                pd[pdIdx].Execute   = 1;
                pd[pdIdx].LargePage = 1;  // 2MB page
                pd[pdIdx].MemType   = memType;
                pd[pdIdx].Pfn       = physAddr >> 21; // 2MB-aligned PFN
            }
        }
    }

    // Build EPTP: WB memory type (6), 4-level walk (3 = 4-1), access/dirty tracking off
    g_Ept->Eptp.Value       = 0;
    g_Ept->Eptp.MemType     = EPT_MT_WB;
    g_Ept->Eptp.PageWalkLen = 3;     // 4-level paging
    g_Ept->Eptp.AccessDirty = 0;
    g_Ept->Eptp.Pfn         = pml4Phys >> 12;

    DbgPrint("[EPT] Identity map built. EPTP=0x%llX\n", g_Ept->Eptp.Value);
    return STATUS_SUCCESS;
}

// ============================================================
//  EptShutdown — free all EPT allocations
// ============================================================
VOID EptShutdown() {
    if (!g_Ept) return;

    // Walk and free all allocated PD and PDPT tables
    if (g_Ept->Pml4) {
        for (UINT32 i = 0; i < 512; i++) {
            EptPml4Entry* pml4e = &g_Ept->Pml4[i];
            if (!pml4e->Read) continue;

            PHYSICAL_ADDRESS pdptPA;
            pdptPA.QuadPart = (LONGLONG)(pml4e->Pfn << 12);
            EptPdptEntry* pdpt = (EptPdptEntry*)MmGetVirtualForPhysical(pdptPA);

            if (pdpt) {
                for (UINT32 j = 0; j < 512; j++) {
                    EptPdptEntry* pdpte = &pdpt[j];
                    if (!pdpte->Read) continue;
                    PHYSICAL_ADDRESS pdPA;
                    pdPA.QuadPart = (LONGLONG)(pdpte->Pfn << 12);
                    EptPdEntry* pd = (EptPdEntry*)MmGetVirtualForPhysical(pdPA);
                    if (pd) ExFreePoolWithTag(pd, XENTYPE2_POOL_TAG);
                }
                ExFreePoolWithTag(pdpt, XENTYPE2_POOL_TAG);
            }
        }
        ExFreePoolWithTag(g_Ept->Pml4, XENTYPE2_POOL_TAG);
    }

    ExFreePoolWithTag(g_Ept, XENTYPE2_POOL_TAG);
    g_Ept = nullptr;
}

// ============================================================
//  EptGetEptp
// ============================================================
UINT64 EptGetEptp() {
    return g_Ept ? g_Ept->Eptp.Value : 0;
}

// ============================================================
//  Hooking framework
// ============================================================
//
// Walk the EPT PML4 → PDPT → PD to locate the PD entry that covers
// `guestPhys`.  Returns both the parent PD pointer and the index within
// it, so we can split the large page in-place.
static EptPdEntry* EptFindPdEntry(UINT64 guestPhys) {
    if (!g_Ept || !g_Ept->Pml4) return nullptr;

    UINT32 pml4Idx = (UINT32)((guestPhys >> 39) & 0x1FF);
    UINT32 pdptIdx = (UINT32)((guestPhys >> 30) & 0x1FF);
    UINT32 pdIdx   = (UINT32)((guestPhys >> 21) & 0x1FF);

    // We only allocated PML4[0] (covers 0..512GB GPA — every plausible
    // bare-metal RAM range).  Any GPA outside that is unmapped.
    if (pml4Idx != 0) return nullptr;

    EptPml4Entry* pml4e = &g_Ept->Pml4[pml4Idx];
    if (!pml4e->Read) return nullptr;

    // Use the cached VAs from EptInit instead of MmGetVirtualForPhysical
    // (which is obsolete and returns wrong VAs on Win11 23H2+).
    EptPdptEntry* pdpt = g_Ept->PdptVa[pml4Idx];
    if (!pdpt) return nullptr;
    EptPdptEntry* pdpte = &pdpt[pdptIdx];
    if (!pdpte->Read) return nullptr;

    EptPdEntry* pd = g_Ept->PdVa[pdptIdx];
    if (!pd) return nullptr;

    return &pd[pdIdx];
}

// Split a 2MB large-page EPT entry covering `guestPhys` into 512 4KB PT
// entries.  The new PT inherits the large-page's R/W/X and MemType.
// Idempotent if already split.
NTSTATUS EptSplitLargePage(UINT64 guestPhys) {
    EptPdEntry* pde = EptFindPdEntry(guestPhys);
    if (!pde) return STATUS_NOT_FOUND;

    // Already split? LargePage=0 means this PD entry points at a PT.
    if (!pde->LargePage) return STATUS_SUCCESS;

    // Allocate the 4KB PT.
    UINT64 ptPhys = 0;
    EptPtEntry* pt = (EptPtEntry*)EptAllocTable(&ptPhys);
    if (!pt) return STATUS_INSUFFICIENT_RESOURCES;

    // Cache the large-page attrs before we overwrite the entry.
    UINT8   memType = (UINT8)pde->MemType;
    UINT64  basePfn = pde->Pfn << 9;   // 2MB PFN → 4KB PFN base (×512)

    for (UINT32 i = 0; i < 512; i++) {
        EptPtEntry* e = &pt[i];
        e->Value   = 0;
        e->Read    = 1;
        e->Write   = 1;
        e->Execute = 1;
        e->MemType = memType;
        e->Pfn     = basePfn + i;
    }

    // Repoint the PD entry at the new PT.  LargePage=0 switches semantics.
    pde->Value     = 0;
    pde->Read      = 1;
    pde->Write     = 1;
    pde->Execute   = 1;
    pde->LargePage = 0;
    pde->Pfn       = ptPhys >> 12;

    return STATUS_SUCCESS;
}

EptPtEntry* EptFindPtEntryFor4K(UINT64 guestPhys) {
    EptPdEntry* pde = EptFindPdEntry(guestPhys);
    if (!pde || pde->LargePage) return nullptr;

    PHYSICAL_ADDRESS pa{};
    pa.QuadPart = (LONGLONG)(pde->Pfn << 12);
    EptPtEntry* pt = (EptPtEntry*)MmGetVirtualForPhysical(pa);
    if (!pt) return nullptr;

    UINT32 ptIdx = (UINT32)((guestPhys >> 12) & 0x1FF);
    return &pt[ptIdx];
}

// Global registry of active hooks (walked by the VM-exit handler).
static EptHook* g_HookList  = nullptr;
static KSPIN_LOCK g_HookLock;
static BOOLEAN  g_HookLockInit = FALSE;

// Cloak-range bounds.  Populated by EptCloakImage so the EPT-violation
// handler can decide whether a read came from inside our own image
// (serve real bytes) or from a foreign scanner (serve shadow zeros).
static UINT64 g_CloakRangeStart = 0;
static UINT64 g_CloakRangeEnd   = 0;

BOOLEAN EptIsRipInCloakedRegion(UINT64 rip) {
    return (g_CloakRangeStart != 0) &&
           (rip >= g_CloakRangeStart) &&
           (rip <  g_CloakRangeEnd);
}

static EptHook* FindHookByGuestPhysPage(UINT64 guestPhysPage) {
    for (EptHook* h = g_HookList; h; h = h->Next) {
        if (h->GuestPhysPage == guestPhysPage) return h;
    }
    return nullptr;
}

EptHook* EptHookInstall(UINT64 guestVa, const void* patchBytes, SIZE_T patchLen) {
    if (!guestVa || !patchBytes || patchLen == 0 || patchLen > PAGE_SIZE) return nullptr;

    if (!g_HookLockInit) {
        KeInitializeSpinLock(&g_HookLock);
        g_HookLockInit = TRUE;
    }

    // Resolve guestVa → guest physical (in the current process's CR3).
    PHYSICAL_ADDRESS gpa = MmGetPhysicalAddress((PVOID)guestVa);
    if (!gpa.QuadPart) return nullptr;
    UINT64 guestPhys = (UINT64)gpa.QuadPart;
    UINT64 pageBase  = guestPhys & ~0xFFFULL;
    UINT64 pageOff   = guestPhys &  0xFFFULL;
    if (pageOff + patchLen > PAGE_SIZE) return nullptr;

    // Ensure the containing 2MB large page is split.
    NTSTATUS st = EptSplitLargePage(pageBase);
    if (!NT_SUCCESS(st)) return nullptr;

    EptPtEntry* ptEntry = EptFindPtEntryFor4K(pageBase);
    if (!ptEntry) return nullptr;

    // Allocate the shadow page and populate it from the current guest contents.
    UINT64 shadowPhys = 0;
    PVOID  shadowVa   = EptAllocTable(&shadowPhys);
    if (!shadowVa) return nullptr;

    // Map guest's physical page so we can memcpy its current contents.
    PHYSICAL_ADDRESS pa{}; pa.QuadPart = (LONGLONG)pageBase;
    PVOID origView = MmMapIoSpaceEx(pa, PAGE_SIZE, PAGE_READWRITE);
    if (!origView) {
        ExFreePoolWithTag(shadowVa, XENTYPE2_POOL_TAG);
        return nullptr;
    }
    RtlCopyMemory(shadowVa, origView, PAGE_SIZE);

    // Apply the patch at the correct offset within the shadow page.
    RtlCopyMemory((UCHAR*)shadowVa + pageOff, patchBytes, patchLen);

    MmUnmapIoSpace(origView, PAGE_SIZE);

    // Allocate the hook record.
    EptHook* h = (EptHook*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(EptHook),
                                           XENTYPE2_POOL_TAG);
    if (!h) {
        ExFreePoolWithTag(shadowVa, XENTYPE2_POOL_TAG);
        return nullptr;
    }
    h->GuestPhysPage   = pageBase;
    h->ShadowPhys      = shadowPhys;
    h->ShadowVa        = shadowVa;
    h->OriginalVa      = nullptr;
    h->OriginalMemType = (UINT8)ptEntry->MemType;

    // Flip the EPT entry to R/W-only.  First execute hits EPT violation,
    // handler swaps in the shadow.
    ptEntry->Execute = 0;

    // Link into global list.
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_HookLock, &oldIrql);
    h->Next = g_HookList;
    g_HookList = h;
    KeReleaseSpinLock(&g_HookLock, oldIrql);

    // NOTE: caller must invalidate EPT (INVEPT) — done via VMX helper in
    // the caller or during next VM-exit.  For passively-installed hooks
    // before VMX start, no invalidation needed.
    return h;
}

VOID EptHookRemove(EptHook* hook) {
    if (!hook) return;

    // Unlink from list.
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_HookLock, &oldIrql);
    EptHook** p = &g_HookList;
    while (*p && *p != hook) p = &(*p)->Next;
    if (*p) *p = hook->Next;
    KeReleaseSpinLock(&g_HookLock, oldIrql);

    // Restore EPT entry: execute=1, PFN back to original.
    EptPtEntry* ptEntry = EptFindPtEntryFor4K(hook->GuestPhysPage);
    if (ptEntry) {
        ptEntry->Pfn     = hook->GuestPhysPage >> 12;
        ptEntry->Read    = 1;
        ptEntry->Write   = 1;
        ptEntry->Execute = 1;
        ptEntry->MemType = hook->OriginalMemType;
    }
    if (hook->ShadowVa) ExFreePoolWithTag(hook->ShadowVa, XENTYPE2_POOL_TAG);
    ExFreePoolWithTag(hook, XENTYPE2_POOL_TAG);
}

// VM-exit handler for EPT violations.
// Exit qualification bits (Intel SDM §28.2.1):
//   bit 0: data read
//   bit 1: data write
//   bit 2: instruction fetch
//
// Patch-hook semantics (h->IsCloak == FALSE):
//   exec fault    → swap in shadow page (X=1, R=0, W=0)
//   read/write    → swap in original    (R=1, W=1, X=0)
// (Shadow holds patched bytes; foreign scanners reading the page see the
//  unpatched original, our code at this RIP executes the patched shadow.)
//
// Cloak semantics (h->IsCloak == TRUE):
//   exec fault         → swap in real page (X=1, R=0, W=0)
//   read/write fault   → if RIP is inside the cloaked region: real (R/W)
//                        else (foreign scanner):                shadow zeros
// (Shadow holds zeros; our own data references see real, scanners see zero.)
BOOLEAN EptHandleViolation(UINT64 guestPhys, UINT64 qualification) {
    UINT64 page = guestPhys & ~0xFFFULL;
    EptHook* h = FindHookByGuestPhysPage(page);
    if (!h) return FALSE;

    EptPtEntry* ptEntry = EptFindPtEntryFor4K(page);
    if (!ptEntry) return FALSE;

    const BOOLEAN execFault = (qualification & 0x4) != 0;

    if (h->IsCloak) {
        if (execFault) {
            // Our own (or any) code wants to execute these bytes — give the
            // real page with execute-only.  Reads from another core during
            // this window will VM-exit and be served the shadow.
            ptEntry->Pfn     = page >> 12;
            ptEntry->Read    = 0;
            ptEntry->Write   = 0;
            ptEntry->Execute = 1;
        } else {
            UINT64 rip = VmcsRead(VMCS_GUEST_RIP);
            if (EptIsRipInCloakedRegion(rip)) {
                // Our own code is reading data on its own page (literal
                // pool / jump table / .rdata adjacent to .text).  Serve
                // real bytes so we don't corrupt our own state.
                ptEntry->Pfn     = page >> 12;
                ptEntry->Read    = 1;
                ptEntry->Write   = 1;
                ptEntry->Execute = 0;
            } else {
                // Foreign read — anti-cheat / kernel scanner.  Serve the
                // zero-filled shadow so they see nothing of value.
                ptEntry->Pfn     = h->ShadowPhys >> 12;
                ptEntry->Read    = 1;
                ptEntry->Write   = 1;
                ptEntry->Execute = 0;
            }
        }
    } else {
        // Patch-hook (existing behavior).
        if (execFault) {
            ptEntry->Pfn     = h->ShadowPhys >> 12;
            ptEntry->Read    = 0;
            ptEntry->Write   = 0;
            ptEntry->Execute = 1;
        } else {
            ptEntry->Pfn     = page >> 12;
            ptEntry->Read    = 1;
            ptEntry->Write   = 1;
            ptEntry->Execute = 0;
        }
    }
    // TLB invalidation is needed; we let the caller (VmExit.cpp) issue
    // INVEPT after us since it has the VMCS context.
    return TRUE;
}

// ============================================================
//  EPT cloak — install per-page cloak hooks across our own image
// ============================================================
//
// Walks every 4KB page in [imageBase, imageBase + imageSize), splits the
// containing 2MB regions, allocates a zero-filled shadow page, and sets
// the EPT leaf entry to point at the shadow with R/W=1, X=0 — so the
// initial state any scanner sees is zeros.  Our own code's first execute
// from a cloaked page will VM-exit; the handler swaps to real-X and we
// run.  Reads with RIP inside the cloak range get real bytes (handler
// branches on EptIsRipInCloakedRegion).
//
// Must be called AFTER vmlaunch.  Caller is expected to broadcast invept
// to all cores via IPI after this returns.
NTSTATUS EptCloakImage(UINT64 imageBase, ULONG imageSize) {
    if (!g_Ept || !g_Ept->Pml4) return STATUS_DEVICE_NOT_READY;
    if (!imageBase || !imageSize) return STATUS_INVALID_PARAMETER;
    if (!g_HookLockInit) {
        KeInitializeSpinLock(&g_HookLock);
        g_HookLockInit = TRUE;
    }

    const UINT64 startVa = imageBase & ~0xFFFULL;
    const UINT64 endVa   = (imageBase + imageSize + 0xFFFULL) & ~0xFFFULL;

    // Record the cloak range so the violation handler can distinguish
    // self-reads from foreign-reads (RIP-in-range check).
    g_CloakRangeStart = startVa;
    g_CloakRangeEnd   = endVa;

    ULONG cloakedCount   = 0;
    ULONG splitFailures  = 0;
    ULONG allocFailures  = 0;
    ULONG resolveFailures= 0;

    for (UINT64 va = startVa; va < endVa; va += PAGE_SIZE) {
        // VA → guest physical (we run in System context; kernel pool
        // pages have a stable phys mapping for our lifetime).
        PHYSICAL_ADDRESS gpa = MmGetPhysicalAddress((PVOID)va);
        if (!gpa.QuadPart) { resolveFailures++; continue; }
        UINT64 pageBase = (UINT64)gpa.QuadPart & ~0xFFFULL;

        // Skip if a hook (cloak or patch) is already on this physical
        // page — don't double-install.
        if (FindHookByGuestPhysPage(pageBase)) continue;

        // Split the containing 2MB large page (idempotent).
        NTSTATUS st = EptSplitLargePage(pageBase);
        if (!NT_SUCCESS(st)) { splitFailures++; continue; }

        EptPtEntry* ptEntry = EptFindPtEntryFor4K(pageBase);
        if (!ptEntry) { splitFailures++; continue; }

        // Allocate a 4KB zero-filled shadow.  EptAllocTable already
        // zero-fills, so the shadow contains zeros — what scanners read.
        UINT64 shadowPhys = 0;
        PVOID  shadowVa   = EptAllocTable(&shadowPhys);
        if (!shadowVa) { allocFailures++; continue; }

        EptHook* h = (EptHook*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(EptHook),
                                               XENTYPE2_POOL_TAG);
        if (!h) {
            ExFreePoolWithTag(shadowVa, XENTYPE2_POOL_TAG);
            allocFailures++;
            continue;
        }
        h->GuestPhysPage   = pageBase;
        h->ShadowPhys      = shadowPhys;
        h->ShadowVa        = shadowVa;
        h->OriginalVa      = nullptr;
        h->OriginalMemType = (UINT8)ptEntry->MemType;
        h->IsCloak         = TRUE;

        // Initial state: serve the shadow on read with no execute.  First
        // execute from any core will VM-exit and the handler swaps to
        // real-X.  Reads from elsewhere always land here serving zeros.
        ptEntry->Pfn     = shadowPhys >> 12;
        ptEntry->Read    = 1;
        ptEntry->Write   = 1;
        ptEntry->Execute = 0;

        KIRQL oldIrql;
        KeAcquireSpinLock(&g_HookLock, &oldIrql);
        h->Next = g_HookList;
        g_HookList = h;
        KeReleaseSpinLock(&g_HookLock, oldIrql);

        cloakedCount++;
    }

    DbgPrint("[EPT] Cloak: range=[0x%llX..0x%llX] pages_cloaked=%u "
             "split_fail=%u alloc_fail=%u resolve_fail=%u\n",
             startVa, endVa, cloakedCount,
             splitFailures, allocFailures, resolveFailures);

    return cloakedCount > 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
