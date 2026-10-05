/*
 * Driver.cpp - service lifecycle + device I/O for XenV2-Type2 usermode
 */
#include "..\include\Driver.h"
#include <cstdio>
#include <string>
#include <shellapi.h>

// ============================================================
//  Internal helpers
// ============================================================

// Run a command hidden, return its exit code.
static DWORD RunHidden(const wchar_t* cmdline) {
    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring cmd(cmdline);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;

    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

// Import XenV2TestCert.cer (sitting next to this exe) into the VM's
// Trusted Root CA and Trusted Publishers stores, enable test signing,
// then reboot.  Does not return.
static void EnableTestSigningAndReboot() {
    wchar_t cerPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, cerPath, MAX_PATH);
    wchar_t* slash = wcsrchr(cerPath, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash - cerPath + 1),
                        L"XenV2TestCert.cer");

    if (GetFileAttributesW(cerPath) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"  [!] XenV2TestCert.cer not found next to xentype2.exe.\n");
        return;
    }

    wprintf(L"  [*] Importing test certificate...\n");

    // Import into Root and TrustedPublisher stores via certutil
    wchar_t cmd[MAX_PATH + 64]{};
    swprintf_s(cmd, L"C:\\Windows\\System32\\certutil.exe -addstore Root \"%s\"", cerPath);
    RunHidden(cmd);
    swprintf_s(cmd, L"C:\\Windows\\System32\\certutil.exe -addstore TrustedPublisher \"%s\"", cerPath);
    RunHidden(cmd);

    wprintf(L"  [*] Enabling test signing mode...\n");
    RunHidden(L"C:\\Windows\\System32\\bcdedit.exe /set {current} testsigning on");

    // Acquire shutdown privilege and reboot
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(tok);
    }

    wprintf(L"  [*] Rebooting — run xentype2.exe again after Windows restarts.\n\n");
    Sleep(2000);
    ExitWindowsEx(EWX_REBOOT | EWX_FORCE,
                  SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
    ExitProcess(0);
}

DeviceDriver g_Driver;

// ============================================================
//  Service name (must match driver.inf [Service_Inst])
// ============================================================
static const wchar_t* const SVC_NAME = L"XenV2Type2";

// ============================================================
//  Static helpers
// ============================================================

bool DeviceDriver::IsAdmin() {
    BOOL elevated = FALSE;
    HANDLE token  = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e{};
        DWORD sz = sizeof(e);
        if (GetTokenInformation(token, TokenElevation, &e, sizeof(e), &sz))
            elevated = e.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated == TRUE;
}

void DeviceDriver::RelaunchAsAdmin(int argc, wchar_t* argv[]) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    std::wstring args;
    for (int i = 1; i < argc; i++) {
        if (i > 1) args += L' ';
        args += L'"';
        args += argv[i];
        args += L'"';
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = path;
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.nShow        = SW_SHOW;

    if (!ShellExecuteExW(&sei))
        wprintf(L"  [!] Failed to elevate (err=%lu)\n", GetLastError());
}

bool DeviceDriver::IsKernelDebugEnabled() {
    typedef LONG(WINAPI* PFN)(ULONG, PVOID, ULONG, PULONG);
    auto fn = (PFN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                  "NtQuerySystemInformation");
    if (!fn) return false;
    struct { BOOLEAN Enabled; BOOLEAN NotPresent; } info{};
    return fn(35, &info, (ULONG)sizeof(info), nullptr) >= 0 && info.Enabled;
}

std::wstring DeviceDriver::FindSysPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash)
        wcscpy_s(slash + 1, MAX_PATH - (size_t)(slash - path + 1), L"XenV2Type2.sys");
    return path;
}

// ============================================================
//  Open device handle only
// ============================================================
bool DeviceDriver::Open() {
    m_Handle = CreateFileW(
        XENTYPE2_USERMODE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    return m_Handle != INVALID_HANDLE_VALUE;
}

// ============================================================
//  EnsureLoaded — try SCM path first, fall back to kdmapper
// ============================================================
bool DeviceDriver::EnsureLoaded(const wchar_t* sysPath) {
    if (IsOpen()) return true;

    // 1. Driver may already be running (e.g. from a previous session).
    if (Open()) return true;

    // 2. Verify .sys exists before touching SCM.
    if (GetFileAttributesW(sysPath) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"  [!] Driver binary not found: %s\n"
                L"      Build the driver first (run build.bat).\n", sysPath);
        return false;
    }

    // 3. Try the normal SCM path (install + start service).
    wprintf(L"  [*] Attempting SCM load...\n");
    bool dseBlocked = false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        wprintf(L"  [!] OpenSCManager failed: %lu\n", GetLastError());
        return false;
    }

    {
        SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, SERVICE_ALL_ACCESS);
        if (!svc) {
            if (!Install(sysPath)) {
                CloseServiceHandle(scm);
                return false;
            }
            m_OwnedService = true;
            svc = OpenServiceW(scm, SVC_NAME, SERVICE_ALL_ACCESS);
        }
        CloseServiceHandle(scm);
        if (svc) CloseServiceHandle(svc);
    }

    if (Start(&dseBlocked)) {
        if (Open()) return true;
        wprintf(L"  [!] Driver started but device open failed — symlink missing?\n");
        wprintf(L"      Check WinDbg / DbgView for a [XenV2-Type2] error from DriverEntry.\n");
        return false;
    }

    // Start() failed.  Only auto-reboot if it was DSE (ERROR_INVALID_IMAGE_HASH);
    // any other failure means the driver is refusing to load for some reason we
    // should diagnose, not paper over by rebooting the machine.
    if (!dseBlocked) {
        wprintf(L"  [!] Driver start failed (non-DSE). Not rebooting.\n"
                L"      Likely causes: DriverEntry returned failure, VmxCheckSupport\n"
                L"      rejected the CPU, or a BCD change disabled VT-x access.\n"
                L"      Check the kernel debugger for [XenV2-Type2] log lines.\n");
        return false;
    }

    // DSE case: clean up service entry, enable test signing, reboot.
    Stop();
    Delete();
    m_OwnedService = false;

    wprintf(L"  [!] DSE blocked the driver. Setting up test signing...\n");
    EnableTestSigningAndReboot(); // does not return
    return false;
}

