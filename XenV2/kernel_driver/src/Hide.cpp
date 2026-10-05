/*
 * Hide.cpp - driver self-concealment against common anti-cheat kernel scans.
 *
 * Scrubs (best-effort; any failure is logged and skipped):
 *   - PsLoadedModuleList  (NtQuerySystemInformation class 0x0B / EnumDeviceDrivers)
 *   - PiDDBCacheTable     (hash cache anti-cheats search for known-bad drivers)
 *   - MmUnloadedDrivers   (history buffer of recently unloaded driver names)
 *   - DRIVER_OBJECT->DriverName  (visible via ObQueryNameString)
 *   - DriverObject->MajorFunction[] (null entries fingerprint our IRP set)
 *   - DriverObject->DriverSize / DriverStart (scan-targetable ranges)
 *
 * Locates PiDDB via an ntoskrnl .text signature scan (RtlLookupFunctionEntry
 * anchors us to ntos), then walks the AVL-tree to find and remove our entry.
 * Locates MmUnloadedDrivers via the same anchor.
 */

#include "..\include\Hide.h"
#include <ntimage.h>

// ---- Kernel structures we need that aren't in ntifs.h -------------------

typedef struct _KLDR_DATA_TABLE_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    PVOID          ExceptionTable;
    ULONG          ExceptionTableSize;
    PVOID          GpValue;
    PVOID          NonPagedDebugInfo;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG          Flags;
    USHORT         LoadCount;
    USHORT         __Unused5;
    PVOID          SectionPointer;
    ULONG          CheckSum;
    ULONG          CoverageSectionSize;
    PVOID          CoverageSection;
    PVOID          LoadedImports;
    PVOID          Spare;
    ULONG          SizeOfImageNotRounded;
    ULONG          TimeDateStamp;
} KLDR_DATA_TABLE_ENTRY, *PKLDR_DATA_TABLE_ENTRY;

// PiDDBCacheTable entries are arranged in an AVL tree keyed by this struct
typedef struct _PIDCACHE_ENTRY {
    LIST_ENTRY      List;
    UNICODE_STRING  DriverName;
    ULONG           TimeDateStamp;
    NTSTATUS        LoadStatus;
    CHAR            _0x20[0x20];
} PIDCACHE_ENTRY, *PPIDCACHE_ENTRY;

// MmUnloadedDrivers[] element.  Layout stable since Win7.
typedef struct _UNLOADED_DRIVER {
    UNICODE_STRING Name;
    PVOID          StartAddress;
    PVOID          EndAddress;
    LARGE_INTEGER  CurrentTime;
} UNLOADED_DRIVER, *PUNLOADED_DRIVER;

extern "C" NTKERNELAPI PVOID NTAPI RtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage);

// ---- ntoskrnl .text sig scan for non-exported symbols --------------------
static ULONG_PTR FindSigInNtos(const UCHAR* pBytes, const char* pMaskStr) {
    PVOID base = nullptr;
    if (!RtlPcToFileHeader((PVOID)KeBugCheckEx, &base) || !base) return 0;
    auto* pDos = (IMAGE_DOS_HEADER*)base;
    auto* pNth = (IMAGE_NT_HEADERS64*)((UCHAR*)base + pDos->e_lfanew);

    auto* pSec = IMAGE_FIRST_SECTION(pNth);
    UCHAR* textBase = nullptr; SIZE_T textSize = 0;
    for (ULONG i = 0; i < pNth->FileHeader.NumberOfSections; i++) {
        if (!memcmp(pSec[i].Name, ".text", 5)) {
            textBase = (UCHAR*)base + pSec[i].VirtualAddress;
            textSize = pSec[i].Misc.VirtualSize;
            break;
        }
    }
    if (!textBase) return 0;

    SIZE_T patLen = 0; while (pMaskStr[patLen]) patLen++;
    for (SIZE_T i = 0; i + patLen <= textSize; i++) {
        bool ok = true;
        for (SIZE_T j = 0; j < patLen; j++) {
            if (pMaskStr[j] == 'x' && textBase[i + j] != pBytes[j]) { ok = false; break; }
        }
        if (ok) return (ULONG_PTR)(textBase + i);
    }
    return 0;
}

