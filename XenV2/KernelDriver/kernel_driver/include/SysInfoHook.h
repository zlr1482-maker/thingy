#pragma once
#include <ntifs.h>

//
// SysInfoHook — inline detour on nt!NtQuerySystemInformation to scrub
// VM-detection data from two info classes:
//
//   SystemModuleInformation          (class 0x0B) — drivers list.
//     Filters out vmci.sys / vmhgfs.sys / vmmouse.sys / vm3dmp.sys etc.
//
//   SystemFirmwareTableInformation   (class 0x4C) — SMBIOS / DMI tables.
//     Replaces "VMware"-prefixed strings in the SMBIOS blob with
//     neutral values ("Intel Corporation" / "Desktop" / "0").
//
// The hook is a 12-byte prologue trampoline using `movabs rax, <hook>; jmp rax`.
// Original bytes are copied into a trampoline buffer that calls back into
// the real function at (target + 12).  No relocation of RIP-relative
// instructions — if the first 12 bytes contain any, install fails and we
// refuse to hook (logged, never crash the box).
//

// Install the hook.  Returns STATUS_SUCCESS on install, STATUS_NOT_SUPPORTED
// if the function layout isn't compatible.  Requires kernel memory R/W
// (MmMapIoSpace path) so must be called after Auto-DSE stabilises.
NTSTATUS SysInfoHookInstall();

// Remove the hook (restores original prologue).  Safe to call before reboot.
VOID SysInfoHookRemove();
