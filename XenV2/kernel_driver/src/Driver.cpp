/*
 * Driver.cpp - XenV2-Type2 kernel driver entry point
 *
 * Type 2 hypervisor + memory access driver for Windows 10/11 x64.
 * Exposes a named device (\Device\XenV2Type2) with IOCTL interface.
 *
 * Loading without test signing:
 *   bcdedit /debug on
 *   bcdedit /dbgsettings net hostip:<IP> port:<PORT> key:<KEY>
 *   sc create XenV2Type2 type= kernel start= demand binPath= C:\path\driver.sys
 *   sc start XenV2Type2
 *   (Kernel debugger must be attached — Windows skips DSE verification in debug mode)
 */

#include <ntifs.h>
#include <wdm.h>
#include <ntimage.h>

// RUNTIME_FUNCTION layout on x64 (not pulled in by ntifs.h in WDM driver)
typedef struct _RUNTIME_FUNCTION_LOCAL {
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG UnwindData;
} RUNTIME_FUNCTION_LOCAL, *PRUNTIME_FUNCTION_LOCAL;

typedef PRUNTIME_FUNCTION_LOCAL (*PGET_RUNTIME_FUNCTION_CALLBACK)(
    ULONG64 ControlPc, PVOID Context);

typedef BOOLEAN (*PFN_RtlAddFunctionTable)(
    PRUNTIME_FUNCTION_LOCAL FunctionTable,
    ULONG                   EntryCount,
    ULONG64                 BaseAddress);

typedef BOOLEAN (*PFN_RtlInstallFunctionTableCallback)(
    ULONG64                        TableIdentifier,
    ULONG64                        BaseAddress,
    ULONG                          Length,
    PGET_RUNTIME_FUNCTION_CALLBACK Callback,
    PVOID                          Context,
    PCWSTR                         OutOfProcessCallbackDll);

// Global state for the callback to return unwind info.
static PRUNTIME_FUNCTION_LOCAL g_OwnRuntimeFunctions = nullptr;
static ULONG                   g_OwnRuntimeFunctionCount = 0;
// Non-static so VmxInit can pull image bounds for EPT cloaking.
extern "C" ULONG64             g_OwnImageBase = 0;
extern "C" ULONG               g_OwnImageSize = 0;

static PRUNTIME_FUNCTION_LOCAL OwnRuntimeFunctionCallback(ULONG64 ControlPc, PVOID /*Ctx*/) {
    // Find the RUNTIME_FUNCTION entry whose [BeginAddress, EndAddress)
    // (RVAs) covers (ControlPc - g_OwnImageBase).
    if (!g_OwnRuntimeFunctions || !g_OwnImageBase) return nullptr;
    ULONG32 rva = (ULONG32)(ControlPc - g_OwnImageBase);
    for (ULONG i = 0; i < g_OwnRuntimeFunctionCount; i++) {
        if (rva >= g_OwnRuntimeFunctions[i].BeginAddress &&
            rva <  g_OwnRuntimeFunctions[i].EndAddress) {
            return &g_OwnRuntimeFunctions[i];
        }
    }
    return nullptr;
}
#include "..\include\Vmx.h"
#include "..\include\Ept.h"
#include "..\include\Ioctl.h"
#include "..\include\Hide.h"
#include "..\include\Discover.h"
#include "..\..\shared\Shared.h"

// AutoDisableDse / SysInfoHookInstall / RegFilterInstall / FileHookInstall
// are NOT invoked from DriverEntry — they are gated behind IOCTLs that the
// usermode client triggers after the protected game has finished its
// anti-cheat initialisation.  See Ioctl.cpp : HandleActivate().

// Prototype for nt!IoCreateDriver (exported from ntoskrnl).
extern "C" NTSTATUS IoCreateDriver(
    _In_opt_ PUNICODE_STRING    DriverName,
    _In_     PDRIVER_INITIALIZE InitializationFunction);

extern "C" NTSTATUS RealDriverEntry(PDRIVER_OBJECT, PUNICODE_STRING);

