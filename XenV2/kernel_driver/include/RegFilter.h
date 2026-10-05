#pragma once
#include <ntifs.h>

// Register a CmRegisterCallbackEx filter that returns STATUS_OBJECT_NAME_NOT_FOUND
// for any operation on a key path containing "VMware" / "VirtualBox" / "VBox",
// unless the caller is a whitelisted VMware Tools process.
NTSTATUS RegFilterInstall(PDRIVER_OBJECT DriverObject);

// Unregister the callback.  Safe to call at any IRQL <= APC_LEVEL.
VOID     RegFilterRemove();
