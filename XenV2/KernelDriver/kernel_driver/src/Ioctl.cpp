/*
 * Ioctl.cpp - IOCTL dispatch and handlers
 *
 * All IOCTLs use METHOD_BUFFERED.  The kernel copies input from
 * Irp->AssociatedIrp.SystemBuffer and writes output to the same buffer.
 * Irp->IoStatus.Information must be set to the number of bytes written.
 */

#include <ntifs.h>
#include "..\include\Ioctl.h"
#include "..\include\Memory.h"
#include "..\include\Vmx.h"
#include "..\include\Ept.h"
#include "..\include\EptTrace.h"
#include "..\include\RemoteOps.h"
#include "..\include\Discover.h"
#include "..\include\Hide.h"
#include "..\include\RegFilter.h"
#include "..\..\shared\Shared.h"
extern PDRIVER_OBJECT g_DriverObject;

extern VmxGlobal* g_Vmx;
extern EptState*  g_Ept;

static UINT64 FindGCiOptions();

// ============================================================
//  Macro to complete an IRP with a given status + bytes written
// ============================================================
#define COMPLETE_IRP(irp, status, info) \
    do {                                \
        (irp)->IoStatus.Status      = (status); \
        (irp)->IoStatus.Information = (info);   \
        IoCompleteRequest((irp), IO_NO_INCREMENT); \
        return (status);                \
    } while (0)

// ============================================================
//  IOCTL_READ_PROCESS_MEMORY
// ============================================================
static NTSTATUS HandleReadMemory(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inLen < sizeof(ReadMemoryRequest))
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    ReadMemoryRequest* req = (ReadMemoryRequest*)irp->AssociatedIrp.SystemBuffer;

    // Output buffer must be at least sizeof(ReadMemoryResponse) + req->Size bytes
    ULONG needed = sizeof(ReadMemoryResponse) - 1 + (ULONG)req->Size;
    if (outLen < needed)
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    if (req->Size == 0 || req->Size > 0x10000000) // Sanity: max 256MB per call
        COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);

    // CAPTURE req fields before we start writing into the shared SystemBuffer.
    // req and resp overlap: resp->Buffer (offset 8) will be overwritten, and the
    // write extends into req->Address (offset 8) and req->Size (offset 16+) for
    // any size > 0.  After the read, those req values are clobbered.
    ULONG64 savedPid  = req->ProcessId;
    ULONG64 savedAddr = req->Address;
    ULONG64 savedSize = req->Size;

    ReadMemoryResponse* resp = (ReadMemoryResponse*)irp->AssociatedIrp.SystemBuffer;

    SIZE_T bytesRead = 0;
    NTSTATUS status = ReadProcessMemory(
        savedPid,
        savedAddr,
        resp->Buffer,
        (SIZE_T)savedSize,
        &bytesRead
    );

    // Full or partial success: return whatever prefix we managed to read so
    // the caller can harvest readable pages and split around holes.  Total
    // zero reads (e.g. first page unmapped) still get the hard error.
    if (status == STATUS_SUCCESS || status == STATUS_PARTIAL_COPY) {
        resp->BytesRead = bytesRead;
        ULONG transferred = sizeof(ReadMemoryResponse) - 1 + (ULONG)bytesRead;
        COMPLETE_IRP(irp, STATUS_SUCCESS, transferred);
    }
    COMPLETE_IRP(irp, status, 0);
}

