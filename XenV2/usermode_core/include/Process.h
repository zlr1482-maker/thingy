#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>

// ============================================================
//  Process utilities (usermode side)
// ============================================================

// Find the first process whose name matches (case-insensitive).
// Returns 0 if not found.
uint64_t FindProcessId(const std::wstring& processName);

// Get the base address of a module in a remote process.
// Uses the driver's IOCTL_GET_MODULE_BASE (kernel PEB walk).
// Returns 0 on failure.
uint64_t GetModuleBase(uint64_t pid, const std::wstring& moduleName, uint64_t* sizeOut = nullptr);

// Get the CR3 (DirectoryTableBase) of a process.
uint64_t GetProcessCr3(uint64_t pid);
