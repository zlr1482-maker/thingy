#pragma once
#include <wdm.h>

// Handle IRP_MJ_DEVICE_CONTROL — dispatches to per-IOCTL handlers below.
NTSTATUS IoctlDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
