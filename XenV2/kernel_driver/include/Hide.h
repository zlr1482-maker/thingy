#pragma once
//
// Hide.h - driver self-concealment (unlink from lists that naive anti-cheat
// scans rely on).  Called once from DriverEntry after the rest of init.
//

#include <ntifs.h>

// Unlink ourselves from:
//   - PsLoadedModuleList (EnumDeviceDrivers / ZwQuerySystemInformation(SystemModuleInformation))
//   - PiDDBCacheTable    (kernel's "have I seen this driver hash?" cache)
//   - MmUnloadedDrivers  (history of recently unloaded drivers)
//   - Object directory DriverObject name
//
// Returns the number of lists we successfully unlinked from; mostly for
// DbgPrint. Never fails hard — if any one scrub fails the driver still works.
ULONG HideDriver(PDRIVER_OBJECT DriverObject);
