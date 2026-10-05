/*
 * Memory.cpp - CR3 page-walk memory engine
 *
 * Reads and writes target process memory by:
 *   1. Locating the EPROCESS for the target PID.
 *   2. Reading DirectoryTableBase (CR3 equivalent) from EPROCESS+0x28.
 *   3. Walking the 4-level x64 page table in physical memory to translate
 *      the guest virtual address → guest physical address.
 *   4. Mapping the physical page into kernel VA temporarily to R/W.
 *
 * This works from ring 0 (no VMX required) and is invisible to anything
 * running in the target process's address space.
 *
 * EPROCESS offsets used (stable across Win10 1903 → Win11 24H2 x64):
 *   +0x028  DirectoryTableBase (CR3)
 *   +0x550  Peb  (Win10 2004+; Win10 1903 = 0x550 as well — unchanged since RS1)
 *
 * PEB offsets (x64):
 *   +0x018  Ldr  (pointer to PEB_LDR_DATA)
 *
 * PEB_LDR_DATA offsets (x64):
 *   +0x010  InLoadOrderModuleList (LIST_ENTRY head)
 *
 * LDR_DATA_TABLE_ENTRY (InLoadOrderLinks, x64):
 *   +0x000  InLoadOrderLinks (LIST_ENTRY)
 *   +0x030  DllBase
 *   +0x040  SizeOfImage
 *   +0x058  BaseDllName (UNICODE_STRING: +0x000 Len, +0x008 Buffer ptr)
 */

#include <ntifs.h>
#include "..\include\Memory.h"
#include "..\include\Vmx.h"
#include "..\include\Discover.h"
extern VmxGlobal* g_Vmx;

// ZwProtectVirtualMemory isn't in ntifs.h on all WDK versions; declare it.
extern "C" NTKERNELAPI NTSTATUS ZwProtectVirtualMemory(
    HANDLE  ProcessHandle,
    PVOID*  BaseAddress,
    PSIZE_T RegionSize,
    ULONG   NewProtect,
    PULONG  OldProtect);

// ============================================================
//  Physical memory read/write  via MmMapIoSpaceEx
// ============================================================

NTSTATUS ReadPhysical(UINT64 physAddr, PVOID buffer, SIZE_T size) {
    if (!physAddr || !buffer || size == 0) return STATUS_INVALID_PARAMETER;

    // MmCopyMemory is the correct API for reading regular RAM by physical
    // address — unlike MmMapIoSpaceEx which is for uncached I/O memory and
    // returns garbage for RAM pages in cached mode.
    MM_COPY_ADDRESS src = {};
    src.PhysicalAddress.QuadPart = (LONGLONG)physAddr;
    SIZE_T transferred = 0;
    return MmCopyMemory(buffer, src, size, MM_COPY_MEMORY_PHYSICAL, &transferred);
}

