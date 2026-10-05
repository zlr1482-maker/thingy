/*
 * RemoteOps.cpp - kernel-side remote memory + thread primitives.
 *
 * These ops replace OpenProcess / VirtualAllocEx / WriteProcessMemory /
 * VirtualProtectEx / CreateRemoteThread on the user side, so the attacker
 * process never needs a handle to the target.  All four of those syscalls
 * are the usual targets of ObRegisterCallbacks / inline hooks in anti-cheats.
 */

#include "..\include\RemoteOps.h"
#include <ntimage.h>

// RtlPcToFileHeader isn't in ntifs.h on all WDKs; forward-declare it.
extern "C" NTKERNELAPI PVOID NTAPI RtlPcToFileHeader(
    PVOID  PcValue,
    PVOID* BaseOfImage);

// ZwProtectVirtualMemory isn't in ntifs.h; declare it here so the call in
// RemoteProtect resolves.
extern "C" NTKERNELAPI NTSTATUS ZwProtectVirtualMemory(
    HANDLE  ProcessHandle,
    PVOID*  BaseAddress,
    PSIZE_T RegionSize,
    ULONG   NewProtect,
    PULONG  OldProtect);

#ifndef THREAD_QUERY_INFORMATION
#define THREAD_QUERY_INFORMATION 0x0040
#endif

// ZwCreateThreadEx is not in the WDK headers; declare + resolve at runtime.
typedef NTSTATUS (NTAPI* PFN_ZwCreateThreadEx)(
    PHANDLE           ThreadHandle,
    ACCESS_MASK       DesiredAccess,
    PVOID             ObjectAttributes,    // POBJECT_ATTRIBUTES (OPTIONAL)
    HANDLE            ProcessHandle,
    PVOID             StartAddress,
    PVOID             Argument,
    ULONG             CreateFlags,
    SIZE_T            ZeroBits,
    SIZE_T            StackSize,
    SIZE_T            MaximumStackSize,
    PVOID             AttributeList);      // PPS_ATTRIBUTE_LIST (OPTIONAL)

static PFN_ZwCreateThreadEx g_ZwCreateThreadEx = nullptr;

