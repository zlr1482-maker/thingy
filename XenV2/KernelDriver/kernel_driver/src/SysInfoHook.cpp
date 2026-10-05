/*
 * SysInfoHook.cpp — NtQuerySystemInformation inline hook.
 *
 * Scrubs VM-detection signals from two info classes on the return path:
 *   - SystemModuleInformation        (0x0B)
 *   - SystemFirmwareTableInformation (0x4C)
 *
 * Hook technique: 12-byte prologue overwrite with an absolute indirect jump
 * (`movabs rax, <hook>; jmp rax`).  The original 12 bytes are copied into a
 * trampoline buffer that the hook calls into for "real behaviour", followed
 * by a `jmp [rip+0]` back to (target + 12).  We refuse to install if the
 * first 12 bytes contain any RIP-relative instruction — none of the Win10
 * 26100 builds inspected have RIP-relative in the first 12 bytes of
 * NtQuerySystemInformation, but we fail-safe anyway.
 */

#include "..\include\SysInfoHook.h"
#include "..\include\Discover.h"
#include "..\include\Memory.h"

#define NQSI_PROLOGUE_BYTES 12

// Filled by SysInfoHookInstall.
static UINT64  g_NqsiTarget    = 0;
static UCHAR   g_NqsiOriginal[NQSI_PROLOGUE_BYTES] = {};
static PVOID   g_TrampolineVa  = nullptr;

// Type of the real function we're hooking.
typedef NTSTATUS (NTAPI* PFN_NQSI)(
    ULONG   SystemInformationClass,
    PVOID   SystemInformation,
    ULONG   SystemInformationLength,
    PULONG  ReturnLength);

// Type of our trampoline callable (points at the copied prologue + jmp-back).
static PFN_NQSI g_CallOriginal = nullptr;

// ---- Known info classes we need to filter on ----------------------------
enum {
    SystemModuleInformation          = 0x0B,
    SystemFirmwareTableInformation   = 0x4C,
};