// ============================================================
//  UnlinkFromPsLoadedModuleList
// ============================================================
// Capture our DllBase before we nuke the LDR entry so we can come back and
// scrub the PE header on the live image.
static PVOID g_OurDllBase = nullptr;
static ULONG g_OurImageSize = 0;

static BOOLEAN UnlinkFromPsLoadedModuleList(PDRIVER_OBJECT DriverObject) {
    auto* entry = (PKLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    if (!entry) return FALSE;

    // Stash image base/size before we zero the fields.
    g_OurDllBase   = entry->DllBase;
    g_OurImageSize = entry->SizeOfImage;

    entry->InLoadOrderLinks.Blink->Flink = entry->InLoadOrderLinks.Flink;
    entry->InLoadOrderLinks.Flink->Blink = entry->InLoadOrderLinks.Blink;

    if (entry->BaseDllName.Buffer)
        RtlZeroMemory(entry->BaseDllName.Buffer, entry->BaseDllName.MaximumLength);
    if (entry->FullDllName.Buffer)
        RtlZeroMemory(entry->FullDllName.Buffer, entry->FullDllName.MaximumLength);
    entry->BaseDllName.Length = 0;
    entry->FullDllName.Length = 0;

    // Zero image-range fields so a scan of LDR entries for our code range
    // can't find us via DllBase/SizeOfImage match either.
    entry->DllBase              = nullptr;
    entry->SizeOfImage          = 0;
    entry->SizeOfImageNotRounded= 0;
    entry->EntryPoint           = nullptr;
    entry->CheckSum             = 0;
    entry->TimeDateStamp        = 0;

    return TRUE;
}

// Zero the "MZ" / "PE" signatures and the DOS stub inside our own loaded
// image.  A memory scanner that walks non-paged pool looking for PE headers
// won't pattern-match us anymore.  We use the WriteKernel helper because
// the header page is typically read-only.
NTSTATUS WriteKernel(UINT64 kernelVa, PVOID src, SIZE_T size);

static BOOLEAN ScrubOwnPeHeader() {
    if (!g_OurDllBase || !g_OurImageSize) return FALSE;
    auto* dos = (IMAGE_DOS_HEADER*)g_OurDllBase;
    // Zero the whole DOS header + stub up to e_lfanew, then the 4-byte PE sig.
    UINT32 stubLen = dos->e_lfanew > 0 && dos->e_lfanew < 0x400 ? dos->e_lfanew : 0x40;
    UCHAR zeros[0x40] = {};
    // Header write — wrap in WriteKernel which handles the RO mapping.
    NTSTATUS s1 = WriteKernel((UINT64)g_OurDllBase, zeros, stubLen < sizeof(zeros) ? stubLen : sizeof(zeros));
    // Kill the PE signature so `!drivers` and PE-scanning AC both lose us.
    if (dos->e_lfanew > 0 && dos->e_lfanew < (LONG)g_OurImageSize - 4) {
        UCHAR zero4[4] = {};
        WriteKernel((UINT64)g_OurDllBase + dos->e_lfanew, zero4, 4);
    }
    return NT_SUCCESS(s1);
}

static BOOLEAN ClearDriverObjectName(PDRIVER_OBJECT DriverObject) {
    if (DriverObject->DriverName.Buffer && DriverObject->DriverName.MaximumLength) {
        RtlZeroMemory(DriverObject->DriverName.Buffer,
                      DriverObject->DriverName.MaximumLength);
        DriverObject->DriverName.Length = 0;
    }
    // Also zero the driver-range fields that ObReferenceObjectByName-style
    // scans sometimes fingerprint.
    DriverObject->DriverStart = nullptr;
    DriverObject->DriverSize  = 0;
    return TRUE;
}

// Null out unused IRP major function handlers.  The default dispatcher fills
// every slot with IofCallDriver-returning stub; distinctive "we handle 0x0E
// and nothing else" patterns are a signature target.  Setting the rest to
// the same dispatch (our real one) produces a flat, boring table.
static BOOLEAN NormaliseIrpTable(PDRIVER_OBJECT DriverObject) {
    PDRIVER_DISPATCH any = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    if (!any) return FALSE;
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        if (!DriverObject->MajorFunction[i])
            DriverObject->MajorFunction[i] = any;
    }
    return TRUE;
}

