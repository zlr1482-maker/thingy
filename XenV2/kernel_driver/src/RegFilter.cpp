/*
 * RegFilter.cpp — CmRegisterCallbackEx-based registry stealth.
 *
 * Blocks any open/enumerate/query on a key whose path contains
 * "VMware" / "VirtualBox" / "VBox" by returning STATUS_OBJECT_NAME_NOT_FOUND.
 * Whitelists VMware Tools processes so Tools itself keeps working.
 *
 * Installed from the Auto-DSE work item after the driver has a stable
 * DriverObject (kdmapper-synthesised objects are sufficient for
 * CmRegisterCallbackEx).
 */

#include "..\include\RegFilter.h"

extern "C" PCHAR PsGetProcessImageFileName(PEPROCESS Process);

static LARGE_INTEGER g_RegCookie = {};
static BOOLEAN       g_Installed = FALSE;

// ---- helpers ------------------------------------------------------------
static BOOLEAN UStrIContains(PCUNICODE_STRING u, const wchar_t* needle) {
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

static BOOLEAN PathMentionsVM(PCUNICODE_STRING path) {
    if (!path) return FALSE;
    return UStrIContains(path, L"VMware")     ||
           UStrIContains(path, L"VirtualBox") ||
           UStrIContains(path, L"VBox")       ||
           UStrIContains(path, L"vmci")       ||
           UStrIContains(path, L"vmhgfs")     ||
           UStrIContains(path, L"vmmouse")    ||
           UStrIContains(path, L"vm3dmp")     ||
           UStrIContains(path, L"vmxnet")     ||
           // PCI enumeration: block VMware's (0x15AD) and VirtualBox's
           // (0x80EE) vendor IDs so reading HKLM\SYSTEM\...\Enum\PCI\
           // returns STATUS_OBJECT_NAME_NOT_FOUND on these devices.
           UStrIContains(path, L"VEN_15AD")   ||
           UStrIContains(path, L"VEN_80EE");
}

static BOOLEAN CallerIsVMwareTools() {
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
        // --- In-guest VMware Tools processes ---
        "vmtoolsd.exe", "vmwaretray.exe", "vmacthlp.exe",
        "vm3dservice.exe", "vgauthservice.", "vmwareresoluti",
        // --- Host-side VMware Workstation (don't break host when
        //     our driver is loaded on the host for bare-metal testing) ---
        "vmware.exe",        // GUI
        "vmware-vmx.exe",    // per-VM backend
        "vmware-tray.ex",    // (truncated) VMware Workstation tray
        "vmware-authd.e",    // auth daemon
        "vmware-hostd.e",    // hostd service
        "vmnat.exe",         // NAT daemon
        "vmnetdhcp.exe",     // DHCP daemon
        "vmrun.exe",         // CLI
        // --- Windows core ---
        "services.exe",      // SCM legitimately walks VMware service keys
        "system",            // kernel-side lookups (ImageFileName "System")
        "lsass.exe",
        "svchost.exe",       // many legit Windows services
        "wininit.exe",
        "csrss.exe",
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

// ---- pre-op helpers: extract the key path ------------------------------
static NTSTATUS GetKeyPath(PVOID arg2, REG_NOTIFY_CLASS cls,
                           PUNICODE_STRING out, WCHAR* buf, ULONG bufChars)
{
    out->Buffer = buf;
    out->MaximumLength = (USHORT)(bufChars * sizeof(WCHAR));
    out->Length = 0;

    switch (cls) {
        case RegNtPreCreateKeyEx: {
            auto* info = (REG_CREATE_KEY_INFORMATION*)arg2;
            if (info && info->CompleteName) {
                USHORT n = info->CompleteName->Length / sizeof(WCHAR);
                if (n > bufChars - 1) n = (USHORT)(bufChars - 1);
                RtlCopyMemory(buf, info->CompleteName->Buffer, n * sizeof(WCHAR));
                buf[n] = 0;
                out->Length = (USHORT)(n * sizeof(WCHAR));
                return STATUS_SUCCESS;
            }
            break;
        }
        case RegNtPreOpenKeyEx: {
            auto* info = (REG_OPEN_KEY_INFORMATION*)arg2;
            if (info && info->CompleteName) {
                USHORT n = info->CompleteName->Length / sizeof(WCHAR);
                if (n > bufChars - 1) n = (USHORT)(bufChars - 1);
                RtlCopyMemory(buf, info->CompleteName->Buffer, n * sizeof(WCHAR));
                buf[n] = 0;
                out->Length = (USHORT)(n * sizeof(WCHAR));
                return STATUS_SUCCESS;
            }
            break;
        }
        default: break;
    }
    return STATUS_NOT_FOUND;
}

// ---- the callback -------------------------------------------------------
static NTSTATUS RegCallback(PVOID /*CallbackContext*/, PVOID arg1, PVOID arg2) {
    REG_NOTIFY_CLASS cls = (REG_NOTIFY_CLASS)(ULONG_PTR)arg1;

    if (cls != RegNtPreOpenKeyEx && cls != RegNtPreCreateKeyEx)
        return STATUS_SUCCESS;

    WCHAR buf[520];
    UNICODE_STRING path = {};
    if (!NT_SUCCESS(GetKeyPath(arg2, cls, &path, buf, RTL_NUMBER_OF(buf))))
        return STATUS_SUCCESS;

    if (!PathMentionsVM(&path)) return STATUS_SUCCESS;
    if (CallerIsVMwareTools())  return STATUS_SUCCESS;

    // Block — tell the kernel the key doesn't exist.
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

// ---- install / remove ---------------------------------------------------
NTSTATUS RegFilterInstall(PDRIVER_OBJECT DriverObject) {
    if (g_Installed) return STATUS_SUCCESS;
    UNICODE_STRING altitude;
    RtlInitUnicodeString(&altitude, L"360000.1");

    NTSTATUS st = CmRegisterCallbackEx(
        RegCallback,
        &altitude,
        DriverObject,
        nullptr,
        &g_RegCookie,
        nullptr);
    if (NT_SUCCESS(st)) {
        g_Installed = TRUE;
        DbgPrint("[RegFilter] registered, cookie=0x%llx\n", g_RegCookie.QuadPart);
    } else {
        DbgPrint("[RegFilter] CmRegisterCallbackEx failed 0x%x\n", st);
    }
    return st;
}

VOID RegFilterRemove() {
    if (!g_Installed) return;
    CmUnRegisterCallback(g_RegCookie);
    g_Installed = FALSE;
}
