/*
 * Main.cpp - XenV2-Type2 usermode application
 *
 * Simple interactive CLI for process/memory inspection.
 * Extend this with your own game-specific logic.
 *
 * Usage:
 *   xentype2.exe               — interactive CLI
 *   xentype2.exe <process.exe> — auto-attach and enter CLI
 */

#include "..\include\Driver.h"
#include "..\include\Memory.h"
#include "..\include\Process.h"
#include <cstdio>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cwctype>
#include <iostream>

// ============================================================
//  Helpers
// ============================================================

static void PrintHex(const uint8_t* data, size_t size, uint64_t baseAddr = 0) {
    for (size_t i = 0; i < size; i += 16) {
        wprintf(L"  %016llX  ", baseAddr + i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) wprintf(L"%02X ", data[i + j]);
            else               wprintf(L"   ");
            if (j == 7) wprintf(L" ");
        }
        wprintf(L" |");
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            uint8_t c = data[i + j];
            wprintf(L"%c", (c >= 0x20 && c < 0x7F) ? (wchar_t)c : L'.');
        }
        wprintf(L"|\n");
    }
}

static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s;
}

// ============================================================
//  Command handlers
// ============================================================

static uint64_t g_TargetPid = 0;

static void CmdHelp() {
    wprintf(L"\n  Commands:\n"
            L"    vmxstart                    -- start hypervisor on all cores\n"
            L"    vmxstop                     -- stop hypervisor\n"
            L"    status                      -- driver / VMX status\n"
            L"    attach <process.exe>         -- find PID and attach\n"
            L"    pid <number>                 -- attach to explicit PID\n"
            L"    module <name>               -- get module base + size\n"
            L"    cr3                         -- get process CR3\n"
            L"    read  <hex_addr> <bytes>    -- hex dump from target process\n"
            L"    write <hex_addr> <hex_bytes> -- write bytes (e.g. write 0x1000 DEADBEEF)\n"
            L"    readptr <hex_addr>           -- read 8-byte pointer\n"
            L"    kmod <basename>              -- resolve kernel module base (e.g. nt, ci)\n"
            L"    kexport <mod> <sym>          -- resolve kernel export VA\n"
            L"    kread <hex_addr> <bytes>     -- read kernel memory\n"
            L"    kwrite <hex_addr> <hex>      -- write kernel memory\n"
            L"    dse off [hex_addr]           -- zero g_CiOptions (auto-discover or explicit)\n"
            L"    pg off                       -- neuter PatchGuard (EPT hook KeBugCheckEx; VMX req'd)\n"
            L"    quit                        -- exit\n\n");
}

static void CmdStatus() {
    VmxStatusResponse resp{};
    if (!g_Driver.GetVmxStatus(resp)) {
        wprintf(L"  [!] Failed to query VMX status (driver not loaded?)\n");
        return;
    }
    wprintf(L"\n  VMX initialized : %s\n",  resp.VmxInitialized ? L"YES" : L"NO");
    wprintf(L"  Logical cores   : %u\n",   resp.LogicalCoreCount);
    wprintf(L"  IA32_VMX_BASIC  : 0x%016llX\n", resp.VmxBasicMsr);
    wprintf(L"  EPTP            : 0x%016llX\n", resp.EptpValue);
    if (resp.LastVmlaunchError) {
        UINT64 e = resp.LastVmlaunchError;
        const wchar_t* step = (e & 0xFFFF0000) == 0xDEAD0000 ? L"VMXON"
                            : e == 0xDEAD0002 ? L"VMCLEAR"
                            : e == 0xDEAD0003 ? L"VMPTRLD"
                            : e == 0xDEAD0004 ? L"VMLAUNCH(VMfailInvalid-VMCS not current)"
                            : e == 0xDEAD0005 ? L"VMLAUNCH(VMfailValid-errcode=0?)"
                            : L"VMLAUNCH(VMfailValid)";
        wprintf(L"  Last VMX failure : step=%s  code=0x%llX  core=%u\n",
                step, e, resp.LastFailedCore);
    }
    if (resp.VmxInitialized) {
        for (UINT32 i = 0; i < resp.LogicalCoreCount && i < 8; i++) {
            wprintf(L"    core %u : %llu exits, last reason=%llu\n",
                    i, resp.VmExitCount[i], resp.VmExitLastReason[i]);
        }
    }
    wprintf(L"\n");
}

static void CmdAttach(const std::wstring& name) {
    g_TargetPid = FindProcessId(name);
    if (!g_TargetPid) {
        wprintf(L"  [!] Process '%s' not found.\n", name.c_str());
        return;
    }
    wprintf(L"  [+] Attached to '%s'  PID=%llu\n", name.c_str(), g_TargetPid);
}