// Manual-mapped drivers aren't in PsLoadedModuleList, so the kernel's
// exception dispatcher (RtlLookupFunctionEntry) can't find our .pdata
// when a fault happens inside our code — every __try/__except becomes
// a silent bugcheck instead of a catch.  Fix by registering our
// exception tables explicitly with RtlAddFunctionTable at load time.
//
// NOTE: `&__ImageBase` in a manual-mapped driver is NOT our actual
// load address — it's the PE's compile-time preferred base
// (~0x140000000), which points at user-mode garbage in kernel mode.
// We locate the real image base by walking back from a known code
// address looking for the DOS/NT signature.
static BOOLEAN g_FunctionTableRegistered = FALSE;

static PVOID FindOwnImageBase(PVOID anchorFn) {
    // kdmapper maps the image contiguously so walking backward in page
    // strides stays within readable kernel pool.
    UINT64 p = (UINT64)anchorFn & ~0xFFFULL;
    for (ULONG i = 0; i < 4096; i++, p -= 0x1000) {
        if (*(USHORT*)p == IMAGE_DOS_SIGNATURE) {
            auto* dos = (IMAGE_DOS_HEADER*)p;
            if (dos->e_lfanew > 0 && dos->e_lfanew < 0x400) {
                auto* nt = (IMAGE_NT_HEADERS64*)(p + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE &&
                    nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
                    return (PVOID)p;
                }
            }
        }
    }
    return nullptr;
}

static VOID RegisterOwnFunctionTable() {
    if (g_FunctionTableRegistered) return;
    PVOID base = FindOwnImageBase((PVOID)&RealDriverEntry);
    if (!base) {
        DbgPrint("[XenV2-Type2] Own image base not found — SEH disabled.\n");
        return;
    }
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt  = (IMAGE_NT_HEADERS64*)((UCHAR*)base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (dir.VirtualAddress == 0 || dir.Size == 0) {
        DbgPrint("[XenV2-Type2] No .pdata directory — SEH disabled.\n");
        return;
    }
    auto* funcs = (PRUNTIME_FUNCTION_LOCAL)((UCHAR*)base + dir.VirtualAddress);
    ULONG count = dir.Size / sizeof(RUNTIME_FUNCTION_LOCAL);

    // Stash for the callback path.
    g_OwnRuntimeFunctions     = funcs;
    g_OwnRuntimeFunctionCount = count;
    g_OwnImageBase            = (ULONG64)base;
    g_OwnImageSize            = nt->OptionalHeader.SizeOfImage;

    ULONG imageSize = g_OwnImageSize;

    // Try MmGetSystemRoutineAddress first.  If it doesn't know the name
    // (some builds restrict its allowlist), fall back to walking the
    // ntoskrnl export table ourselves via DiscoverExport.
    auto resolveNt = [](const char* name, PVOID* outFn) -> bool {
        WCHAR wname[128] = {};
        for (SIZE_T i = 0; name[i] && i < 127; i++) wname[i] = (WCHAR)name[i];
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, wname);
        *outFn = MmGetSystemRoutineAddress(&us);
        if (*outFn) return true;
        UINT64 addr = DiscoverExport(L"ntoskrnl", name);
        if (addr) { *outFn = (PVOID)addr; return true; }
        return false;
    };

    PVOID p = nullptr;

    if (resolveNt("RtlAddFunctionTable", &p)) {
        auto fnAdd = (PFN_RtlAddFunctionTable)p;
        if (fnAdd(funcs, count, (ULONG64)base)) {
            g_FunctionTableRegistered = TRUE;
            DbgPrint("[XenV2-Type2] Registered via RtlAddFunctionTable: %u entries @ 0x%p.\n",
                     count, base);
            return;
        }
    }

    if (!resolveNt("RtlInstallFunctionTableCallback", &p)) {
        DbgPrint("[XenV2-Type2] Neither table API found in ntoskrnl (even via PE export scan) — SEH disabled.\n");
        return;
    }
    auto fnCb = (PFN_RtlInstallFunctionTableCallback)p;
    ULONG64 tableId = (ULONG64)base | 3;  // callback-mode marker
    BOOLEAN ok = fnCb(tableId, (ULONG64)base, imageSize,
                     OwnRuntimeFunctionCallback, nullptr, nullptr);
    if (ok) {
        g_FunctionTableRegistered = TRUE;
        DbgPrint("[XenV2-Type2] Registered via RtlInstallFunctionTableCallback @ 0x%p (SEH live).\n",
                 base);
    } else {
        DbgPrint("[XenV2-Type2] RtlInstallFunctionTableCallback failed.\n");
    }
}

