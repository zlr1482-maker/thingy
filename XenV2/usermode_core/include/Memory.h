#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <initializer_list>
#include "Driver.h"

// ============================================================
//  Raw memory R/W
// ============================================================
bool ReadMemoryRaw(uint64_t pid, uint64_t address, void* buffer, size_t size);
bool WriteMemoryRaw(uint64_t pid, uint64_t address, const void* buffer, size_t size);

// ============================================================
//  Typed template wrappers — primary interface for game hacking
//
//  Usage:
//    float health = ReadMemory<float>(pid, baseAddress + 0x3A8);
//    WriteMemory<int>(pid, ammoAddress, 999);
// ============================================================
template<typename T>
T ReadMemory(uint64_t pid, uint64_t address) {
    T val{};
    ReadMemoryRaw(pid, address, &val, sizeof(T));
    return val;
}

template<typename T>
bool WriteMemory(uint64_t pid, uint64_t address, const T& value) {
    return WriteMemoryRaw(pid, address, &value, sizeof(T));
}

// Read a null-terminated wide string from the target process (up to maxChars)
std::wstring ReadWString(uint64_t pid, uint64_t address, size_t maxChars = 256);

// Pointer chain: reads a chain of offsets starting from base.
// e.g. ReadPointerChain(pid, base, {0x18, 0x30, 0x8}) resolves:
//   ptr = *(base + 0x18), ptr = *(ptr + 0x30), return ptr + 0x8
uint64_t ReadPointerChain(uint64_t pid, uint64_t base, std::initializer_list<uint64_t> offsets);
