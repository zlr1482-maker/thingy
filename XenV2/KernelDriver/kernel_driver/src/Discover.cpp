/*
 * Discover.cpp - Guest-kernel module + symbol lookup.
 *
 * Walks PsLoadedModuleList (the same LIST_ENTRY chain we unlink ourselves
 * from in Hide.cpp) to locate other kernel drivers.  For each module we can
 * parse its PE headers via MmIsAddressValid-guarded reads.
 */

#include "..\include\Discover.h"
#include <ntimage.h>

// Same layout we use in Hide.cpp — copy here to keep the file standalone.
typedef struct _KLDR_DATA_TABLE_ENTRY_D {
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
} KLDR_DATA_TABLE_ENTRY_D, *PKLDR_DATA_TABLE_ENTRY_D;

extern "C" NTKERNELAPI PVOID NTAPI RtlPcToFileHeader(PVOID PcValue, PVOID* BaseOfImage);

// ----- Walk PsLoadedModuleList.  PsLoadedModuleList is referenced by
// PsGetCurrentProcessId's pseudo-neighbourhood (via LDR_DATA of ntoskrnl),
// but the simplest portable route is RtlPcToFileHeader(&KeBugCheckEx) to
// anchor ntoskrnl, then walk through our own DriverObject's DriverSection.
// Our entry was unlinked, so we can't walk from ourselves — anchor through
// any other known-loaded driver's chain.  Fallback: use
// AuxKlibQueryModuleInformation which is officially documented.

extern "C" NTSTATUS AuxKlibInitialize();
extern "C" NTSTATUS AuxKlibQueryModuleInformation(
    PULONG BufferSize, ULONG ElementSize, PVOID QueryInfo);

// AUX_MODULE_EXTENDED_INFO layout (docs: AuxKlibQueryModuleInformation).
typedef struct _AUX_MODULE_BASIC_INFO {
    PVOID ImageBase;
} AUX_MODULE_BASIC_INFO;

typedef struct _AUX_MODULE_EXTENDED_INFO {
    AUX_MODULE_BASIC_INFO BasicInfo;
    ULONG ImageSize;
    USHORT FileNameOffset;
    UCHAR  FullPathName[256];
} AUX_MODULE_EXTENDED_INFO, *PAUX_MODULE_EXTENDED_INFO;

// Case-insensitive ASCII compare of last path component vs base-name.
// `modBase` = ASCII full path like "\SystemRoot\System32\ntoskrnl.exe".
// `wantBase` = "ntoskrnl" or "ci" or "ntos" — match with optional ".sys/.dll/.exe".
static BOOLEAN BaseNameMatches(const UCHAR* fullpath, USHORT fileNameOffset,
                               const WCHAR* wantBase)
{
    const UCHAR* p = fullpath + fileNameOffset;
    // Convert wide `wantBase` to lowercase ASCII inline, then compare.
    size_t i = 0;
    while (wantBase[i] && p[i]) {
        WCHAR w = wantBase[i]; if (w >= L'A' && w <= L'Z') w += 32;
        UCHAR c = p[i];        if (c >= 'A'  && c <= 'Z')  c += 32;
        if ((UCHAR)w != c) return FALSE;
        i++;
    }
    // `wantBase` exhausted → check that what follows in `p` is nothing or a '.ext'.
    if (wantBase[i] != 0) return FALSE;
    UCHAR next = p[i];
    return next == 0 || next == '.';
}