// MmGetSystemRoutineAddress uses a whitelist that Microsoft has been trimming
// on recent Win11 builds — even architectural routines like NtCreateThreadEx
// aren't reachable through it anymore.  Fall back to parsing ntoskrnl's
// export table directly.  We locate ntoskrnl via RtlPcToFileHeader(&KeBugCheckEx)
// (KeBugCheckEx is a WDK-exported symbol always resident in ntoskrnl).
static PVOID FindNtosExport(const char* name) {
    PVOID base = nullptr;
    if (!RtlPcToFileHeader((PVOID)KeBugCheckEx, &base) || !base)
        return nullptr;

    auto* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = (IMAGE_NT_HEADERS64*)((UCHAR*)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return nullptr;

    auto* exp   = (IMAGE_EXPORT_DIRECTORY*)((UCHAR*)base + dir.VirtualAddress);
    auto* names = (ULONG*)((UCHAR*)base + exp->AddressOfNames);
    auto* ords  = (USHORT*)((UCHAR*)base + exp->AddressOfNameOrdinals);
    auto* funcs = (ULONG*)((UCHAR*)base + exp->AddressOfFunctions);

    for (ULONG i = 0; i < exp->NumberOfNames; i++) {
        const char* n = (const char*)base + names[i];
        // Inline strcmp
        int j = 0;
        while (name[j] && name[j] == n[j]) j++;
        if (name[j] == 0 && n[j] == 0)
            return (UCHAR*)base + funcs[ords[i]];
    }
    return nullptr;
}

static NTSTATUS ResolveZwCreateThreadEx() {
    if (g_ZwCreateThreadEx) return STATUS_SUCCESS;
    static const char* kCandidates[] = { "ZwCreateThreadEx", "NtCreateThreadEx" };
    for (int i = 0; i < 2; i++) {
        PVOID p = FindNtosExport(kCandidates[i]);
        if (p) {
            g_ZwCreateThreadEx = (PFN_ZwCreateThreadEx)p;
            DbgPrint("[RemoteOps] %s resolved via export table -> %p\n",
                     kCandidates[i], p);
            return STATUS_SUCCESS;
        }
    }
    DbgPrint("[RemoteOps] Neither ZwCreateThreadEx nor NtCreateThreadEx found in ntoskrnl exports.\n");
    return STATUS_NOT_FOUND;
}

// ============================================================
//  Attach-to-PID helper.  Returns EPROCESS the caller must deref.
// ============================================================
static NTSTATUS AttachByPid(HANDLE Pid, PEPROCESS* OutProc, KAPC_STATE* Apc) {
    PEPROCESS proc = nullptr;
    NTSTATUS st = PsLookupProcessByProcessId(Pid, &proc);
    if (!NT_SUCCESS(st)) return st;
    KeStackAttachProcess(proc, Apc);
    *OutProc = proc;
    return STATUS_SUCCESS;
}

static VOID DetachAndDeref(PEPROCESS Proc, KAPC_STATE* Apc) {
    KeUnstackDetachProcess(Apc);
    ObDereferenceObject(Proc);
}

// ============================================================
//  RemoteAlloc - ZwAllocateVirtualMemory in target
// ============================================================
NTSTATUS RemoteAlloc(HANDLE Pid, PVOID* InOutAddress, SIZE_T* InOutSize, ULONG Protect) {
    PEPROCESS proc; KAPC_STATE apc;
    NTSTATUS st = AttachByPid(Pid, &proc, &apc);
    if (!NT_SUCCESS(st)) return st;

    st = ZwAllocateVirtualMemory(ZwCurrentProcess(),
                                 InOutAddress,
                                 0,
                                 InOutSize,
                                 MEM_COMMIT | MEM_RESERVE,
                                 Protect);

    // ZwAllocateVirtualMemory returns demand-zero pages — committed but not
    // backed by physical frames until first touch.  The xentype2 driver
    // writes target memory via CR3 page-walk (no MmCopyVirtualMemory), which
    // requires a valid PTE or the translation returns 0 -> STATUS_ACCESS_VIOLATION.
    // Force-fault every allocated page by writing a zero byte into it while
    // we're still attached.  Safe because the region is at least PAGE_READWRITE
    // (our page-protection semantics: exec-only regions aren't a thing we
    // request from user mode).
    if (NT_SUCCESS(st) && InOutAddress && *InOutAddress && InOutSize && *InOutSize) {
        __try {
            volatile UINT8* p    = (volatile UINT8*)*InOutAddress;
            SIZE_T          sz   = *InOutSize;
            for (SIZE_T off = 0; off < sz; off += PAGE_SIZE) p[off] = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // If the caller requested a protection that isn't writable
            // (PAGE_EXECUTE / PAGE_READONLY), the fault-in write will
            // #GP — that's fine, skip.  Caller can IOCTL_PROTECT_REMOTE
            // later to get the final protection it wants.
        }
    }

    DetachAndDeref(proc, &apc);
    return st;
}

// ============================================================
//  RemoteFree - ZwFreeVirtualMemory in target.  Size==0 => MEM_RELEASE.
// ============================================================
NTSTATUS RemoteFree(HANDLE Pid, PVOID Address, SIZE_T Size) {
    PEPROCESS proc; KAPC_STATE apc;
    NTSTATUS st = AttachByPid(Pid, &proc, &apc);
    if (!NT_SUCCESS(st)) return st;

    PVOID addr = Address;
    SIZE_T size = Size;
    st = ZwFreeVirtualMemory(ZwCurrentProcess(),
                             &addr,
                             &size,
                             Size == 0 ? MEM_RELEASE : MEM_DECOMMIT);
    DetachAndDeref(proc, &apc);
    return st;
}

// ============================================================
//  RemoteProtect - ZwProtectVirtualMemory in target
// ============================================================
NTSTATUS RemoteProtect(HANDLE Pid, PVOID Address, SIZE_T Size,
                       ULONG NewProtect, ULONG* OldProtect) {
    PEPROCESS proc; KAPC_STATE apc;
    NTSTATUS st = AttachByPid(Pid, &proc, &apc);
    if (!NT_SUCCESS(st)) return st;

    PVOID addr = Address;
    SIZE_T size = Size;
    ULONG oldProt = 0;
    st = ZwProtectVirtualMemory(ZwCurrentProcess(),
                                &addr,
                                &size,
                                NewProtect,
                                &oldProt);
    DetachAndDeref(proc, &apc);
    if (NT_SUCCESS(st) && OldProtect) *OldProtect = oldProt;
    return st;
}

// ============================================================
//  RemoteCreateThread - ZwCreateThreadEx in target.
//
//  We open a kernel handle to the EPROCESS via ObOpenObjectByPointer
//  rather than going through user-mode OpenProcess.  The thread's
//  start address points at our pre-written bootstrap shellcode
//  in the target's VA; Argument is the mapped module base.
// ============================================================
NTSTATUS RemoteCreateThread(HANDLE Pid, PVOID StartAddress, PVOID Argument,
                            ULONG CreateFlags, HANDLE* OutTid) {
    NTSTATUS st = ResolveZwCreateThreadEx();
    if (!NT_SUCCESS(st)) return st;

    PEPROCESS proc = nullptr;
    st = PsLookupProcessByProcessId(Pid, &proc);
    if (!NT_SUCCESS(st)) return st;

    HANDLE procHandle = nullptr;
    st = ObOpenObjectByPointer(proc,
                               OBJ_KERNEL_HANDLE,
                               nullptr,
                               PROCESS_ALL_ACCESS,
                               *PsProcessType,
                               KernelMode,
                               &procHandle);
    if (!NT_SUCCESS(st)) {
        ObDereferenceObject(proc);
        return st;
    }

    HANDLE threadHandle = nullptr;
    st = g_ZwCreateThreadEx(&threadHandle,
                            THREAD_ALL_ACCESS,
                            nullptr,
                            procHandle,
                            StartAddress,
                            Argument,
                            CreateFlags,
                            0, 0, 0, nullptr);

    if (NT_SUCCESS(st) && threadHandle) {
        if (OutTid) {
            // Resolve handle -> TID
            PETHREAD thr = nullptr;
            if (NT_SUCCESS(ObReferenceObjectByHandle(threadHandle,
                                                     THREAD_QUERY_INFORMATION,
                                                     nullptr,
                                                     KernelMode,
                                                     (PVOID*)&thr,
                                                     nullptr))) {
                *OutTid = PsGetThreadId(thr);
                ObDereferenceObject(thr);
            }
        }
        ZwClose(threadHandle);
    }

    ZwClose(procHandle);
    ObDereferenceObject(proc);
    return st;
}