static void CmdModule(const std::wstring& name) {
    if (!g_TargetPid) { wprintf(L"  [!] No process attached.\n"); return; }
    uint64_t sz   = 0;
    uint64_t base = GetModuleBase(g_TargetPid, name, &sz);
    if (!base) { wprintf(L"  [!] Module '%s' not found.\n", name.c_str()); return; }
    wprintf(L"  [+] %-40s  base=0x%016llX  size=0x%llX\n", name.c_str(), base, sz);
}

static void CmdCr3() {
    if (!g_TargetPid) { wprintf(L"  [!] No process attached.\n"); return; }
    uint64_t cr3 = GetProcessCr3(g_TargetPid);
    if (!cr3) { wprintf(L"  [!] Failed to get CR3.\n"); return; }
    wprintf(L"  [+] CR3 = 0x%016llX\n", cr3);
}

static void CmdRead(uint64_t addr, size_t bytes) {
    if (!g_TargetPid) { wprintf(L"  [!] No process attached.\n"); return; }
    if (bytes == 0 || bytes > 0x1000) { wprintf(L"  [!] Size must be 1–4096.\n"); return; }

    std::vector<uint8_t> buf(bytes, 0);
    if (!ReadMemoryRaw(g_TargetPid, addr, buf.data(), bytes)) {
        wprintf(L"  [!] Read failed.\n"); return;
    }
    wprintf(L"\n");
    PrintHex(buf.data(), bytes, addr);
    wprintf(L"\n");
}

static void CmdWrite(uint64_t addr, const std::wstring& hexStr) {
    if (!g_TargetPid) { wprintf(L"  [!] No process attached.\n"); return; }

    // Parse hex string (e.g. "DEADBEEF" or "DE AD BE EF")
    std::vector<uint8_t> data;
    for (size_t i = 0; i < hexStr.size(); ) {
        while (i < hexStr.size() && hexStr[i] == L' ') i++;
        if (i + 1 >= hexStr.size()) break;
        wchar_t hi = hexStr[i], lo = hexStr[i + 1];
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        int h = hex(hi), l = hex(lo);
        if (h < 0 || l < 0) { wprintf(L"  [!] Invalid hex.\n"); return; }
        data.push_back((uint8_t)((h << 4) | l));
        i += 2;
    }

    if (data.empty()) { wprintf(L"  [!] No bytes parsed.\n"); return; }
    if (!WriteMemoryRaw(g_TargetPid, addr, data.data(), data.size())) {
        wprintf(L"  [!] Write failed.\n"); return;
    }
    wprintf(L"  [+] Wrote %zu byte(s) to 0x%016llX\n", data.size(), addr);
}

// ============================================================
//  Kernel-mode helpers (for DSE bypass / PG neuter)
// ============================================================

static void CmdKmod(const std::wstring& name) {
    DiscoverKModuleRequest req{};
    wcsncpy_s(req.BaseName, name.c_str(), _TRUNCATE);
    DiscoverKModuleResponse resp{};
    DWORD bytes = 0;
    if (!g_Driver.Ioctl(IOCTL_DISCOVER_KMODULE, &req, sizeof(req), &resp, sizeof(resp), &bytes)
        || !resp.Base) {
        wprintf(L"  [!] kmod '%s' not found.\n", name.c_str());
        return;
    }
    wprintf(L"  [+] %s  base=0x%016llX  size=0x%llX\n", name.c_str(), resp.Base, resp.Size);
}

static void CmdKexport(const std::wstring& mod, const std::wstring& sym) {
    DiscoverExportRequest req{};
    wcsncpy_s(req.ModuleBaseName, mod.c_str(), _TRUNCATE);
    // wchar -> ascii for symbol
    char asym[128] = {};
    for (size_t i = 0; i < sym.size() && i < 127; i++)
        asym[i] = (char)(sym[i] & 0x7F);
    memcpy(req.SymbolName, asym, sizeof(req.SymbolName));
    DiscoverExportResponse resp{};
    DWORD bytes = 0;
    if (!g_Driver.Ioctl(IOCTL_DISCOVER_EXPORT, &req, sizeof(req), &resp, sizeof(resp), &bytes)
        || !resp.Address) {
        wprintf(L"  [!] %s!%s not found.\n", mod.c_str(), sym.c_str());
        return;
    }
    wprintf(L"  [+] %s!%s = 0x%016llX\n", mod.c_str(), sym.c_str(), resp.Address);
}