// ============================================================
//  IOCTL_BATCH_READ_PROCESS_MEMORY
//
//  One syscall, N reads.  Caller passes a header + array of
//  BatchReadDesc.  Driver services every descriptor inside one
//  KeStackAttachProcess scope and packs the results.  Per-descriptor
//  failures don't abort the batch.
// ============================================================
static NTSTATUS HandleBatchRead(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inLen < sizeof(BatchReadRequest))
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    BatchReadRequest* req = (BatchReadRequest*)irp->AssociatedIrp.SystemBuffer;
    ULONG64 savedPid   = req->ProcessId;
    ULONG   savedCount = req->Count;

    if (savedCount == 0 || savedCount > XENTYPE2_BATCH_MAX)
        COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);

    ULONG descBytes = savedCount * sizeof(BatchReadDesc);
    if (inLen < sizeof(BatchReadRequest) + descBytes)
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    // Snapshot descriptors out of SystemBuffer before we start writing the
    // response into the same buffer.  Stack-allocate — XENTYPE2_BATCH_MAX is
    // 256 so this is at most 4 KiB.
    BatchReadDesc descs[XENTYPE2_BATCH_MAX];
    RtlCopyMemory(descs,
                  (UCHAR*)irp->AssociatedIrp.SystemBuffer + sizeof(BatchReadRequest),
                  descBytes);

    // Plan the response layout: [header][result table][packed bytes...].
    ULONG headerBytes  = sizeof(BatchReadResponse);
    ULONG resultsBytes = savedCount * sizeof(BatchReadResult);
    ULONG bytesOffset  = headerBytes + resultsBytes;

    ULONG totalBytes = bytesOffset;
    for (ULONG i = 0; i < savedCount; ++i) {
        if (descs[i].Size > 0x100000) {        // 1 MiB per-descriptor cap
            COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);
        }
        totalBytes += descs[i].Size;
    }
    if (outLen < totalBytes)
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    // Same SystemBuffer for response.
    BatchReadResponse* resp    = (BatchReadResponse*)irp->AssociatedIrp.SystemBuffer;
    BatchReadResult*   results = (BatchReadResult*)((UCHAR*)resp + headerBytes);
    UCHAR*             dataBuf = (UCHAR*)resp + bytesOffset;

    // Zero the results table up front so partial completion has clean state.
    RtlZeroMemory(results, resultsBytes);

    // Fan out: ReadProcessMemory does its own KeStackAttachProcess, so each
    // call attaches+detaches.  For very hot paths a future tweak can hold
    // the attach across the loop, but the typical N=10..100 case is plenty
    // fast already and KeStackAttachProcess is microseconds-cheap.
    ULONG runningOff = bytesOffset;
    for (ULONG i = 0; i < savedCount; ++i) {
        results[i].OutOffset = runningOff;
        if (descs[i].Size == 0) {
            // Empty read — valid, just produces zero bytes.
            continue;
        }
        SIZE_T bytesRead = 0;
        NTSTATUS s = ReadProcessMemory(
            savedPid,
            descs[i].Address,
            dataBuf + (runningOff - bytesOffset),
            (SIZE_T)descs[i].Size,
            &bytesRead);
        if (s == STATUS_SUCCESS || s == STATUS_PARTIAL_COPY) {
            results[i].BytesRead = (ULONG)bytesRead;
            results[i].Status    = 0;
        } else {
            results[i].BytesRead = 0;
            results[i].Status    = (ULONG)s;
        }
        runningOff += descs[i].Size;
    }

    resp->Count = savedCount;
    resp->_pad  = 0;
    COMPLETE_IRP(irp, STATUS_SUCCESS, totalBytes);
}

// ============================================================
//  IOCTL_WRITE_PROCESS_MEMORY
// ============================================================
static NTSTATUS HandleWriteMemory(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inLen < sizeof(WriteMemoryRequest))
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    WriteMemoryRequest* req = (WriteMemoryRequest*)irp->AssociatedIrp.SystemBuffer;

    ULONG needed = (ULONG)(sizeof(WriteMemoryRequest) - 1 + req->Size);
    if (inLen < needed)
        COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    if (req->Size == 0 || req->Size > 0x10000000)
        COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);

    // Capture req values before issuing the write: the response writes into
    // the same SystemBuffer and would clobber req->ProcessId (and any later
    // handler code that read req->Size again would read garbage — same class
    // of bug as the Read IOCTL).
    ULONG64 savedPid  = req->ProcessId;
    ULONG64 savedAddr = req->Address;
    ULONG64 savedSize = req->Size;

    NTSTATUS status = WriteProcessMemory(savedPid, savedAddr, req->Buffer, (SIZE_T)savedSize);

    if (!NT_SUCCESS(status))
        COMPLETE_IRP(irp, status, 0);

    if (outLen >= sizeof(WriteMemoryResponse)) {
        WriteMemoryResponse* resp = (WriteMemoryResponse*)irp->AssociatedIrp.SystemBuffer;
        resp->BytesWritten = savedSize;
        COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(WriteMemoryResponse));
    }
    COMPLETE_IRP(irp, STATUS_SUCCESS, 0);
}

// ============================================================
//  IOCTL_GET_MODULE_BASE
// ============================================================
static NTSTATUS HandleGetModuleBase(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inLen  < sizeof(ModuleBaseRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(ModuleBaseResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    ModuleBaseRequest*  req  = (ModuleBaseRequest*)irp->AssociatedIrp.SystemBuffer;
    ModuleBaseResponse* resp = (ModuleBaseResponse*)irp->AssociatedIrp.SystemBuffer;

    // Copy out the module name before it gets overwritten by resp
    WCHAR moduleName[64] = {};
    RtlCopyMemory(moduleName, req->ModuleName, sizeof(moduleName));
    ULONG64 pid = req->ProcessId;

    ULONG64 base = 0, size = 0;
    NTSTATUS status = GetModuleBase(pid, moduleName, &base, &size);
    if (!NT_SUCCESS(status)) COMPLETE_IRP(irp, status, 0);

    resp->BaseAddress = base;
    resp->ModuleSize  = size;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(ModuleBaseResponse));
}

// ============================================================
//  IOCTL_GET_PROCESS_CR3
// ============================================================
static NTSTATUS HandleGetCr3(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (inLen  < sizeof(Cr3Request))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(Cr3Response)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    Cr3Request*  req  = (Cr3Request*)irp->AssociatedIrp.SystemBuffer;
    Cr3Response* resp = (Cr3Response*)irp->AssociatedIrp.SystemBuffer;

    ULONG64 pid = req->ProcessId;
    UINT64 cr3  = GetProcessCr3(pid);
    if (!cr3) COMPLETE_IRP(irp, STATUS_NOT_FOUND, 0);

    resp->Cr3Value = cr3;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(Cr3Response));
}

