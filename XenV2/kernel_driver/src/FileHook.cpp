/*
 * FileHook.cpp — inline hook on nt!NtCreateFile.
 *
 * Filters file opens whose ObjectAttributes->ObjectName contains
 * "VMware" / "VirtualBox" / "VBox" to return STATUS_OBJECT_NAME_NOT_FOUND,
 * unless the caller is a whitelisted VMware Tools process.
 *
 * Covers EAC's "does C:\Program Files\VMware\... exist?" probe
 * without needing a filesystem mini-filter.
 */

#include "..\include\FileHook.h"
#include "..\include\Discover.h"
#include "..\include\Memory.h"

extern "C" PCHAR PsGetProcessImageFileName(PEPROCESS Process);

#define HOOK_PROLOGUE 12

static UINT64  g_Target    = 0;
static UCHAR   g_Original[HOOK_PROLOGUE] = {};
static PVOID   g_Trampoline = nullptr;

typedef NTSTATUS (NTAPI* PFN_NtCreateFile)(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength);

static PFN_NtCreateFile g_CallOriginal = nullptr;

static BOOLEAN UStrIContains2(PCUNICODE_STRING u, const wchar_t* needle) {
    if (!u || !u->Buffer || !needle) return FALSE;
    USHORT chars = u->Length / sizeof(WCHAR);
    SIZE_T nlen = 0; while (needle[nlen]) nlen++;
    if ((SIZE_T)chars < nlen) return FALSE;
    for (USHORT i = 0; (SIZE_T)i + nlen <= chars; i++) {
        BOOLEAN eq = TRUE;
        for (SIZE_T j = 0; j < nlen; j++) {
            WCHAR a = u->Buffer[i + j];
            WCHAR b = needle[j];
            if (a >= L'A' && a <= L'Z') a = (WCHAR)(a + 32);
            if (b >= L'A' && b <= L'Z') b = (WCHAR)(b + 32);
            if (a != b) { eq = FALSE; break; }
        }
        if (eq) return TRUE;
    }
    return FALSE;
}

static BOOLEAN PathIsForbidden(PCUNICODE_STRING p) {
    if (!p) return FALSE;
    return UStrIContains2(p, L"VMware")     ||
           UStrIContains2(p, L"VirtualBox") ||
           UStrIContains2(p, L"VBox");
}

static BOOLEAN CallerIsTools() {
    PEPROCESS proc = PsGetCurrentProcess();
    if (!proc) return FALSE;
    PCHAR name = PsGetProcessImageFileName(proc);
    if (!name) return FALSE;
    char lower[16] = {};
    for (int i = 0; i < 15; i++) {
        char c = name[i]; if (!c) break;
        if (c >= 'A' && c <= 'Z') c += 32;
        lower[i] = c;
    }
    static const char* allowed[] = {
        "vmtoolsd.exe", "vmwaretray.exe", "vmacthlp.exe",
        "vm3dservice.exe", "vgauthservice.", "vmwareresoluti",
        "services.exe", "system", "lsass.exe", "msiexec.exe",
        "trustedinstalle",  // Windows TrustedInstaller
    };
    for (const char* s : allowed) {
        SIZE_T n = 0; while (s[n]) n++;
        if (n > 15) n = 15;
        BOOLEAN eq = TRUE;
        for (SIZE_T i = 0; i < n; i++) if (lower[i] != s[i]) { eq = FALSE; break; }
        if (eq) return TRUE;
    }
    return FALSE;
}

static NTSTATUS NTAPI HookedNtCreateFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes,
    ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions,
    PVOID EaBuffer, ULONG EaLength)
{
    __try {
        if (ObjectAttributes && ObjectAttributes->ObjectName &&
            PathIsForbidden(ObjectAttributes->ObjectName) &&
            !CallerIsTools())
        {
            if (IoStatusBlock) {
                IoStatusBlock->Status = STATUS_OBJECT_NAME_NOT_FOUND;
                IoStatusBlock->Information = 0;
            }
            if (FileHandle) *FileHandle = NULL;
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    return g_CallOriginal(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock,
        AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions,
        EaBuffer, EaLength);
}

NTSTATUS FileHookInstall() {
    if (g_Target) return STATUS_SUCCESS;
    UINT64 target = DiscoverExport(L"nt", "NtCreateFile");
    if (!target) return STATUS_NOT_FOUND;

    __try {
        UCHAR* p = (UCHAR*)target;
        for (int i = 0; i + 5 < HOOK_PROLOGUE; i++) {
            if (p[i] == 0x48 && (p[i+1] == 0x8B || p[i+1] == 0x89 || p[i+1] == 0x8D) &&
                (p[i+2] & 0xC7) == 0x05) {
                DbgPrint("[FileHook] RIP-relative in prologue; refusing\n");
                return STATUS_NOT_SUPPORTED;
            }
        }
        RtlCopyMemory(g_Original, p, HOOK_PROLOGUE);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_ACCESS_VIOLATION;
    }

    PVOID tramp = ExAllocatePool2(POOL_FLAG_NON_PAGED_EXECUTE, 32, 'fhXt');
    if (!tramp) return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(tramp, g_Original, HOOK_PROLOGUE);
    UCHAR* t = (UCHAR*)tramp + HOOK_PROLOGUE;
    t[0] = 0xFF; t[1] = 0x25; t[2] = 0; t[3] = 0; t[4] = 0; t[5] = 0;
    *(UINT64*)(t + 6) = target + HOOK_PROLOGUE;
    g_Trampoline = tramp;
    g_CallOriginal = (PFN_NtCreateFile)tramp;

    UCHAR patch[HOOK_PROLOGUE];
    patch[0] = 0x48; patch[1] = 0xB8;
    *(UINT64*)(patch + 2) = (UINT64)HookedNtCreateFile;
    patch[10] = 0xFF; patch[11] = 0xE0;

    NTSTATUS ws = WriteKernel(target, patch, HOOK_PROLOGUE);
    if (!NT_SUCCESS(ws)) {
        ExFreePoolWithTag(g_Trampoline, 'fhXt');
        g_Trampoline = nullptr;
        g_CallOriginal = nullptr;
        return ws;
    }
    g_Target = target;
    DbgPrint("[FileHook] installed on nt!NtCreateFile @ 0x%llx\n", target);
    return STATUS_SUCCESS;
}

VOID FileHookRemove() {
    if (!g_Target) return;
    WriteKernel(g_Target, g_Original, HOOK_PROLOGUE);
    if (g_Trampoline) ExFreePoolWithTag(g_Trampoline, 'fhXt');
    g_Trampoline = nullptr;
    g_CallOriginal = nullptr;
    g_Target = 0;
}
