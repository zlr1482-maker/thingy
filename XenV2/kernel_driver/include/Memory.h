#pragma once
#include <ntifs.h>

// ============================================================
//  Physical memory access
// ============================================================

// Map physAddr for `size` bytes (read-only) into kernel VA, copy to buffer, unmap.
NTSTATUS ReadPhysical(UINT64 physAddr, PVOID buffer, SIZE_T size);

// Map physAddr for `size` bytes (read-write) into kernel VA, copy from buffer, unmap.
NTSTATUS WritePhysical(UINT64 physAddr, PVOID buffer, SIZE_T size);

// ============================================================
//  CR3-based virtual-to-physical translation
// ============================================================

// Walk the 4-level page table rooted at `cr3` for a guest virtual address.
// Returns the guest physical address, or 0 on failure (not present / invalid).
UINT64 TranslateGva(UINT64 cr3, UINT64 gva);

// ============================================================
//  Process memory access (ring-0, CR3 page-walk based)
// ============================================================

// Retrieve DirectoryTableBase from EPROCESS for the given process ID.
// Returns 0 if the process is not found.
UINT64 GetProcessCr3(ULONG64 pid);

// Read `size` bytes from virtual address `address` in the address space of
// process `pid` into `buffer`. Handles page-boundary crossings.
// Returns STATUS_SUCCESS on full read, STATUS_PARTIAL_COPY on partial (with
// bytesReadOut holding the successful prefix length), STATUS_ACCESS_VIOLATION
// if nothing could be read.  Pass nullptr for bytesReadOut if you don't care.
NTSTATUS ReadProcessMemory(ULONG64 pid, ULONG64 address, PVOID buffer, SIZE_T size,
                           SIZE_T* bytesReadOut = nullptr);

// Write `size` bytes from `buffer` into virtual address `address` in the
// address space of process `pid`.
NTSTATUS WriteProcessMemory(ULONG64 pid, ULONG64 address, PVOID buffer, SIZE_T size);

// ============================================================
//  Kernel-space read / write
// ============================================================

// SEH-guarded memcpy from a kernel VA.
NTSTATUS ReadKernel(UINT64 kernelVa, PVOID out, SIZE_T size);

// SEH-guarded memcpy to a kernel VA.  Temporarily clears CR0.WP on the
// current core so writes into pages mapped PAGE_READONLY succeed.  Raises
// IRQL to DISPATCH_LEVEL around the write to block migration.
NTSTATUS WriteKernel(UINT64 kernelVa, PVOID src, SIZE_T size);

// ============================================================
//  Module enumeration (PEB walk, ring-0)
// ============================================================

// Walk the PEB loader list of process `pid` and find the module whose name
// matches `moduleName` (case-insensitive).
// Fills *baseOut and *sizeOut on success.
NTSTATUS GetModuleBase(ULONG64 pid, const WCHAR* moduleName, ULONG64* baseOut, ULONG64* sizeOut);