// ============================================================
//  IOCTL_GET_VMX_STATUS
// ============================================================
static NTSTATUS HandleVmxStatus(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (outLen < sizeof(VmxStatusResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    VmxStatusResponse* resp = (VmxStatusResponse*)irp->AssociatedIrp.SystemBuffer;
    resp->VmxInitialized    = g_Vmx && g_Vmx->Initialized;
    resp->LogicalCoreCount  = g_Vmx ? g_Vmx->CoreCount : 0;
    resp->VmxBasicMsr = __readmsr(0x480);  // IA32_VMX_BASIC
    resp->EptpValue         = g_Ept ? g_Ept->Eptp.Value : 0;
    resp->LastVmlaunchError = g_Vmx ? g_Vmx->LastVmlaunchError : 0;
    resp->LastFailedCore    = g_Vmx ? g_Vmx->LastFailedCore    : 0;
    extern volatile UINT64 g_VmExitCounter[];
    extern volatile UINT64 g_VmExitLastReason[];
    for (int i = 0; i < 8; i++) {
        resp->VmExitCount[i]      = g_VmExitCounter[i];
        resp->VmExitLastReason[i] = g_VmExitLastReason[i];
    }

    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(VmxStatusResponse));
}

// ============================================================
//  IOCTL_VMX_START — initialise EPT + VMX on all cores
// ============================================================
static NTSTATUS HandleVmxStart(PIRP irp, PIO_STACK_LOCATION /*stack*/) {
    if (g_Vmx && g_Vmx->Initialized)
        COMPLETE_IRP(irp, STATUS_SUCCESS, 0); // already running

    NTSTATUS status = EptInit();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[XenV2-Type2] EptInit failed: 0x%X\n", status);
        COMPLETE_IRP(irp, status, 0);
    }

    if (!VmxCheckSupport()) {
        DbgPrint("[XenV2-Type2] VMX not supported.\n");
        COMPLETE_IRP(irp, STATUS_NOT_SUPPORTED, 0);
    }

    status = VmxInit();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[XenV2-Type2] VmxInit failed: 0x%X\n", status);
        COMPLETE_IRP(irp, status, 0);
    }

    DbgPrint("[XenV2-Type2] VMX started on %u cores.\n", g_Vmx->CoreCount);
    COMPLETE_IRP(irp, STATUS_SUCCESS, 0);
}

// Called from the DriverEntry work item — runs ~1s after driver load,
// independent of VMX, so DSE bypass is available immediately after
// `sc start`.
extern "C" VOID AutoDisableDse()
{
    UINT64 ciOpts = FindGCiOptions();
    if (!ciOpts) {
        DbgPrint("[XenV2-Type2] Auto-DSE: FindGCiOptions returned 0\n");
        return;
    }
    ULONG32 original = 0;
    NTSTATUS rs = ReadKernel(ciOpts, &original, sizeof(original));
    if (!NT_SUCCESS(rs) || !(original & 0x2) || original >= 0x02000000) {
        DbgPrint("[XenV2-Type2] Auto-DSE: bad value 0x%x (rs=0x%x)\n", original, rs);
        return;
    }
    ULONG32 newVal = original & ~(ULONG32)0x6;
    NTSTATUS ws = WriteKernel(ciOpts, &newVal, sizeof(newVal));
    DbgPrint("[XenV2-Type2] Auto-DSE: g_CiOptions @ 0x%llx  0x%x -> 0x%x (st=0x%x)\n",
             ciOpts, original, newVal, ws);
}

// ============================================================
//  IOCTL_VMX_STOP — acknowledge but do NOT tear down VMX.
//
//  A real graceful-shutdown path needs to: issue vmcall from every core,
//  the host side then executes vmxoff, restores guest state to pre-vmlaunch
//  registers, and resumes execution as if VMX was never active.  We don't
//  have that — the old version called VmxShutdown which frees per-core
//  HostStack / VMCS regions while the CPU is still in VMX non-root mode,
//  so the next VM-exit jumps to freed memory and triple-faults.
//
//  For now: keep VMX alive until reboot.  xentype2's "quit" still exits
//  user-mode cleanly; only the IOCTL here is neutered.
// ============================================================
static NTSTATUS HandleVmxStop(PIRP irp, PIO_STACK_LOCATION /*stack*/) {
    DbgPrint("[XenV2-Type2] VMX_STOP requested — ignoring (reboot to stop).\n");
    COMPLETE_IRP(irp, STATUS_SUCCESS, 0);
}