// The RTL_PROCESS_MODULE_INFORMATION array layout returned by class 0x0B.
// Stable since Windows 7.
#pragma pack(push, 1)
typedef struct _RTL_PROCESS_MODULE_INFORMATION {
    HANDLE  Section;
    PVOID   MappedBase;
    PVOID   ImageBase;
    ULONG   ImageSize;
    ULONG   Flags;
    USHORT  LoadOrderIndex;
    USHORT  InitOrderIndex;
    USHORT  LoadCount;
    USHORT  OffsetToFileName;
    UCHAR   FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES {
    ULONG                          NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

typedef struct _XEN_FIRMWARE_TABLE_INFORMATION {
    ULONG  ProviderSignature;
    ULONG  Action;
    ULONG  TableID;
    ULONG  TableBufferLength;
    UCHAR  TableBuffer[1];
} XEN_FIRMWARE_TABLE_INFORMATION, *PXEN_FIRMWARE_TABLE_INFORMATION;
#pragma pack(pop)

// ---- Helpers ------------------------------------------------------------
static BOOLEAN AsciiIContains(const UCHAR* hay, SIZE_T hayLen,
                              const char*  needle, SIZE_T needleLen)
{
    if (needleLen == 0 || hayLen < needleLen) return FALSE;
    for (SIZE_T i = 0; i + needleLen <= hayLen; i++) {
        BOOLEAN match = TRUE;
        for (SIZE_T j = 0; j < needleLen; j++) {
            UCHAR a = hay[i + j];
            UCHAR b = (UCHAR)needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

static SIZE_T CStrLen(const char* s) { SIZE_T n = 0; while (s[n]) n++; return n; }

// Compact an array of RTL_PROCESS_MODULE_INFORMATION: remove entries whose
// FullPathName contains any VMware-related driver name.
static ULONG FilterModuleArray(PRTL_PROCESS_MODULES modules)
{
    if (!modules) return 0;
    static const char* tags[] = {
        "vmci.sys", "vmhgfs.sys", "vm3dmp.sys", "vmmemctl.sys",
        "vmmouse.sys", "vmusbmouse.sys", "vmx_svga.sys", "vmsvga.sys",
        "vmxnet3.sys", "vmnetbridge.sys", "VBoxGuest.sys", "VBoxMouse.sys",
        "VBoxVideo.sys", "VBoxSF.sys"
    };
    ULONG src = 0, dst = 0, removed = 0;
    ULONG n = modules->NumberOfModules;
    while (src < n) {
        auto& m = modules->Modules[src];
        BOOLEAN hide = FALSE;
        USHORT pathLen = 0; while (pathLen < 256 && m.FullPathName[pathLen]) pathLen++;
        for (const char* t : tags) {
            if (AsciiIContains(m.FullPathName, pathLen, t, CStrLen(t))) {
                hide = TRUE; break;
            }
        }
        if (!hide) {
            if (dst != src) modules->Modules[dst] = m;
            dst++;
        } else {
            removed++;
        }
        src++;
    }
    modules->NumberOfModules = dst;
    return removed;
}

// SMBIOS-strings scrub: replace "VMware, Inc." / "VMware Virtual Platform"
// / "VMware7,1" / "VMWARE" in-place with neutral OEM strings.  Preserves
// buffer length by padding with the neutral string (same length or shorter +
// NULs).  Inline replace; SMBIOS strings are NUL-terminated.
static ULONG ScrubFirmwareBuffer(UCHAR* buf, ULONG len)
{
    struct Rep { const char* from; const char* to; };
    static const Rep reps[] = {
        { "VMware, Inc.",             "Intel Corporation" },
        { "VMware Virtual Platform",  "Desktop OEM"       },
        { "VMware7,1",                "Board"             },
        { "VMware7",                  "Board"             },
        { "VMware",                   "Generic"           },
        { "VMware SVGA II",           "Standard VGA"      },
        { "VMware SATA AHCI",         "Standard AHCI"     },
        { "innotek GmbH",             "Generic OEM"       },
        { "VirtualBox",               "Desktop"           },
    };
    ULONG hits = 0;
    for (const auto& r : reps) {
        SIZE_T fl = CStrLen(r.from);
        SIZE_T tl = CStrLen(r.to);
        if (tl > fl) continue;  // never expand
        for (ULONG i = 0; i + fl <= len; i++) {
            BOOLEAN eq = TRUE;
            for (SIZE_T j = 0; j < fl; j++) {
                UCHAR a = buf[i + j], b = (UCHAR)r.from[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { eq = FALSE; break; }
            }
            if (eq) {
                // Write replacement, pad the tail with NULs.
                for (SIZE_T j = 0; j < tl; j++) buf[i + j] = (UCHAR)r.to[j];
                for (SIZE_T j = tl; j < fl; j++) buf[i + j] = 0;
                hits++;
                i += (ULONG)fl - 1;  // skip past; loop post-inc advances by 1
            }
        }
    }
    return hits;
}

// ---- Our hook entry -----------------------------------------------------
static NTSTATUS NTAPI HookedNqsi(
    ULONG   infoClass,
    PVOID   info,
    ULONG   infoLen,
    PULONG  retLen)
{
    NTSTATUS st = g_CallOriginal(infoClass, info, infoLen, retLen);

    if (NT_SUCCESS(st) && info) {
        __try {
            if (infoClass == SystemModuleInformation && infoLen >= sizeof(ULONG)) {
                FilterModuleArray((PRTL_PROCESS_MODULES)info);
            } else if (infoClass == SystemFirmwareTableInformation &&
                       infoLen >= sizeof(XEN_FIRMWARE_TABLE_INFORMATION)) {
                auto* fti = (PXEN_FIRMWARE_TABLE_INFORMATION)info;
                if (fti->TableBufferLength &&
                    fti->TableBufferLength <= infoLen -
                        FIELD_OFFSET(XEN_FIRMWARE_TABLE_INFORMATION, TableBuffer))
                {
                    ScrubFirmwareBuffer(fti->TableBuffer, fti->TableBufferLength);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return st;
}

// ---- Install / remove ---------------------------------------------------
NTSTATUS SysInfoHookInstall() {
    if (g_NqsiTarget) return STATUS_SUCCESS;  // already installed

    UINT64 target = DiscoverExport(L"nt", "NtQuerySystemInformation");
    if (!target) {
        DbgPrint("[SysInfoHook] NtQuerySystemInformation not resolved.\n");
        return STATUS_NOT_FOUND;
    }

    // Sanity: check first 12 bytes don't contain a RIP-relative instruction.
    // Rough test: reject if opcode 0x8B/0x89/0x48 8B/0x48 8D with ModR/M low
    // 3 bits == 101 appears.  A precise disassembler would be correct; this
    // approximation catches the common cases.  If the function has any
    // RIP-relative ref in prologue, punt.
    __try {
        UCHAR* p = (UCHAR*)target;
        for (int i = 0; i + 5 < NQSI_PROLOGUE_BYTES; i++) {
            if (p[i] == 0x48 && (p[i+1] == 0x8B || p[i+1] == 0x89 || p[i+1] == 0x8D) &&
                (p[i+2] & 0xC7) == 0x05) {
                DbgPrint("[SysInfoHook] RIP-relative in prologue @ +%d; refusing\n", i);
                return STATUS_NOT_SUPPORTED;
            }
        }
        RtlCopyMemory(g_NqsiOriginal, p, NQSI_PROLOGUE_BYTES);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_ACCESS_VIOLATION;
    }

    // Allocate trampoline: [original 12 bytes][jmp qword ptr [rip+0]][target+12 qword]
    // Trampoline layout: 12 + 6 (jmp [rip+0]) + 8 (absolute addr) = 26 bytes.
    PVOID tramp = ExAllocatePool2(POOL_FLAG_NON_PAGED_EXECUTE, 32, 'pmXt');
    if (!tramp) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(tramp, g_NqsiOriginal, NQSI_PROLOGUE_BYTES);
    UCHAR* t = (UCHAR*)tramp + NQSI_PROLOGUE_BYTES;
    // jmp qword ptr [rip+0]
    t[0] = 0xFF; t[1] = 0x25; t[2] = 0; t[3] = 0; t[4] = 0; t[5] = 0;
    *(UINT64*)(t + 6) = target + NQSI_PROLOGUE_BYTES;
    g_TrampolineVa = tramp;
    g_CallOriginal = (PFN_NQSI)tramp;

    // Patch target: movabs rax, <HookedNqsi>; jmp rax   (12 bytes)
    UCHAR patch[NQSI_PROLOGUE_BYTES];
    patch[0] = 0x48; patch[1] = 0xB8;
    *(UINT64*)(patch + 2) = (UINT64)HookedNqsi;
    patch[10] = 0xFF; patch[11] = 0xE0;

    NTSTATUS ws = WriteKernel(target, patch, NQSI_PROLOGUE_BYTES);
    if (!NT_SUCCESS(ws)) {
        ExFreePoolWithTag(g_TrampolineVa, 'pmXt');
        g_TrampolineVa = nullptr;
        g_CallOriginal = nullptr;
        DbgPrint("[SysInfoHook] prologue write failed 0x%x\n", ws);
        return ws;
    }

    g_NqsiTarget = target;
    DbgPrint("[SysInfoHook] installed on nt!NtQuerySystemInformation @ 0x%llx\n", target);
    return STATUS_SUCCESS;
}

VOID SysInfoHookRemove() {
    if (!g_NqsiTarget) return;
    WriteKernel(g_NqsiTarget, g_NqsiOriginal, NQSI_PROLOGUE_BYTES);
    if (g_TrampolineVa) ExFreePoolWithTag(g_TrampolineVa, 'pmXt');
    g_TrampolineVa = nullptr;
    g_CallOriginal = nullptr;
    g_NqsiTarget   = 0;
}
