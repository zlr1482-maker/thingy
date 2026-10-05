#include "..\include\Process.h"
#include "..\include\Driver.h"
#include <TlHelp32.h>
#include <algorithm>
#include <cwctype>

// ============================================================
//  FindProcessId — snapshot-based PID lookup
// ============================================================
uint64_t FindProcessId(const std::wstring& processName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    uint64_t pid = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            std::wstring name(entry.szExeFile);
            // Case-insensitive compare
            if (name.size() == processName.size()) {
                bool match = true;
                for (size_t i = 0; i < name.size() && match; i++) {
                    if (std::towlower(name[i]) != std::towlower(processName[i]))
                        match = false;
                }
                if (match) {
                    pid = entry.th32ProcessID;
                    break;
                }
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return pid;
}

// ============================================================
//  GetModuleBase — asks the kernel driver (PEB walk)
// ============================================================
uint64_t GetModuleBase(uint64_t pid, const std::wstring& moduleName, uint64_t* sizeOut) {
    ModuleBaseRequest req{};
    req.ProcessId = pid;

    // Copy module name (up to 63 chars + null)
    size_t copyLen = min(moduleName.size(), (size_t)63);
    wcsncpy_s(req.ModuleName, 64, moduleName.c_str(), copyLen);

    ModuleBaseResponse resp{};
    bool ok = g_Driver.Ioctl(
        IOCTL_GET_MODULE_BASE,
        &req,  (DWORD)sizeof(req),
        &resp, (DWORD)sizeof(resp)
    );

    if (!ok) return 0;
    if (sizeOut) *sizeOut = resp.ModuleSize;
    return resp.BaseAddress;
}

// ============================================================
//  GetProcessCr3 — asks the kernel driver
// ============================================================
uint64_t GetProcessCr3(uint64_t pid) {
    Cr3Request  req  { pid };
    Cr3Response resp {};

    bool ok = g_Driver.Ioctl(
        IOCTL_GET_PROCESS_CR3,
        &req,  (DWORD)sizeof(req),
        &resp, (DWORD)sizeof(resp)
    );

    return ok ? resp.Cr3Value : 0;
}