// ============================================================
//  IOCTL_ALLOC_REMOTE_MEMORY / FREE_REMOTE_MEMORY / PROTECT_REMOTE_MEMORY
//  IOCTL_CREATE_REMOTE_THREAD
//
//  Let user mode do a complete manual-map of a DLL into any process
//  without ever calling NtOpenProcess.  All four syscalls that anti-cheats
//  hook (OpenProcess, VirtualAllocEx, WriteProcessMemory -> already covered
//  by IOCTL_WRITE_PROCESS_MEMORY, VirtualProtectEx, CreateRemoteThread)
//  get routed through the driver instead.
// ============================================================
static NTSTATUS HandleAllocRemote(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(AllocRemoteRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(AllocRemoteResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    AllocRemoteRequest*  req  = (AllocRemoteRequest*) irp->AssociatedIrp.SystemBuffer;
    AllocRemoteResponse* resp = (AllocRemoteResponse*)irp->AssociatedIrp.SystemBuffer;

    HANDLE pid   = (HANDLE)req->ProcessId;
    PVOID  addr  = (PVOID)req->PreferredAddress;
    SIZE_T size  = (SIZE_T)req->Size;
    ULONG  prot  = req->Protection;

    NTSTATUS st = RemoteAlloc(pid, &addr, &size, prot);
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);

    resp->Address = (ULONG64)addr;
    resp->Size    = size;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

static NTSTATUS HandleFreeRemote(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    if (inLen < sizeof(FreeRemoteRequest)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    FreeRemoteRequest* req = (FreeRemoteRequest*)irp->AssociatedIrp.SystemBuffer;
    NTSTATUS st = RemoteFree((HANDLE)req->ProcessId, (PVOID)req->Address, (SIZE_T)req->Size);
    COMPLETE_IRP(irp, st, 0);
}

static NTSTATUS HandleProtectRemote(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(ProtectRemoteRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(ProtectRemoteResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    ProtectRemoteRequest*  req  = (ProtectRemoteRequest*) irp->AssociatedIrp.SystemBuffer;
    ProtectRemoteResponse* resp = (ProtectRemoteResponse*)irp->AssociatedIrp.SystemBuffer;

    ULONG oldProt = 0;
    NTSTATUS st = RemoteProtect((HANDLE)req->ProcessId, (PVOID)req->Address,
                                (SIZE_T)req->Size, req->NewProtection, &oldProt);
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);
    resp->OldProtection = oldProt;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

static NTSTATUS HandleCreateThread(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(CreateThreadRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(CreateThreadResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    CreateThreadRequest*  req  = (CreateThreadRequest*) irp->AssociatedIrp.SystemBuffer;
    CreateThreadResponse* resp = (CreateThreadResponse*)irp->AssociatedIrp.SystemBuffer;

    HANDLE tid = nullptr;
    NTSTATUS st = RemoteCreateThread((HANDLE)req->ProcessId,
                                     (PVOID)req->StartAddress,
                                     (PVOID)req->Argument,
                                     req->CreateFlags,
                                     &tid);
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);
    resp->ThreadId = (ULONG64)tid;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

// ============================================================
//  Kernel memory r/w
// ============================================================
static NTSTATUS HandleKernelRead(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen < sizeof(KernelReadRequest)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* req = (KernelReadRequest*)irp->AssociatedIrp.SystemBuffer;
    ULONG64 va  = req->Address;
    ULONG64 sz  = req->Size;
    if (!va || !sz || sz > 0x10000) COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);

    ULONG needed = (ULONG)(FIELD_OFFSET(KernelReadResponse, Buffer) + sz);
    if (outLen < needed) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* resp = (KernelReadResponse*)irp->AssociatedIrp.SystemBuffer;
    NTSTATUS st = ReadKernel(va, resp->Buffer, (SIZE_T)sz);
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);
    resp->BytesRead = sz;
    COMPLETE_IRP(irp, STATUS_SUCCESS, needed);
}

static NTSTATUS HandleKernelWrite(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    if (inLen < sizeof(KernelWriteRequest)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    auto* req = (KernelWriteRequest*)irp->AssociatedIrp.SystemBuffer;
    if (!req->Address || !req->Size || req->Size > 0x10000)
        COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);
    ULONG needed = (ULONG)(FIELD_OFFSET(KernelWriteRequest, Buffer) + req->Size);
    if (inLen < needed) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    ULONG64 va = req->Address;
    ULONG64 sz = req->Size;
    NTSTATUS st = WriteKernel(va, req->Buffer, (SIZE_T)sz);
    COMPLETE_IRP(irp, st, 0);
}

// ============================================================
//  Guest-kernel discovery
// ============================================================
static NTSTATUS HandleDiscoverModule(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(DiscoverKModuleRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(DiscoverKModuleResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* req  = (DiscoverKModuleRequest*) irp->AssociatedIrp.SystemBuffer;
    WCHAR name[64]; RtlCopyMemory(name, req->BaseName, sizeof(name));
    name[63] = 0;

    UINT32 size = 0;
    UINT64 base = DiscoverModuleBase(name, &size);

    auto* resp = (DiscoverKModuleResponse*)irp->AssociatedIrp.SystemBuffer;
    resp->Base = base;
    resp->Size = size;
    COMPLETE_IRP(irp, base ? STATUS_SUCCESS : STATUS_NOT_FOUND, sizeof(*resp));
}

static NTSTATUS HandleDiscoverExport(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(DiscoverExportRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(DiscoverExportResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* req = (DiscoverExportRequest*)irp->AssociatedIrp.SystemBuffer;
    WCHAR mod[64]; RtlCopyMemory(mod, req->ModuleBaseName, sizeof(mod)); mod[63] = 0;
    CHAR  sym[128]; RtlCopyMemory(sym, req->SymbolName, sizeof(sym));    sym[127] = 0;

    UINT64 va = DiscoverExport(mod, sym);
    auto* resp = (DiscoverExportResponse*)irp->AssociatedIrp.SystemBuffer;
    resp->Address = va;
    COMPLETE_IRP(irp, va ? STATUS_SUCCESS : STATUS_NOT_FOUND, sizeof(*resp));
}

static NTSTATUS HandleDiscoverSig(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(DiscoverSigRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(DiscoverSigResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* req = (DiscoverSigRequest*)irp->AssociatedIrp.SystemBuffer;
    if (req->PatternLen == 0 || req->PatternLen >= sizeof(req->Pattern))
        COMPLETE_IRP(irp, STATUS_INVALID_PARAMETER, 0);
    WCHAR mod[64]; RtlCopyMemory(mod, req->ModuleBaseName, sizeof(mod)); mod[63] = 0;
    UCHAR pat[64]; RtlCopyMemory(pat, req->Pattern, sizeof(pat));
    CHAR  mask[64]; RtlCopyMemory(mask, req->Mask, sizeof(mask)); mask[63] = 0;

    UINT64 va = DiscoverSig(mod, pat, mask);
    auto* resp = (DiscoverSigResponse*)irp->AssociatedIrp.SystemBuffer;
    resp->Address = va;
    COMPLETE_IRP(irp, va ? STATUS_SUCCESS : STATUS_NOT_FOUND, sizeof(*resp));
}

// ============================================================
//  DSE bypass: locate g_CiOptions and zero it.
//  Scan ci.dll .text for any RIP-relative instruction (mov/test) whose
//  target falls in ci's writable data section AND whose current value
//  matches a plausible g_CiOptions bitmask (bit 1 "CI enabled" set,
//  value < 0x02000000).  First match wins.  Refuses to return any
//  candidate that fails the value check — safer to fail than to zero
//  the wrong pointer and BSOD.
// ============================================================
#include <ntimage.h>
static UINT64 FindGCiOptions()
{
    UINT32 ciSize = 0;
    UINT64 ciBase = DiscoverModuleBase(L"ci", &ciSize);
    if (!ciBase || !ciSize) return 0;

    __try {
        auto* dos = (IMAGE_DOS_HEADER*)ciBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = (IMAGE_NT_HEADERS64*)(ciBase + dos->e_lfanew);
        auto* sec = IMAGE_FIRST_SECTION(nt);

        UCHAR* textBase = nullptr; SIZE_T textSize = 0;
        UINT64 dataStart = 0, dataEnd = 0;
        for (ULONG i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if (!textBase && memcmp(sec[i].Name, ".text", 5) == 0) {
                textBase = (UCHAR*)ciBase + sec[i].VirtualAddress;
                textSize = sec[i].Misc.VirtualSize;
            }
            if ((sec[i].Characteristics & IMAGE_SCN_MEM_WRITE) &&
                !(sec[i].Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) {
                UINT64 vs = ciBase + sec[i].VirtualAddress;
                UINT64 ve = vs + sec[i].Misc.VirtualSize;
                if (dataStart == 0 || vs < dataStart) dataStart = vs;
                if (ve > dataEnd) dataEnd = ve;
            }
        }
        if (!textBase || !dataStart) return 0;

        // Histogram of reference counts.  g_CiOptions is touched by MANY
        // CI functions (dozens of references); random ULONGs that pass the
        // value gate usually have 1-2.  Pick the most-referenced slot.
        struct Hit { UINT64 addr; ULONG count; };
        const ULONG MAX_HITS = 512;
        Hit hits[MAX_HITS] = {};
        ULONG hitCount = 0;

        // RIP-relative ModR/M bytes: mod=00, rm=101, reg=0..7 => 0x05,0x0D,
        // 0x15,0x1D,0x25,0x2D,0x35,0x3D.  An instruction like
        // `mov ecx, [rip+disp]` has modrm=0x0D (reg=1 = ecx).
        auto isRipModRm = [](UCHAR m) -> bool {
            return (m & 0xC7) == 0x05;  // mod=00, rm=101, any reg
        };
        for (SIZE_T i = 0; i + 10 <= textSize; i++) {
            UCHAR b0 = textBase[i], b1 = textBase[i + 1];
            ULONG instrLen = 0, dispOff = 0;
            // mov reg32, [rip+disp] / mov [rip+disp], reg32 / mov reg8, [rip+disp] / mov [rip+disp], reg8
            if ((b0 == 0x8B || b0 == 0x89 || b0 == 0x8A || b0 == 0x88) && isRipModRm(b1)) {
                instrLen = 6; dispOff = 2;
            }
            // test [rip+disp], imm8 / test [rip+disp], imm32  (modrm must be 0x05: reg=0)
            else if (b0 == 0xF6 && b1 == 0x05)  { instrLen = 7;  dispOff = 2; }
            else if (b0 == 0xF7 && b1 == 0x05)  { instrLen = 10; dispOff = 2; }
            // cmp [rip+disp], imm8 (modrm must be 0x3D: reg=7)
            else if (b0 == 0x83 && b1 == 0x3D)  { instrLen = 7;  dispOff = 2; }
            // cmp [rip+disp], imm32
            else if (b0 == 0x81 && b1 == 0x3D)  { instrLen = 10; dispOff = 2; }
            // or [rip+disp], imm8 (modrm 0x0D: reg=1)
            else if (b0 == 0x83 && b1 == 0x0D)  { instrLen = 7;  dispOff = 2; }
            // and [rip+disp], imm8 (modrm 0x25: reg=4)
            else if (b0 == 0x83 && b1 == 0x25)  { instrLen = 7;  dispOff = 2; }
            else continue;

            LONG disp = *(LONG*)(textBase + i + dispOff);
            UINT64 target = (UINT64)(textBase + i + instrLen) + disp;
            if (target < dataStart || target + 4 > dataEnd) continue;
            if (target & 3) continue;  // must be 4-byte aligned

            UINT32 val = *(volatile UINT32*)target;
            if (!(val & 0x2) || val >= 0x02000000) continue;

            BOOLEAN found = FALSE;
            for (ULONG h = 0; h < hitCount; h++) {
                if (hits[h].addr == target) { hits[h].count++; found = TRUE; break; }
            }
            if (!found && hitCount < MAX_HITS) {
                hits[hitCount].addr  = target;
                hits[hitCount].count = 1;
                hitCount++;
            }
        }

        // Dump top 5 candidates (consumes counts; compute `best`/`bestAddr`
        // from rank 0).
        ULONG best = 0; UINT64 bestAddr = 0;
        for (ULONG rank = 0; rank < 5; rank++) {
            ULONG maxIdx = (ULONG)-1; ULONG maxVal = 0;
            for (ULONG h = 0; h < hitCount; h++) {
                if (hits[h].count > maxVal) { maxVal = hits[h].count; maxIdx = h; }
            }
            if (maxIdx == (ULONG)-1) break;
            UINT32 v = *(volatile UINT32*)hits[maxIdx].addr;
            DbgPrint("[DSE-scan] rank %u: addr=0x%llx refs=%u value=0x%x\n",
                     rank, hits[maxIdx].addr, hits[maxIdx].count, v);
            if (rank == 0) { best = hits[maxIdx].count; bestAddr = hits[maxIdx].addr; }
            hits[maxIdx].count = 0;
        }
        DbgPrint("[DSE-scan] total=%u candidates\n", hitCount);
        if (best < 3) return 0;
        return bestAddr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

static NTSTATUS HandleDseDisable(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(DseDisableRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(DseDisableResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);

    auto* req = (DseDisableRequest*)irp->AssociatedIrp.SystemBuffer;
    UINT64 va = req->ExplicitAddr ? req->ExplicitAddr : FindGCiOptions();
    if (!va) COMPLETE_IRP(irp, STATUS_NOT_FOUND, 0);

    ULONG32 original = 0;
    NTSTATUS st = ReadKernel(va, &original, sizeof(original));
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);

    // Sanity gate: refuse to write if value doesn't look like CI options.
    // Real g_CiOptions: bit 1 set (CI enabled), < 0x02000000.  An explicit
    // address with a clearly wrong value means user passed the wrong VA.
    if (!(original & 0x2) || original >= 0x02000000) {
        DbgPrint("[DSE] REFUSED: 0x%llx value 0x%x doesn't look like g_CiOptions\n",
                 va, original);
        COMPLETE_IRP(irp, STATUS_INVALID_ADDRESS, 0);
    }

    // DON'T write 0 — on Win10 26100, zeroing g_CiOptions bugchecks
    // (SYSTEM_SERVICE_EXCEPTION 0xC0000096).  Instead clear only the
    // enforcement bits (0x2 = CI enabled, 0x4 = require signature) and
    // leave debug mode, HVCI flag, and other upper bits intact.  DSE is
    // off; CI's internal invariants don't see an impossible state.
    ULONG32 newVal = original & ~(ULONG32)0x6;
    st = WriteKernel(va, &newVal, sizeof(newVal));
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);

    auto* resp = (DseDisableResponse*)irp->AssociatedIrp.SystemBuffer;
    resp->GCiOptionsVa  = va;
    resp->OriginalValue = original;
    DbgPrint("[DSE] g_CiOptions @ 0x%llx  0x%x -> 0x%x\n", va, original, newVal);
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

// ============================================================
//  PatchGuard neutralisation (best-effort; VMX must be running)
//  Installs an EPT split-view hook on nt!KeBugCheckEx with one-byte
//  `ret` so PG DPCs that try to bugcheck return harmlessly.
// ============================================================
static NTSTATUS HandlePgNeuter(PIRP irp, PIO_STACK_LOCATION /*stack*/) {
    if (!g_Vmx || !g_Vmx->Initialized) {
        DbgPrint("[PG] PG neuter requires VMX running.\n");
        COMPLETE_IRP(irp, STATUS_DEVICE_NOT_READY, 0);
    }
    UINT64 kbce = DiscoverExport(L"nt", "KeBugCheckEx");
    if (!kbce) {
        DbgPrint("[PG] KeBugCheckEx not resolved.\n");
        COMPLETE_IRP(irp, STATUS_NOT_FOUND, 0);
    }
    UCHAR retByte = 0xC3;
    EptHook* h = EptHookInstall(kbce, &retByte, 1);
    if (!h) {
        DbgPrint("[PG] EptHookInstall failed.\n");
        COMPLETE_IRP(irp, STATUS_UNSUCCESSFUL, 0);
    }
    DbgPrint("[PG] nt!KeBugCheckEx @ 0x%llx hooked (ret).\n", kbce);
    COMPLETE_IRP(irp, STATUS_SUCCESS, 0);
}

// ============================================================
//  IOCTL_ACTIVATE — usermode triggers deferred stealth measures.
//  Call AFTER the protected game's anti-cheat has finished initialising.
// ============================================================
static NTSTATUS HandleActivate(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (outLen < sizeof(ActivateResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    auto* resp = (ActivateResponse*)irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(resp, sizeof(*resp));

    PDRIVER_OBJECT drv = g_DriverObject;
    if (drv) {
        // Manual-map DriverObject has DriverSection == NULL.  HideDriver
        // early-returns gracefully on NULL DriverSection.
        resp->Flags |= 0x1;
        resp->HideScrubs = HideDriver(drv);
        DbgPrint("[XenV2-Type2] Activate: self-hide scrubs=%u\n", resp->HideScrubs);
        drv->DriverUnload = nullptr;

        // Only install RegFilter when we have a persistent DriverObject
        // (i.e. SCM-loaded, where DriverSection != NULL).  A manual-map
        // DriverObject is synthesised in pool but holds no ref counting
        // state; CmRegisterCallbackEx tracking it would bugcheck later.
        if (drv->DriverSection) {
            resp->Flags |= 0x2;
            NTSTATUS rs = RegFilterInstall(drv);
            resp->RegFilterStatus = (ULONG32)rs;
            DbgPrint("[XenV2-Type2] Activate: RegFilterInstall=0x%x\n", rs);
        }
    } else {
        DbgPrint("[XenV2-Type2] Activate: g_DriverObject is NULL, nothing to do.\n");
    }

    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

// ============================================================
//  IOCTL_EPT_TRACE_REGISTER / UNREGISTER / QUERY / DUMP
//
//  Register an encrypted global for read-tracing.  After registration, any
//  guest read of the page triggers EPT violation → MTF single-step → the
//  driver records the decryption op sequence (bswap/add/xor/rol with
//  immediates).  Query applies the learned sequence to a raw value.
// ============================================================
static NTSTATUS HandleEptTraceRegister(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(EptTraceRegisterRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(EptTraceRegisterResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (!g_Vmx || !g_Vmx->Initialized) COMPLETE_IRP(irp, STATUS_DEVICE_NOT_READY, 0);

    auto* req  = (EptTraceRegisterRequest*)irp->AssociatedIrp.SystemBuffer;
    auto* resp = (EptTraceRegisterResponse*)irp->AssociatedIrp.SystemBuffer;
    UINT32 pid = (UINT32)req->ProcessId;
    UINT64 va  = req->GuestVa;
    UINT32 len = req->Length;
    UINT32 tag = req->Tag;
    UINT32 dep = req->TraceDepth;
    UINT64 gpa = 0;

    NTSTATUS st = EptTraceRegister(pid, va, len, tag, dep, &gpa);
    RtlZeroMemory(resp, sizeof(*resp));
    resp->GuestPhys = gpa;
    resp->Status    = (UINT32)st;
    COMPLETE_IRP(irp, st, sizeof(*resp));
}

static NTSTATUS HandleEptTraceUnregister(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    if (inLen < sizeof(EptTraceUnregisterRequest)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    auto* req = (EptTraceUnregisterRequest*)irp->AssociatedIrp.SystemBuffer;
    NTSTATUS st = EptTraceUnregister(req->Tag);
    COMPLETE_IRP(irp, st, 0);
}

static NTSTATUS HandleEptTraceQuery(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(EptTraceQueryRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(EptTraceQueryResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    auto* req  = (EptTraceQueryRequest*)irp->AssociatedIrp.SystemBuffer;
    auto* resp = (EptTraceQueryResponse*)irp->AssociatedIrp.SystemBuffer;

    UINT32 opCount = 0, hitCount = 0;
    UINT32 tag = req->Tag;
    UINT64 raw = req->RawValue;
    UINT64 plain = EptTraceDecrypt(tag, raw, &opCount, &hitCount);
    RtlZeroMemory(resp, sizeof(*resp));
    resp->PlaintextValue = plain;
    resp->OpCount        = opCount;
    resp->HitCount       = hitCount;
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

static NTSTATUS HandleEptTraceDump(PIRP irp, PIO_STACK_LOCATION stack) {
    ULONG inLen  = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    if (inLen  < sizeof(EptTraceDumpRequest))  COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    if (outLen < sizeof(EptTraceDumpResponse)) COMPLETE_IRP(irp, STATUS_BUFFER_TOO_SMALL, 0);
    auto* req  = (EptTraceDumpRequest*)irp->AssociatedIrp.SystemBuffer;
    auto* resp = (EptTraceDumpResponse*)irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(resp, sizeof(*resp));

    EptTraceOp ops[EPT_TRACE_MAX_OPS] = { 0 };
    UINT32 opCount = 0, hitCount = 0;
    NTSTATUS st = EptTraceDump(req->Tag, ops, EPT_TRACE_MAX_OPS, &opCount, &hitCount);
    if (!NT_SUCCESS(st)) COMPLETE_IRP(irp, st, 0);

    resp->OpCount  = opCount;
    resp->HitCount = hitCount;
    UINT32 outOps = (opCount < 32) ? opCount : 32;
    for (UINT32 i = 0; i < outOps; ++i) {
        resp->Ops[i].Type  = ops[i].Type;
        resp->Ops[i].Const = ops[i].Const;
    }
    COMPLETE_IRP(irp, STATUS_SUCCESS, sizeof(*resp));
}

// ============================================================
//  Top-level IRP_MJ_DEVICE_CONTROL dispatcher
//
//  Wrapped in SEH because anti-cheat drivers probe arbitrary device
//  objects with malformed IRPs / garbage IOCTL codes / NULL system
//  buffers as part of their detection sweep.  Without SEH a single
//  bad probe deref-faults inside one of the Handle* helpers and
//  bugchecks the system (the manual-mapped image's .pdata is only
//  partially registered, so unhandled #GPs in our pool become 0x1AA
//  EXCEPTION_ON_INVALID_STACK).  With SEH we return STATUS_UNSUCCESSFUL
//  and let the caller move on.
// ============================================================
static NTSTATUS IoctlDispatchInner(PIRP Irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;

    switch (code) {
    case IOCTL_READ_PROCESS_MEMORY:        return HandleReadMemory(Irp, stack);
    case IOCTL_BATCH_READ_PROCESS_MEMORY:  return HandleBatchRead(Irp, stack);
    case IOCTL_WRITE_PROCESS_MEMORY:       return HandleWriteMemory(Irp, stack);
    case IOCTL_GET_MODULE_BASE:      return HandleGetModuleBase(Irp, stack);
    case IOCTL_GET_PROCESS_CR3:      return HandleGetCr3(Irp, stack);
    case IOCTL_GET_VMX_STATUS:       return HandleVmxStatus(Irp, stack);
    case IOCTL_VMX_START:            return HandleVmxStart(Irp, stack);
    case IOCTL_VMX_STOP:             return HandleVmxStop(Irp, stack);
    case IOCTL_ALLOC_REMOTE_MEMORY:  return HandleAllocRemote(Irp, stack);
    case IOCTL_FREE_REMOTE_MEMORY:   return HandleFreeRemote(Irp, stack);
    case IOCTL_PROTECT_REMOTE_MEMORY:return HandleProtectRemote(Irp, stack);
    case IOCTL_CREATE_REMOTE_THREAD: return HandleCreateThread(Irp, stack);
    case IOCTL_KERNEL_READ:          return HandleKernelRead(Irp, stack);
    case IOCTL_KERNEL_WRITE:         return HandleKernelWrite(Irp, stack);
    case IOCTL_DISCOVER_KMODULE:     return HandleDiscoverModule(Irp, stack);
    case IOCTL_DISCOVER_EXPORT:      return HandleDiscoverExport(Irp, stack);
    case IOCTL_DISCOVER_SIG:         return HandleDiscoverSig(Irp, stack);
    case IOCTL_DSE_DISABLE:          return HandleDseDisable(Irp, stack);
    case IOCTL_PG_NEUTER:            return HandlePgNeuter(Irp, stack);
    case IOCTL_ACTIVATE:             return HandleActivate(Irp, stack);
    case IOCTL_EPT_TRACE_REGISTER:   return HandleEptTraceRegister(Irp, stack);
    case IOCTL_EPT_TRACE_UNREGISTER: return HandleEptTraceUnregister(Irp, stack);
    case IOCTL_EPT_TRACE_QUERY:      return HandleEptTraceQuery(Irp, stack);
    case IOCTL_EPT_TRACE_DUMP:       return HandleEptTraceDump(Irp, stack);
    default:
        Irp->IoStatus.Status      = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS IoctlDispatch(PDEVICE_OBJECT /*DeviceObject*/, PIRP Irp) {
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    __try {
        status = IoctlDispatchInner(Irp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Garbage probe (typical anti-cheat enumeration) — complete the
        // IRP cleanly with a generic error and don't bubble the fault up.
        // Avoid DbgPrint here: the exception happened inside our dispatch
        // path, which means our SEH unwind just walked through partially-
        // registered .pdata; further calls into our code from this stack
        // are best avoided.
        if (Irp) {
            Irp->IoStatus.Status      = STATUS_UNSUCCESSFUL;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
        status = STATUS_UNSUCCESSFUL;
    }
    return status;
}