NTSTATUS WritePhysical(UINT64 physAddr, PVOID buffer, SIZE_T size) {
    if (!physAddr || !buffer || size == 0) return STATUS_INVALID_PARAMETER;

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (LONGLONG)physAddr;

    PVOID mapped = MmMapIoSpaceEx(pa, size, PAGE_READWRITE);
    if (!mapped) return STATUS_INSUFFICIENT_RESOURCES;

    __try {
        RtlCopyMemory(mapped, buffer, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        MmUnmapIoSpace(mapped, size);
        return STATUS_ACCESS_VIOLATION;
    }

    MmUnmapIoSpace(mapped, size);
    return STATUS_SUCCESS;
}

// ============================================================
//  GVA → GPA translation  (4-level page walk)
// ============================================================

UINT64 TranslateGva(UINT64 cr3, UINT64 gva) {
    // Reject non-canonical VAs (bits 48-63 must all match bit 47).
    UINT64 signExt = ((INT64)gva >> 47);
    if (signExt != 0 && signExt != -1) return 0;
    if (cr3 == 0) return 0;

    __try {
        // Strip PCID bits from CR3 (bits 11:0 — PCID is at [11:0])
        UINT64 pml4Base = cr3 & ~0xFFFULL;

        UINT64 pml4Idx = (gva >> 39) & 0x1FF;
        UINT64 pdptIdx = (gva >> 30) & 0x1FF;
        UINT64 pdIdx   = (gva >> 21) & 0x1FF;
        UINT64 ptIdx   = (gva >> 12) & 0x1FF;
        UINT64 offset  = gva & 0xFFF;

        UINT64 pml4e = 0;
        if (!NT_SUCCESS(ReadPhysical(pml4Base + pml4Idx * 8, &pml4e, 8))) return 0;
        if (!(pml4e & 1)) return 0;

        UINT64 pdptBase = pml4e & 0x000FFFFFFFFFF000ULL;
        UINT64 pdpte    = 0;
        if (!NT_SUCCESS(ReadPhysical(pdptBase + pdptIdx * 8, &pdpte, 8))) return 0;
        if (!(pdpte & 1)) return 0;

        if (pdpte & (1ULL << 7)) {
            return (pdpte & 0x000FFFFFC0000000ULL) | (gva & 0x3FFFFFFF);
        }

        UINT64 pdBase = pdpte & 0x000FFFFFFFFFF000ULL;
        UINT64 pde    = 0;
        if (!NT_SUCCESS(ReadPhysical(pdBase + pdIdx * 8, &pde, 8))) return 0;
        if (!(pde & 1)) return 0;

        if (pde & (1ULL << 7)) {
            return (pde & 0x000FFFFFFFE00000ULL) | (gva & 0x1FFFFF);
        }

        UINT64 ptBase = pde & 0x000FFFFFFFFFF000ULL;
        UINT64 pte    = 0;
        if (!NT_SUCCESS(ReadPhysical(ptBase + ptIdx * 8, &pte, 8))) return 0;
        if (!(pte & 1)) return 0;

        return (pte & 0x000FFFFFFFFFF000ULL) | offset;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ============================================================
//  Get EPROCESS.DirectoryTableBase for a PID
// ============================================================

UINT64 GetProcessCr3(ULONG64 pid) {
    PEPROCESS process = nullptr;
    NTSTATUS  status  = PsLookupProcessByProcessId((HANDLE)pid, &process);
    if (!NT_SUCCESS(status)) return 0;

    // Offset 0x28 = DirectoryTableBase, stable since Vista x64.
    // Wrap in SEH — a malformed EPROCESS pointer (shouldn't happen after
    // PsLookup but cheap insurance) would fault here and BSOD the host.
    UINT64 dtb = 0;
    __try {
        dtb = *(volatile UINT64*)((UINT8*)process + 0x28);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dtb = 0;
    }

    ObDereferenceObject(process);
    return dtb;
}

// ============================================================
//  ReadProcessMemory — handles page-boundary crossings
// ============================================================

// MmCopyVirtualMemory — kernel's internal cross-process memory copy.
// Handles page-faults (demand-page, CoW, etc.) inside its own SEH frame,
// so we don't need our own SEH to be registered.  Exported by ntoskrnl
// but not declared in ntifs.h — resolve via MmGetSystemRoutineAddress
// with DiscoverExport fallback.
typedef NTSTATUS (*PFN_MmCopyVirtualMemory)(
    PEPROCESS      SourceProcess,
    PVOID          SourceAddress,
    PEPROCESS      TargetProcess,
    PVOID          TargetAddress,
    SIZE_T         BufferSize,
    KPROCESSOR_MODE PreviousMode,
    PSIZE_T        ReturnSize);

static PFN_MmCopyVirtualMemory ResolveMmCopyVirtualMemory() {
    static PFN_MmCopyVirtualMemory fn = nullptr;
    if (fn) return fn;
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"MmCopyVirtualMemory");
    fn = (PFN_MmCopyVirtualMemory)MmGetSystemRoutineAddress(&name);
    if (!fn) {
        UINT64 a = DiscoverExport(L"ntoskrnl", "MmCopyVirtualMemory");
        fn = (PFN_MmCopyVirtualMemory)a;
    }
    return fn;
}

NTSTATUS ReadProcessMemory(ULONG64 pid, ULONG64 address, PVOID buffer, SIZE_T size,
                           SIZE_T* bytesReadOut) {
    if (bytesReadOut) *bytesReadOut = 0;
    if (!buffer || !size) return STATUS_INVALID_PARAMETER;

    // Strict user-VA range check.  The target is always a usermode
    // process, so valid source VAs are in [0x10000, 0x7FFFFFFFFFFF].
    // Anything outside that range (caller miscomputed, integer underflow,
    // etc.) is rejected up front — otherwise MmCopyVirtualMemory's
    // internal memcpy can fault on garbage and bugcheck 0x50.
    const ULONG64 USER_VA_LO = 0x10000ULL;
    const ULONG64 USER_VA_HI = 0x7FFFFFFFFFFFULL;
    if (address < USER_VA_LO || address > USER_VA_HI) return STATUS_INVALID_ADDRESS;
    if (size > 0x10000) return STATUS_INVALID_PARAMETER;
    if (address + size - 1 > USER_VA_HI) return STATUS_INVALID_ADDRESS;

    // PRIMARY PATH: CR3 page-walk + MmCopyMemory(PHYSICAL).
    //
    // Avoids MmCopyVirtualMemory's working-set lock acquisition per call.
    // At 600 reads/frame * 500 fps = 300k lock-acquires/sec on the target
    // process; the game's render thread contests the same lock when it
    // touches its own UObjects, causing visible FPS drops.  CR3 walk
    // operates on physical pages without ever taking that lock.
    //
    // Limitation: only reads RESIDENT pages.  For hot game data (touched
    // every frame) this is always true.  For paged-out memory we fall
    // back to MmCopyVirtualMemory below.
    UINT64 cr3 = GetProcessCr3(pid);
    if (cr3) {
        UINT8* dst  = (UINT8*)buffer;
        UINT64 src  = address;
        SIZE_T rem  = size;
        SIZE_T done = 0;
        NTSTATUS walkErr = STATUS_SUCCESS;
        while (rem > 0) {
            UINT64 off   = src & 0xFFF;
            SIZE_T chunk = (SIZE_T)min((UINT64)rem, PAGE_SIZE - off);
            UINT64 phys  = TranslateGva(cr3, src);
            if (!phys) { walkErr = STATUS_ACCESS_VIOLATION; break; }
            NTSTATUS st = ReadPhysical(phys, dst, chunk);
            if (!NT_SUCCESS(st)) { walkErr = st; break; }
            rem -= chunk; src += chunk; dst += chunk; done += chunk;
        }
        if (rem == 0) {
            if (bytesReadOut) *bytesReadOut = done;
            return STATUS_SUCCESS;
        }
        // CR3 walk hit a hole.  If we got SOME bytes the prefix is valid;
        // for hot-path callers (per-frame ESP) that's almost always
        // sufficient.  Only fall back to MmCopyVirtualMemory when we got
        // ZERO bytes — meaning the very first page is paged out and we
        // need the kernel to fault it in.
        if (done > 0) {
            if (bytesReadOut) *bytesReadOut = done;
            return STATUS_PARTIAL_COPY;
        }
        (void)walkErr;
    }

    // FALLBACK: MmCopyVirtualMemory.  Used only when CR3 walk got 0 bytes
    // (paged-out source) or when we couldn't get the target's CR3.
    auto fn = ResolveMmCopyVirtualMemory();
    if (fn) {
        PEPROCESS target = nullptr;
        NTSTATUS st = PsLookupProcessByProcessId((HANDLE)pid, &target);
        if (!NT_SUCCESS(st)) return st;

        if (PsGetProcessExitStatus(target) != STATUS_PENDING) {
            ObDereferenceObject(target);
            return STATUS_PROCESS_IS_TERMINATING;
        }

        SIZE_T copied = 0;
        st = fn(target, (PVOID)address,
                PsGetCurrentProcess(), buffer,
                size, KernelMode, &copied);
        ObDereferenceObject(target);
        if (bytesReadOut) *bytesReadOut = copied;
        return st;
    }

    return STATUS_ACCESS_VIOLATION;
}

// ============================================================
//  WriteProcessMemory — handles page-boundary crossings
// ============================================================

NTSTATUS WriteProcessMemory(ULONG64 pid, ULONG64 address, PVOID buffer, SIZE_T size) {
    // IMPORTANT: direct CR3 page-walk writes bypass Copy-on-Write.  Writing
    // to a shared read-only page (e.g. ntdll .text, which every process
    // maps the same physical pages of) mutates the canonical page and
    // corrupts it for all processes — we crashed lsass this way.  Route
    // writes through KeStackAttachProcess + memcpy: that runs in the
    // target's VA context so a store to a COW page raises a page fault
    // which the MM subsystem resolves by allocating a private copy for
    // the target process (the documented behaviour).
    PEPROCESS proc = nullptr;
    NTSTATUS  st   = PsLookupProcessByProcessId((HANDLE)pid, &proc);
    if (!NT_SUCCESS(st)) return st;

    KAPC_STATE apc;
    KeStackAttachProcess(proc, &apc);

    // Target page(s) may be PAGE_EXECUTE_READ.  We need to let the store
    // actually land — flip protection to RW, copy, flip back.  This is
    // per-page; we'll do one protect call covering the whole range.
    PVOID   vaddr = (PVOID)address;
    SIZE_T  vsize = size;
    ULONG   oldProt = 0;
    NTSTATUS pSt = ZwProtectVirtualMemory(ZwCurrentProcess(),
                                          &vaddr, &vsize,
                                          PAGE_EXECUTE_READWRITE, &oldProt);

    // MmIsAddressValid preflight — SEH is unreliable in manual-mapped
    // drivers since the kernel can't find our .pdata.
    if (!MmIsAddressValid((PVOID)address) ||
        !MmIsAddressValid((PVOID)(address + size - 1))) {
        st = STATUS_ACCESS_VIOLATION;
    } else {
        RtlCopyMemory((PVOID)address, buffer, size);
        st = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(pSt)) {
        // Restore original protection
        PVOID   va2  = (PVOID)address;
        SIZE_T  sz2  = size;
        ULONG   junk = 0;
        ZwProtectVirtualMemory(ZwCurrentProcess(), &va2, &sz2, oldProt, &junk);
    }

    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(proc);
    return st;
}

// ============================================================
//  GetModuleBase — PEB Ldr walk via physical memory
// ============================================================

// Read a UINT64 from a target-process virtual address using our CR3 path.
// Returns 0 if anything about the read fails — never faults the host.
static UINT64 ReadPtr(UINT64 cr3, UINT64 va) {
    if (!cr3) return 0;
    UINT64 signExt = ((INT64)va >> 47);
    if (signExt != 0 && signExt != -1) return 0;
    UINT64 val = 0;
    __try {
        UINT64 phys = TranslateGva(cr3, va);
        if (!phys) return 0;
        if (!NT_SUCCESS(ReadPhysical(phys, &val, sizeof(val)))) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return val;
}

// Read N bytes from target-process virtual address.  Fully SEH-guarded:
// any fault during translation or copy returns STATUS_ACCESS_VIOLATION
// instead of bugchecking the host.
static NTSTATUS ReadVa(UINT64 cr3, UINT64 va, PVOID buf, SIZE_T sz) {
    if (!buf || !sz || !cr3) return STATUS_INVALID_PARAMETER;
    // Reject obviously bogus requests (huge sizes, non-canonical VAs).
    if (sz > 0x10000000) return STATUS_INVALID_PARAMETER;
    UINT64 signExt = ((INT64)va >> 47);
    if (signExt != 0 && signExt != -1) return STATUS_INVALID_ADDRESS;

    __try {
        SIZE_T rem = sz;
        UINT8* dst = (UINT8*)buf;
        UINT64 src = va;
        while (rem) {
            UINT64 phys = TranslateGva(cr3, src);
            if (!phys) return STATUS_ACCESS_VIOLATION;
            UINT64 off  = src & 0xFFF;
            SIZE_T chunk = (SIZE_T)min((UINT64)rem, PAGE_SIZE - off);
            NTSTATUS st = ReadPhysical(phys, dst, chunk);
            if (!NT_SUCCESS(st)) return st;
            rem -= chunk; src += chunk; dst += chunk;
        }
        return STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_ACCESS_VIOLATION;
    }
}

extern "C" PVOID PsGetProcessSectionBaseAddress(PEPROCESS Process);
extern "C" PVOID PsGetProcessPeb(PEPROCESS Process);

NTSTATUS GetModuleBase(ULONG64 pid, const WCHAR* moduleName,
                       ULONG64* baseOut, ULONG64* sizeOut)
{
    if (!baseOut || !sizeOut || !moduleName) return STATUS_INVALID_PARAMETER;
    *baseOut = 0;
    *sizeOut = 0;

    PEPROCESS process = nullptr;
    NTSTATUS  status  = PsLookupProcessByProcessId((HANDLE)pid, &process);
    if (!NT_SUCCESS(status)) return status;

    // SEH-guard every EPROCESS deref.  Bad offsets on unexpected Windows
    // builds have historically crashed this function; catching makes the
    // whole lookup a soft failure instead of a bugcheck.
    UINT64 cr3      = 0;
    UINT64 mainBase = 0;
    UINT64 pebVa    = 0;
    __try {
        cr3      = *(volatile UINT64*)((UINT8*)process + 0x28);
        mainBase = (UINT64)PsGetProcessSectionBaseAddress(process);
        pebVa    = (UINT64)PsGetProcessPeb(process);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cr3 = mainBase = pebVa = 0;
    }

    ObDereferenceObject(process);

    if (!pebVa || !cr3) {
        // No PEB — can't walk Ldr.  Fall back to main image base if we have it.
        if (mainBase) {
            *baseOut = mainBase;
            *sizeOut = 0;
            return STATUS_SUCCESS;
        }
        return STATUS_NOT_FOUND;
    }

    // PEB.Ldr = PEB+0x018
    UINT64 ldrVa = ReadPtr(cr3, pebVa + 0x18);
    if (!ldrVa) {
        // Can't read Ldr — fall back to main image base if this is a lookup for the main image.
        if (mainBase) {
            *baseOut = mainBase;
            *sizeOut = 0;
            return STATUS_SUCCESS;
        }
        return STATUS_NOT_FOUND;
    }

    // PEB_LDR_DATA.InLoadOrderModuleList head = Ldr+0x010
    UINT64 listHead = ldrVa + 0x10;
    UINT64 entry    = ReadPtr(cr3, listHead); // Flink of head = first real entry

    WCHAR entryName[64] = {};
    SIZE_T moduleNameLen = wcslen(moduleName);

    for (UINT32 i = 0; i < 512 && entry && entry != listHead; i++) {
        // Validate `entry` looks like a canonical user-mode VA before touching it.
        UINT64 sx = ((INT64)entry >> 47);
        if (sx != 0 && sx != -1) break;

        UINT64 dllBase    = ReadPtr(cr3, entry + 0x30);
        UINT32 sizeOfImage = 0;
        (void)ReadVa(cr3, entry + 0x40, &sizeOfImage, 4);

        UINT16 nameLen = 0;
        (void)ReadVa(cr3, entry + 0x58, &nameLen, 2);
        UINT64 nameBuf = ReadPtr(cr3, entry + 0x60);

        if (nameLen > 0 && nameLen <= 126 && nameBuf) {
            RtlZeroMemory(entryName, sizeof(entryName));
            (void)ReadVa(cr3, nameBuf, entryName, nameLen);

            BOOLEAN match = TRUE;
            SIZE_T  entryChars = nameLen / sizeof(WCHAR);
            if (entryChars != moduleNameLen) {
                match = FALSE;
            } else {
                for (SIZE_T c = 0; c < entryChars && match; c++) {
                    WCHAR a = entryName[c];
                    WCHAR b = moduleName[c];
                    if (a >= L'a' && a <= L'z') a -= 32;
                    if (b >= L'a' && b <= L'z') b -= 32;
                    if (a != b) match = FALSE;
                }
            }

            if (match && dllBase) {
                *baseOut = dllBase;
                *sizeOut = sizeOfImage;
                return STATUS_SUCCESS;
            }
        }

        UINT64 next = ReadPtr(cr3, entry);
        if (next == entry) break;  // broken list — self-reference
        entry = next;
    }

    // Fallback: Ldr walk missed it (offsets may have shifted on newer Windows
    // builds) but we still have a main-image base from the documented API.
    if (mainBase) {
        *baseOut = mainBase;
        *sizeOut = 0;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_FOUND;
}

// ============================================================
//  Kernel-space read / write  (for DSE bypass, PG neuter, etc.)
// ============================================================

NTSTATUS ReadKernel(UINT64 kernelVa, PVOID out, SIZE_T size) {
    if (!kernelVa || !out || !size) return STATUS_INVALID_PARAMETER;
    __try {
        RtlCopyMemory(out, (PVOID)kernelVa, size);
        return STATUS_SUCCESS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_ACCESS_VIOLATION;
    }
}

// Write to a read-only kernel VA by mapping its physical page as writable
// through a fresh kernel VA.  Avoids CR0.WP toggling — that technique
// interacts badly with CET supervisor shadow stacks on Win10 26100 /
// Win11 24H2 and causes immediate SYSTEM_SERVICE_EXCEPTION 0xC0000096.
// Only supports writes that fit in a single page.
NTSTATUS WriteKernel(UINT64 kernelVa, PVOID src, SIZE_T size) {
    if (!kernelVa || !src || !size) return STATUS_INVALID_PARAMETER;
    if (((kernelVa & 0xFFF) + size) > PAGE_SIZE) return STATUS_INVALID_PARAMETER;

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = 0;
    __try {
        pa = MmGetPhysicalAddress((PVOID)kernelVa);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!pa.QuadPart) return STATUS_INVALID_ADDRESS;

    PVOID mapped = MmMapIoSpaceEx(pa, size, PAGE_READWRITE);
    if (!mapped) return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS st = STATUS_SUCCESS;
    __try {
        RtlCopyMemory(mapped, src, size);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        st = STATUS_ACCESS_VIOLATION;
    }
    MmUnmapIoSpace(mapped, size);
    return st;
}
