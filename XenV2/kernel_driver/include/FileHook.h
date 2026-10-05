#pragma once
#include <ntifs.h>

// Inline hook on nt!NtCreateFile that returns STATUS_OBJECT_NAME_NOT_FOUND
// for paths mentioning VMware / VirtualBox when the caller is not a
// whitelisted VMware Tools process.  Uses the same 12-byte trampoline
// technique as SysInfoHook.
NTSTATUS FileHookInstall();
VOID     FileHookRemove();