static void CmdKread(uint64_t addr, size_t bytes) {
    if (bytes == 0 || bytes > 0x1000) { wprintf(L"  [!] Size must be 1-4096.\n"); return; }
    struct { KernelReadResponse hdr; uint8_t tail[0x1000]; } buf{};
    KernelReadRequest req{ addr, (uint64_t)bytes };
    DWORD got = 0;
    if (!g_Driver.Ioctl(IOCTL_KERNEL_READ, &req, sizeof(req), &buf, sizeof(buf), &got)) {
        wprintf(L"  [!] kread failed (err=%lu).\n", GetLastError());
        return;
    }
    wprintf(L"\n");
    PrintHex(buf.hdr.Buffer, (size_t)buf.hdr.BytesRead, addr);
    wprintf(L"\n");
}

static void CmdKwrite(uint64_t addr, const std::wstring& hexStr) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < hexStr.size(); ) {
        while (i < hexStr.size() && hexStr[i] == L' ') i++;
        if (i + 1 >= hexStr.size()) break;
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        int h = hex(hexStr[i]), l = hex(hexStr[i + 1]);
        if (h < 0 || l < 0) { wprintf(L"  [!] Invalid hex.\n"); return; }
        data.push_back((uint8_t)((h << 4) | l));
        i += 2;
    }
    if (data.empty()) { wprintf(L"  [!] No bytes parsed.\n"); return; }

    std::vector<uint8_t> io(sizeof(KernelWriteRequest) - 1 + data.size(), 0);
    auto* req = (KernelWriteRequest*)io.data();
    req->Address = addr;
    req->Size    = data.size();
    memcpy(req->Buffer, data.data(), data.size());
    DWORD got = 0;
    if (!g_Driver.Ioctl(IOCTL_KERNEL_WRITE, io.data(), (DWORD)io.size(), nullptr, 0, &got)) {
        wprintf(L"  [!] kwrite failed (err=%lu).\n", GetLastError());
        return;
    }
    wprintf(L"  [+] Wrote %zu byte(s) to kernel 0x%016llX\n", data.size(), addr);
}

static void CmdDseOff(uint64_t explicitAddr) {
    DseDisableRequest req{ explicitAddr };
    DseDisableResponse resp{};
    DWORD bytes = 0;
    if (!g_Driver.Ioctl(IOCTL_DSE_DISABLE, &req, sizeof(req), &resp, sizeof(resp), &bytes)) {
        wprintf(L"  [!] DSE disable failed (err=%lu).\n", GetLastError());
        return;
    }
    wprintf(L"  [+] g_CiOptions @ 0x%016llX  0x%08X -> 0x%08X (enforce bits cleared)\n",
            resp.GCiOptionsVa, resp.OriginalValue, resp.OriginalValue & ~0x6u);
}

static void CmdReadPtr(uint64_t addr) {
    if (!g_TargetPid) { wprintf(L"  [!] No process attached.\n"); return; }
    uint64_t val = ReadMemory<uint64_t>(g_TargetPid, addr);
    wprintf(L"  [0x%016llX] -> 0x%016llX\n", addr, val);
}

// ============================================================
//  Main REPL loop
// ============================================================

