#pragma once
//
// Discover.h - Locate guest-kernel modules and symbols.
//
// Type-2 hypervisor: we are in the guest kernel's address space, so we can
// walk PsLoadedModuleList directly.  This module wraps:
//   - module lookup by base-name (e.g. "nt", "ci.dll")
//   - export-table parsing for named symbols
//   - byte-pattern scanning over a module's .text for non-exported symbols
//
// All functions are safe at IRQL <= DISPATCH_LEVEL.
//

#include <ntifs.h>

// Locate a loaded kernel module by base-name (case-insensitive, no extension).
// Examples of BaseName: L"nt", L"ci", L"fltmgr", L"xen*".
// Returns pointer-sized image base, 0 if not found.  Optionally fills OutSize
// with the module's SizeOfImage.
UINT64 DiscoverModuleBase(const WCHAR* BaseName, UINT32* OutSize);

// Resolve a named export from a loaded kernel module by base-name.
// Returns the symbol VA or 0 if not found.
UINT64 DiscoverExport(const WCHAR* ModuleBaseName, const char* SymbolName);

// Pattern-scan a loaded module's .text for a byte signature.
// Pattern bytes of 0 in the mask 'x' positions match literally; '?' in mask
// means wildcard (pattern byte is ignored).  Example:
//   pattern: "\x48\x8D\x0D\x00\x00\x00\x00\xE8"
//   mask:    "xxx????x"
// Returns the VA of the first match or 0.
UINT64 DiscoverSig(const WCHAR* ModuleBaseName,
                   const UCHAR* Pattern, const char* Mask);

// Follow a RIP-relative reference at `instrVa` (the LEA / MOV / CALL itself).
// `dispOffsetInInstr` is the byte offset within the instruction where the
// 4-byte disp32 starts (3 for `48 8D 0D ?? ?? ?? ??`, etc.).
// `instrLen` is the full instruction length so we can compute (rip + size).
UINT64 DiscoverRipRelative(UINT64 instrVa, ULONG dispOffsetInInstr, ULONG instrLen);