// ============================================================
//  ClearPiDDBCacheEntry
// ============================================================
//
//  PiDDBCacheAdd contains a known-stable prologue.  We locate it via an
//  ntoskrnl .text signature scan, then recover PiDDBLock and PiDDBCacheTable
//  via RIP-relative displacements inside that prologue.
//
//  Scrub strategy: walk the AVL tree rooted at PiDDBCacheTable, find an entry
//  whose DriverName matches our .sys basename (or TimeDateStamp matches our
//  PE stamp), unlink it from the tree.  If we can't find the symbols, skip.
//
static BOOLEAN ClearPiDDBCacheEntry(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);

    // Signature for PiDDBLock lea inside PiDDBCacheAdd, stable across Win10 20H2+:
    //   48 8D 0D ?? ?? ?? ??   lea rcx, [PiDDBLock]
    //   E8 ?? ?? ?? ??         call ExAcquireResourceExclusiveLite
    // We scan for the longer surrounding context to avoid false positives.
    static const UCHAR SigPiDDBLock[] = {
        0x48, 0x8D, 0x0D, 0, 0, 0, 0,      // lea rcx,[PiDDBLock]
        0xE8, 0, 0, 0, 0,                   // call
        0x84, 0xC0                          // test al,al
    };
    static const char MaskL[] = "xxx????x????xx";
    ULONG_PTR hit = FindSigInNtos(SigPiDDBLock, MaskL);
    if (!hit) {
        DbgPrint("[Hide] PiDDB scrub: signature not found, skipped.\n");
        return FALSE;
    }

    // Resolve PiDDBLock from the lea's disp32.
    LONG disp = *(LONG*)(hit + 3);
    PERESOURCE pLock = (PERESOURCE)(hit + 7 + disp);

    // PiDDBCacheTable lives a fixed few 8-byte slots before PiDDBLock on x64.
    // In practice it's at PiDDBLock - 0x10 (two pointers back).  Try that,
    // and if the "tree root" doesn't look like a valid RTL_AVL_TABLE, bail.
    PRTL_AVL_TABLE pTable = (PRTL_AVL_TABLE)((UCHAR*)pLock - 0x10);

    // Sanity: TableRoutine pointers should be within ntoskrnl.
    PVOID ntosBase = nullptr;
    RtlPcToFileHeader((PVOID)KeBugCheckEx, &ntosBase);
    if (!ntosBase) return FALSE;
    auto* ntosNt  = (IMAGE_NT_HEADERS64*)((UCHAR*)ntosBase +
                    ((IMAGE_DOS_HEADER*)ntosBase)->e_lfanew);
    uintptr_t ntosLo = (uintptr_t)ntosBase;
    uintptr_t ntosHi = ntosLo + ntosNt->OptionalHeader.SizeOfImage;
    auto inNtos = [&](PVOID p) {
        return p && (uintptr_t)p >= ntosLo && (uintptr_t)p < ntosHi;
    };
    if (!inNtos(pTable->CompareRoutine) ||
        !inNtos(pTable->AllocateRoutine) ||
        !inNtos(pTable->FreeRoutine)) {
        DbgPrint("[Hide] PiDDB scrub: table at %p doesn't look valid.\n", pTable);
        return FALSE;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(pLock, TRUE);

    BOOLEAN removed = FALSE;
    PVOID restartKey = nullptr;
    for (PPIDCACHE_ENTRY e = (PPIDCACHE_ENTRY)RtlEnumerateGenericTableWithoutSplayingAvl(
             pTable, &restartKey);
         e != nullptr;
         e = (PPIDCACHE_ENTRY)RtlEnumerateGenericTableWithoutSplayingAvl(pTable, &restartKey))
    {
        if (!e->DriverName.Buffer || !e->DriverName.Length) continue;
        // Match any entry whose name contains "XenV2" — covers renamed
        // variants too.  Unicode case-sensitive substring is overkill; we
        // just do a tolerant WCHAR scan for 'X','e','n','V','2'.
        USHORT chars = e->DriverName.Length / sizeof(WCHAR);
        const WCHAR tag[] = { L'X', L'e', L'n', L'V', L'2' };
        const USHORT taglen = sizeof(tag) / sizeof(tag[0]);
        bool match = false;
        for (USHORT i = 0; i + taglen <= chars; i++) {
            bool ok = true;
            for (USHORT k = 0; k < taglen; k++) {
                if (e->DriverName.Buffer[i + k] != tag[k]) { ok = false; break; }
            }
            if (ok) { match = true; break; }
        }
        if (match) {
            RtlDeleteElementGenericTableAvl(pTable, e);
            removed = TRUE;
            break; // restart-key invalidated
        }
    }

    ExReleaseResourceLite(pLock);
    KeLeaveCriticalRegion();
    DbgPrint("[Hide] PiDDB scrub: %s.\n", removed ? "entry removed" : "no match");
    return removed;
}