int wmain(int argc, wchar_t* argv[]) {
    wprintf(L"\n  XenV2-Type2  —  Educational hypervisor + memory tool\n");
    wprintf(L"  --------------------------------------------------------\n\n");

    // Must be admin to install/start kernel drivers.
    if (!DeviceDriver::IsAdmin()) {
        wprintf(L"  [!] Not running as administrator. Re-launching elevated...\n\n");
        DeviceDriver::RelaunchAsAdmin(argc, argv);
        return 0;
    }

    // Find XenV2Type2.sys next to this exe.
    std::wstring sysPath = DeviceDriver::FindSysPath();
    wprintf(L"  [*] Driver path: %s\n", sysPath.c_str());

    // Install + start the driver (or attach if already running).
    if (!g_Driver.EnsureLoaded(sysPath.c_str())) {
        wprintf(L"  [!] Failed to load driver. Aborting.\n\n");
        return 1;
    }
    wprintf(L"  [+] Driver connected.\n");

    // Auto-attach if process name provided on command line
    if (argc >= 2) {
        CmdAttach(std::wstring(argv[1]));
    }

    CmdHelp();

    std::wstring line;
    while (true) {
        wprintf(L"xen> ");
        if (!std::getline(std::wcin, line)) break;

        // Trim leading spaces
        size_t start = line.find_first_not_of(L" \t");
        if (start == std::wstring::npos) continue;
        line = line.substr(start);
        if (line.empty()) continue;

        std::wistringstream ss(line);
        std::wstring cmd;
        ss >> cmd;
        cmd = ToLower(cmd);

        if (cmd == L"quit" || cmd == L"exit" || cmd == L"q") {
            break;
        } else if (cmd == L"help" || cmd == L"?") {
            CmdHelp();
        } else if (cmd == L"vmxstart") {
            if (g_Driver.Ioctl(IOCTL_VMX_START, nullptr, 0, nullptr, 0))
                wprintf(L"  [+] Hypervisor started.\n");
            else
                wprintf(L"  [!] vmxstart failed (err=%lu).\n", GetLastError());
        } else if (cmd == L"vmxstop") {
            if (g_Driver.Ioctl(IOCTL_VMX_STOP, nullptr, 0, nullptr, 0))
                wprintf(L"  [+] Hypervisor stopped.\n");
            else
                wprintf(L"  [!] vmxstop failed (err=%lu).\n", GetLastError());
        } else if (cmd == L"status") {
            CmdStatus();
        } else if (cmd == L"attach") {
            std::wstring name; ss >> name;
            if (name.empty()) wprintf(L"  Usage: attach <process.exe>\n");
            else CmdAttach(name);
        } else if (cmd == L"pid") {
            uint64_t pid = 0; ss >> pid;
            if (!pid) wprintf(L"  Usage: pid <number>\n");
            else { g_TargetPid = pid; wprintf(L"  [+] Target PID set to %llu\n", pid); }
        } else if (cmd == L"module") {
            std::wstring name; ss >> name;
            if (name.empty()) wprintf(L"  Usage: module <name>\n");
            else CmdModule(name);
        } else if (cmd == L"cr3") {
            CmdCr3();
        } else if (cmd == L"read") {
            uint64_t addr = 0; size_t bytes = 0;
            ss >> std::hex >> addr >> std::dec >> bytes;
            if (!addr || !bytes) wprintf(L"  Usage: read <hex_addr> <bytes>\n");
            else CmdRead(addr, bytes);
        } else if (cmd == L"readptr") {
            uint64_t addr = 0;
            ss >> std::hex >> addr;
            if (!addr) wprintf(L"  Usage: readptr <hex_addr>\n");
            else CmdReadPtr(addr);
        } else if (cmd == L"write") {
            uint64_t addr = 0;
            ss >> std::hex >> addr;
            std::wstring hex;
            std::getline(ss, hex);
            if (hex.size() > 0 && hex[0] == L' ') hex = hex.substr(1);
            if (!addr || hex.empty()) wprintf(L"  Usage: write <hex_addr> <hex_bytes>\n");
            else CmdWrite(addr, hex);
        } else if (cmd == L"kmod") {
            std::wstring name; ss >> name;
            if (name.empty()) wprintf(L"  Usage: kmod <basename>\n");
            else CmdKmod(name);
        } else if (cmd == L"kexport") {
            std::wstring mod, sym; ss >> mod >> sym;
            if (mod.empty() || sym.empty()) wprintf(L"  Usage: kexport <mod> <sym>\n");
            else CmdKexport(mod, sym);
        } else if (cmd == L"kread") {
            uint64_t addr = 0; size_t bytes = 0;
            ss >> std::hex >> addr >> std::dec >> bytes;
            if (!addr || !bytes) wprintf(L"  Usage: kread <hex_addr> <bytes>\n");
            else CmdKread(addr, bytes);
        } else if (cmd == L"kwrite") {
            uint64_t addr = 0;
            ss >> std::hex >> addr;
            std::wstring hex; std::getline(ss, hex);
            if (hex.size() > 0 && hex[0] == L' ') hex = hex.substr(1);
            if (!addr || hex.empty()) wprintf(L"  Usage: kwrite <hex_addr> <hex_bytes>\n");
            else CmdKwrite(addr, hex);
        } else if (cmd == L"dse") {
            std::wstring sub; ss >> sub; sub = ToLower(sub);
            if (sub == L"off") {
                uint64_t addr = 0; ss >> std::hex >> addr;
                CmdDseOff(addr);
            } else {
                wprintf(L"  Usage: dse off [hex_addr]\n");
            }
        } else if (cmd == L"pg") {
            std::wstring sub; ss >> sub; sub = ToLower(sub);
            if (sub == L"off") {
                DWORD bytes = 0;
                if (g_Driver.Ioctl(IOCTL_PG_NEUTER, nullptr, 0, nullptr, 0, &bytes))
                    wprintf(L"  [+] PG neuter installed (KeBugCheckEx -> ret).\n");
                else
                    wprintf(L"  [!] PG neuter failed (err=%lu). VMX running?\n", GetLastError());
            } else {
                wprintf(L"  Usage: pg off\n");
            }
        } else {
            wprintf(L"  Unknown command '%s'. Type 'help'.\n", cmd.c_str());
        }
    }

    wprintf(L"\n  [*] Shutting down...\n");
    g_Driver.Unload();
    return 0;
}
