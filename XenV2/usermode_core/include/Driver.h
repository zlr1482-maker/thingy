#pragma once
#include <Windows.h>
#include <string>
#include "..\..\shared\Shared.h"

// ============================================================
//  DeviceDriver — device handle + service lifecycle management
//
//  Typical flow:
//    g_Driver.EnsureLoaded(L"C:\\path\\XenV2Type2.sys");
//    ... use IOCTLs ...
//    g_Driver.Unload();   // stops + deletes the service if we installed it
// ============================================================
class DeviceDriver {
public:
    DeviceDriver()  : m_Handle(INVALID_HANDLE_VALUE), m_OwnedService(false), m_KdMapped(false) {}
    ~DeviceDriver() { Unload(); }

    // Try to open the device.  If the driver isn't loaded yet, install it from
    // sysPath, start the service, then open.  Returns false if anything fails.
    bool EnsureLoaded(const wchar_t* sysPath);

    // Close device handle.  If we installed the service, stop and delete it.
    void Unload();

    // Just close the handle (does NOT stop the service).
    void Close();

    bool IsOpen() const { return m_Handle != INVALID_HANDLE_VALUE; }

    // Low-level IOCTL wrapper.
    bool Ioctl(DWORD code,
               void* inBuf,  DWORD inSize,
               void* outBuf, DWORD outSize,
               DWORD* outBytes = nullptr) const;

    // Convenience wrappers
    bool GetVmxStatus(VmxStatusResponse& out) const;

    // --------------------------------------------------------
    //  Static helpers
    // --------------------------------------------------------

    // Returns true if kernel debug mode is on (bcdedit /debug on).
    // Required for unsigned drivers to load without test signing.
    static bool IsKernelDebugEnabled();

    // Returns true if the current process has admin rights.
    static bool IsAdmin();

    // Re-launch the current exe elevated via ShellExecuteEx (runas).
    // Call this, then exit the un-elevated process.
    static void RelaunchAsAdmin(int argc, wchar_t* argv[]);

    // Returns the full path of the .sys file expected next to this exe.
    // Convention: replace the exe filename with "XenV2Type2.sys".
    static std::wstring FindSysPath();

private:
    HANDLE m_Handle;
    bool   m_OwnedService;  // true if WE called CreateService (so we clean up)
    bool   m_KdMapped;      // true if loaded via kdmapper (no SCM cleanup needed)

    // Open device handle only.  Does not touch the service.
    bool Open();

    // Install service (CreateService).  Does not start it.
    bool Install(const wchar_t* sysPath);

    // Start an installed service.  Waits up to 5 s for SERVICE_RUNNING.
    // Sets *pDseBlocked=true if error 577 prevented loading.
    bool Start(bool* pDseBlocked = nullptr);

    // Stop the service.  Waits up to 5 s for SERVICE_STOPPED.
    bool Stop();

    // Delete the service from SCM.
    bool Delete();
};

extern DeviceDriver g_Driver;