// Global VMX and EPT state — allocated in DriverEntry, freed in DriverUnload
VmxGlobal* g_Vmx = nullptr;
EptState*  g_Ept = nullptr;
PDEVICE_OBJECT g_DeviceObject = nullptr;
// Captured at DriverEntry.  kdmapper passes a transient DriverObject that
// is freed after DriverEntry returns — our deferred worker can't walk
// devObj->DriverObject safely.  Stash the pointer here; the memory it
// pointed to MAY be freed, but on SCM-load paths it stays valid, and on
// manual-map paths HideDriver handles NULL DriverSection gracefully.
PDRIVER_OBJECT g_DriverObject = nullptr;

// ============================================================
//  Forward declarations
// ============================================================
DRIVER_UNLOAD DriverUnload;
DRIVER_DISPATCH IrpCreate;
DRIVER_DISPATCH IrpClose;

extern "C" DRIVER_INITIALIZE DriverEntry;

// ============================================================
//  IRP_MJ_CREATE — usermode called CreateFile on our device
//  IRP_MJ_CLOSE  — usermode called CloseHandle
//
//  Anti-cheat probes can hand us malformed IRPs (NULL or partially-
//  initialized).  SEH-wrap so a bad pointer deref returns cleanly
//  instead of bugchecking via the partial .pdata path.
// ============================================================
NTSTATUS IrpCreate(PDEVICE_OBJECT /*DeviceObject*/, PIRP Irp) {
    __try {
        Irp->IoStatus.Status      = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

NTSTATUS IrpClose(PDEVICE_OBJECT /*DeviceObject*/, PIRP Irp) {
    __try {
        Irp->IoStatus.Status      = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}

// ============================================================
//  DriverUnload — tear down VMX, free allocations, delete device
// ============================================================
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    // If the hypervisor is live, we CANNOT safely unload — the VmExitStub code
    // pages would be unmapped and the next VM exit would jump to invalid memory.
    // Keep running until reboot.
    if (g_Vmx && g_Vmx->Initialized) {
        DbgPrint("[XenV2-Type2] Unload requested while VMX is live — refusing, reboot required.\n");
        return;
    }

    // Safe unload path (VMX was never started)
    if (g_Ept) {
        EptShutdown();
    }
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, XENTYPE2_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symLink);
    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
    if (g_Vmx) {
        ExFreePoolWithTag(g_Vmx, XENTYPE2_POOL_TAG);
        g_Vmx = nullptr;
    }
    DbgPrint("[XenV2-Type2] Driver unloaded cleanly.\n");
}

// ============================================================
//  DriverEntry — thin wrapper.  kdmapper calls this with NULL,NULL.
//  In that case we use IoCreateDriver to obtain a real tracked
//  DriverObject (with OBJECT_HEADER, ref-count, etc.) and re-enter
//  via RealDriverEntry with a valid first arg.  SCM loads call
//  through directly.
// ============================================================
extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    if (!DriverObject) {
        DbgPrint("[XenV2-Type2] Manual-map bootstrap — calling IoCreateDriver.\n");
        UNICODE_STRING drvName;
        // Use a neutral, common-looking name to avoid giving EAC a trivial
        // string match in \Driver\ directory listings.
        // Camouflage name; matches the device/symlink in Shared.h.
        // Anti-cheats enumerate \Driver\ for unsigned manual-mapped
        // entries — bland Microsoft-shaped names attract less attention.
        RtlInitUnicodeString(&drvName, L"\\Driver\\WdiSysHost1");
        NTSTATUS st = IoCreateDriver(&drvName, RealDriverEntry);
        DbgPrint("[XenV2-Type2] IoCreateDriver returned 0x%x.\n", st);
        return st;
    }
    return RealDriverEntry(DriverObject, RegistryPath);
}

