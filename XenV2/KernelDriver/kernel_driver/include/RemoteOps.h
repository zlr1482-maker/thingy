#pragma once
//
// RemoteOps.h - kernel-side remote memory + thread primitives
//
// Each op attaches to the target EPROCESS with KeStackAttachProcess so the
// allocation/protection/thread creation happens in that process's VA space.
// User mode never holds a handle to the target.
//

#include <ntifs.h>

NTSTATUS RemoteAlloc(HANDLE Pid,
                     PVOID* InOutAddress,
                     SIZE_T* InOutSize,
                     ULONG Protect);

NTSTATUS RemoteFree(HANDLE Pid,
                    PVOID Address,
                    SIZE_T Size);

NTSTATUS RemoteProtect(HANDLE Pid,
                       PVOID Address,
                       SIZE_T Size,
                       ULONG NewProtect,
                       ULONG* OldProtect);

NTSTATUS RemoteCreateThread(HANDLE Pid,
                            PVOID StartAddress,
                            PVOID Argument,
                            ULONG CreateFlags,
                            HANDLE* OutTid);