// Allocate + fill AUX_MODULE_EXTENDED_INFO[] array via AuxKlib.  Caller frees.
static PAUX_MODULE_EXTENDED_INFO QueryAllModules(ULONG* OutCount) {
    *OutCount = 0;
    NTSTATUS st = AuxKlibInitialize();
    if (!NT_SUCCESS(st)) return nullptr;
    ULONG bufSize = 0;
    st = AuxKlibQueryModuleInformation(&bufSize,
                                       sizeof(AUX_MODULE_EXTENDED_INFO), nullptr);
    if (!NT_SUCCESS(st) || bufSize == 0) return nullptr;
    auto* info = (PAUX_MODULE_EXTENDED_INFO)
        ExAllocatePool2(POOL_FLAG_NON_PAGED, bufSize, 'tXDx');
    if (!info) return nullptr;
    st = AuxKlibQueryModuleInformation(&bufSize,
                                       sizeof(AUX_MODULE_EXTENDED_INFO), info);
    if (!NT_SUCCESS(st)) {
        ExFreePoolWithTag(info, 'tXDx');
        return nullptr;
    }
    *OutCount = bufSize / sizeof(AUX_MODULE_EXTENDED_INFO);
    return info;
}

UINT64 DiscoverModuleBase(const WCHAR* BaseName, UINT32* OutSize) {
    if (OutSize) *OutSize = 0;
    ULONG count = 0;
    auto* info = QueryAllModules(&count);
    if (!info) return 0;

    UINT64 found = 0;
    for (ULONG i = 0; i < count; i++) {
        if (BaseNameMatches(info[i].FullPathName, info[i].FileNameOffset, BaseName)) {
            found = (UINT64)info[i].BasicInfo.ImageBase;
            if (OutSize) *OutSize = info[i].ImageSize;
            break;
        }
    }
    ExFreePoolWithTag(info, 'tXDx');
    return found;
}

// Parse a loaded PE's export table to find `SymbolName`.
static UINT64 FindExportInImage(UINT64 moduleBase, const char* symbolName) {
    if (!moduleBase) return 0;
    __try {
        auto* dos = (IMAGE_DOS_HEADER*)moduleBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = (IMAGE_NT_HEADERS64*)(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress) return 0;
        auto* exp   = (IMAGE_EXPORT_DIRECTORY*)(moduleBase + dir.VirtualAddress);
        auto* names = (ULONG*)(moduleBase + exp->AddressOfNames);
        auto* ords  = (USHORT*)(moduleBase + exp->AddressOfNameOrdinals);
        auto* funcs = (ULONG*)(moduleBase + exp->AddressOfFunctions);
        for (ULONG i = 0; i < exp->NumberOfNames; i++) {
            const char* n = (const char*)(moduleBase + names[i]);
            int j = 0;
            while (symbolName[j] && symbolName[j] == n[j]) j++;
            if (symbolName[j] == 0 && n[j] == 0) {
                return moduleBase + funcs[ords[i]];
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

UINT64 DiscoverExport(const WCHAR* ModuleBaseName, const char* SymbolName) {
    UINT32 size = 0;
    UINT64 base = DiscoverModuleBase(ModuleBaseName, &size);
    if (!base) return 0;
    return FindExportInImage(base, SymbolName);
}

UINT64 DiscoverSig(const WCHAR* ModuleBaseName,
                   const UCHAR* Pattern, const char* Mask)
{
    UINT32 size = 0;
    UINT64 base = DiscoverModuleBase(ModuleBaseName, &size);
    if (!base || !size) return 0;

    __try {
        auto* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        UCHAR* textBase = nullptr; SIZE_T textSize = 0;
        for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if (!memcmp(sec[i].Name, ".text", 5)) {
                textBase = (UCHAR*)base + sec[i].VirtualAddress;
                textSize = sec[i].Misc.VirtualSize;
                break;
            }
        }
        if (!textBase) return 0;

        SIZE_T patLen = 0; while (Mask[patLen]) patLen++;
        for (SIZE_T i = 0; i + patLen <= textSize; i++) {
            bool ok = true;
            for (SIZE_T j = 0; j < patLen; j++) {
                if (Mask[j] == 'x' && textBase[i + j] != Pattern[j]) { ok = false; break; }
            }
            if (ok) return (UINT64)(textBase + i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

UINT64 DiscoverRipRelative(UINT64 instrVa, ULONG dispOffsetInInstr, ULONG instrLen) {
    if (!instrVa) return 0;
    __try {
        LONG disp = *(LONG*)(instrVa + dispOffsetInInstr);
        return instrVa + instrLen + disp;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}