// ============================================================
//  ClearMmUnloadedDrivers
// ============================================================
//
//  When a driver is unloaded, the kernel keeps its name in a ring buffer of
//  ~50 entries pointed at by nt!MmUnloadedDrivers.  Anti-cheats read this
//  buffer via NtQuerySystemInformation class 27 (SystemUnloadedDriversInformation).
//
//  If the driver is never unloaded, we don't appear.  But if something
//  unloaded us earlier this boot (e.g. reload), we're in there.  Scan the
//  buffer by string and zero any matching entry.
//
static BOOLEAN ClearMmUnloadedDrivers() {
    // MmLocateKernelStack or similar symbols give us an ntos .data range;
    // MmUnloadedDrivers is exported (as a ptr to PUNLOADED_DRIVER) on some
    // builds but not all.  Best-effort resolve via known ntos symbol +
    // fixed offset.  If not found, skip.
    //
    // Signature within MiRememberUnloadedDriver, stable Win10 1909+:
    //   48 8B 05 ?? ?? ?? ??     mov rax, [MmUnloadedDrivers]
    static const UCHAR SigMm[] = { 0x48, 0x8B, 0x05, 0,0,0,0, 0x48, 0x85, 0xC0 };
    static const char MaskMm[] = "xxx????xxx";
    ULONG_PTR hit = FindSigInNtos(SigMm, MaskMm);
    if (!hit) {
        DbgPrint("[Hide] MmUnloadedDrivers scrub: sig not found, skipped.\n");
        return FALSE;
    }
    LONG disp = *(LONG*)(hit + 3);
    PUNLOADED_DRIVER* ppArr = (PUNLOADED_DRIVER*)(hit + 7 + disp);
    PUNLOADED_DRIVER arr = *ppArr;
    if (!arr) return FALSE;

    // Walk up to 50 entries; zero any whose name contains "XenV2"
    ULONG cleared = 0;
    for (int i = 0; i < 50; i++) {
        auto& e = arr[i];
        if (!e.Name.Buffer || !e.Name.Length) continue;
        USHORT chars = e.Name.Length / sizeof(WCHAR);
        const WCHAR tag[] = { L'X', L'e', L'n', L'V', L'2' };
        const USHORT taglen = 5;
        bool match = false;
        for (USHORT j = 0; j + taglen <= chars; j++) {
            bool ok = true;
            for (USHORT k = 0; k < taglen; k++) {
                if (e.Name.Buffer[j + k] != tag[k]) { ok = false; break; }
            }
            if (ok) { match = true; break; }
        }
        if (match) {
            RtlZeroMemory(e.Name.Buffer, e.Name.MaximumLength);
            e.Name.Length = 0;
            e.StartAddress = nullptr;
            e.EndAddress = nullptr;
            e.CurrentTime.QuadPart = 0;
            cleared++;
        }
    }
    DbgPrint("[Hide] MmUnloadedDrivers scrub: cleared %lu entries.\n", cleared);
    return cleared > 0;
}

// ============================================================
//  Public entry
// ============================================================
ULONG HideDriver(PDRIVER_OBJECT DriverObject) {
    ULONG done = 0;
    if (UnlinkFromPsLoadedModuleList(DriverObject)) {
        DbgPrint("[Hide] Unlinked from PsLoadedModuleList.\n"); done++;
    }
    if (ClearDriverObjectName(DriverObject)) {
        DbgPrint("[Hide] Cleared DriverObject->DriverName + range fields.\n"); done++;
    }
    if (NormaliseIrpTable(DriverObject)) {
        DbgPrint("[Hide] Normalised IRP major-function table.\n"); done++;
    }
    if (ClearPiDDBCacheEntry(DriverObject)) done++;
    if (ClearMmUnloadedDrivers())           done++;
    if (ScrubOwnPeHeader()) {
        DbgPrint("[Hide] PE header scrubbed on own image.\n"); done++;
    }
    return done;
}