// ============================================================
//  Install (CreateService) — does not start
// ============================================================
bool DeviceDriver::Install(const wchar_t* sysPath) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        wprintf(L"  [!] OpenSCManager (create) failed: %lu\n", GetLastError());
        return false;
    }

    SC_HANDLE svc = CreateServiceW(
        scm,
        SVC_NAME,
        L"XenV2 Type2 Hypervisor Driver",
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        sysPath,
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            CloseServiceHandle(scm);
            return true;
        }
        wprintf(L"  [!] CreateService failed: %lu\n", err);
        CloseServiceHandle(scm);
        return false;
    }

    wprintf(L"  [+] Driver service installed.\n");
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

// ============================================================
//  Start — starts the service, waits up to 5 s for SERVICE_RUNNING
// ============================================================
bool DeviceDriver::Start(bool* pDseBlocked) {
    if (pDseBlocked) *pDseBlocked = false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
    CloseServiceHandle(scm);
    if (!svc) {
        wprintf(L"  [!] OpenService (start) failed: %lu\n", GetLastError());
        return false;
    }

    BOOL ok = StartServiceW(svc, 0, nullptr);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_INVALID_IMAGE_HASH) {
            // 577 — DSE blocked the unsigned driver
            if (pDseBlocked) *pDseBlocked = true;
            CloseServiceHandle(svc);
            return false;
        }
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            wprintf(L"  [!] StartService failed: %lu\n", err);
            CloseServiceHandle(svc);
            return false;
        }
    } else {
        SERVICE_STATUS_PROCESS ssp{};
        DWORD dummy = 0;
        for (int i = 0; i < 50; i++) {
            if (!QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                                      (LPBYTE)&ssp, sizeof(ssp), &dummy)) break;
            if (ssp.dwCurrentState == SERVICE_RUNNING) break;
            Sleep(100);
        }
        wprintf(L"  [+] Driver service started.\n");
    }

    CloseServiceHandle(svc);
    return true;
}

// ============================================================
//  Stop — sends SERVICE_CONTROL_STOP, waits up to 5 s
// ============================================================
bool DeviceDriver::Stop() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME,
                                 SERVICE_STOP | SERVICE_QUERY_STATUS);
    CloseServiceHandle(scm);
    if (!svc) return false;

    SERVICE_STATUS ss{};
    ControlService(svc, SERVICE_CONTROL_STOP, &ss);

    for (int i = 0; i < 50; i++) {
        if (!QueryServiceStatus(svc, &ss)) break;
        if (ss.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(100);
    }

    CloseServiceHandle(svc);
    return true;
}

// ============================================================
//  Delete — removes the service entry from SCM
// ============================================================
bool DeviceDriver::Delete() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SVC_NAME, DELETE);
    CloseServiceHandle(scm);
    if (!svc) return false;

    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    if (ok) wprintf(L"  [+] Driver service removed.\n");
    return ok == TRUE;
}

// ============================================================
//  Close — close device handle only
// ============================================================
void DeviceDriver::Close() {
    if (m_Handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_Handle);
        m_Handle = INVALID_HANDLE_VALUE;
    }
}

// ============================================================
//  Unload — close device handle ONLY.
//
//  Our driver's self-hide scrubs KLDR entry + DriverObject fields, and
//  nulls DriverUnload.  If SCM sends SERVICE_CONTROL_STOP, the kernel's
//  IopUnloadDriver walks our mangled structures and bugchecks 0x1E
//  (NULL read at KLDR offset 0x44).  Reboot is the only safe removal
//  path — never call Stop() / Delete() here regardless of m_OwnedService.
//  Matches the kdmapper / manual-map story: driver stays in memory
//  until next reboot.
// ============================================================
void DeviceDriver::Unload() {
    Close();
    // Intentionally do NOT Stop()/Delete() the service.  See header comment.
}

// ============================================================
//  Ioctl
// ============================================================
bool DeviceDriver::Ioctl(DWORD code,
                         void* inBuf,  DWORD inSize,
                         void* outBuf, DWORD outSize,
                         DWORD* outBytes) const
{
    DWORD bytes = 0;
    BOOL  ok    = DeviceIoControl(
        m_Handle, code,
        inBuf,  inSize,
        outBuf, outSize,
        &bytes, nullptr
    );
    if (outBytes) *outBytes = bytes;
    return ok == TRUE;
}

// ============================================================
//  GetVmxStatus
// ============================================================
bool DeviceDriver::GetVmxStatus(VmxStatusResponse& out) const {
    return Ioctl(IOCTL_GET_VMX_STATUS, nullptr, 0, &out, (DWORD)sizeof(out));
}