// ============================================================
//  RealDriverEntry — original entry logic; receives a valid DriverObject
// ============================================================
extern "C"
NTSTATUS RealDriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING /*RegistryPath*/) {
    NTSTATUS       status;
    PDEVICE_OBJECT deviceObject = nullptr;

    DbgPrint("[XenV2-Type2] RealDriverEntry start.\n");
    g_DriverObject = DriverObject;

    // CRITICAL: register our .pdata with the kernel so SEH works in our
    // code paths.  Manual-mapped drivers otherwise have every __try
    // silently escape into bugcheck 0x1E on any fault.
    RegisterOwnFunctionTable();

    // --- Set dispatch routines ---
    // DriverUnload refuses to unload if the hypervisor is running, but allows
    // clean unload otherwise.  Enables fast iteration on non-VMX features.
    DriverObject->DriverUnload                         = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = IrpCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = IrpClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoctlDispatch;

    // --- Allocate global VMX state ---
    g_Vmx = (VmxGlobal*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(VmxGlobal), XENTYPE2_POOL_TAG);
    if (!g_Vmx) {
        DbgPrint("[XenV2-Type2] Failed to allocate VmxGlobal.\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(g_Vmx, sizeof(VmxGlobal));

    // Capture HOST CR3 by explicitly attaching to the System process
    // (PID 4) which exists for the lifetime of the OS.  DriverEntry may
    // run in a driver-loader worker thread whose process could exit; the
    // System process's PML4 is stable.  Any process's PML4 has the
    // kernel mapped at upper-half VAs, so using System's CR3 as
    // HOST_CR3 keeps our VM-exit handlers reachable indefinitely.
    {
        KAPC_STATE apc;
        KeStackAttachProcess(PsInitialSystemProcess, &apc);
        g_Vmx->HostCr3 = __readcr3();
        KeUnstackDetachProcess(&apc);
    }
    DbgPrint("[XenV2-Type2] Captured HostCr3 = 0x%llX (System process).\n", g_Vmx->HostCr3);

    // --- Create device object ---
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, XENTYPE2_DEVICE_NAME);

    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );
    if (!NT_SUCCESS(status)) {
        DbgPrint("[XenV2-Type2] IoCreateDevice failed: 0x%X\n", status);
        ExFreePoolWithTag(g_Vmx, XENTYPE2_POOL_TAG);
        g_Vmx = nullptr;
        return status;
    }
    deviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject = deviceObject;

    // --- Create symbolic link (usermode will open \\.\XenV2Type2) ---
    UNICODE_STRING symLink;
    RtlInitUnicodeString(&symLink, XENTYPE2_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symLink, &deviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[XenV2-Type2] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(deviceObject);
        ExFreePoolWithTag(g_Vmx, XENTYPE2_POOL_TAG);
        g_Vmx = nullptr;
        return status;
    }

    // VMX and EPT are started on demand via IOCTL_VMX_START.
    // Keeping them out of DriverEntry prevents hangs when loaded via kdmapper
    // in nested-virtualisation environments where VMXON behaviour is uncertain.

    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    // ---- Dormant-on-load policy ----
    //
    // We deliberately DO NOT perform any of the following automatically
    // at DriverEntry or via a deferred worker:
    //   - Auto-DSE (g_CiOptions patch) — EAC reads the CI state during init
    //   - Self-hide (PsLoadedModuleList unlink, DriverName scrub, etc.)
    //   - RegFilter install (CmRegisterCallbackEx)
    //   - Any inline hook (SysInfoHook, FileHook)
    //
    // Any of those touch kernel state in ways EAC's boot-time scan can see
    // and use to flag us.  Instead, the driver stays fully passive until
    // usermode issues IOCTL_ACTIVATE after the game is already running and
    // EAC has finished its initial checks.  At that point the user-triggered
    // memory reads go through CR3 page-walk (no syscalls, no hooks EAC is
    // watching), so activation is invisible post-init.
    //
    // This trades on-boot stealth depth for "invisible during EAC init".
    // For a protected game, that's the right trade.
    BOOLEAN manualMapped = (DriverObject->DriverSection == nullptr);
    DbgPrint("[XenV2-Type2] Load mode: %s (dormant)\n",
             manualMapped ? "MANUAL-MAP" : "SCM-LOADED");

    DbgPrint("[XenV2-Type2] Device ready — IOCTL surface live; no stealth activity until IOCTL_ACTIVATE.\n");
    return STATUS_SUCCESS;
}
