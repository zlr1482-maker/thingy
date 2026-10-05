#include "..\include\Memory.h"
#include <string>
#include <vector>
#include <initializer_list>

// ============================================================
//  ReadMemoryRaw
// ============================================================
bool ReadMemoryRaw(uint64_t pid, uint64_t address, void* buffer, size_t size) {
    if (!buffer || size == 0 || size > 0x10000000) return false;

    // Per-thread reusable scratch buffer to avoid per-call heap allocation.
    // At 300k+ IOCTLs/sec from the cheat hot path, std::vector ctor/dtor
    // becomes the bottleneck (~100ns each + L1 cache pollution that hits
    // the game's render thread).  Thread-local storage keeps each calling
    // thread's buffer alive across calls and grows monotonically to the
    // largest read seen.
    constexpr size_t kInitial = 4096;
    thread_local std::vector<uint8_t> outBuf(kInitial);

    DWORD outSize = (DWORD)(sizeof(ReadMemoryResponse) - 1 + size);
    if (outBuf.size() < outSize) outBuf.resize(outSize);

    ReadMemoryRequest req;
    req.ProcessId = pid;
    req.Address   = address;
    req.Size      = (ULONG64)size;

    DWORD written = 0;
    bool ok = g_Driver.Ioctl(
        IOCTL_READ_PROCESS_MEMORY,
        &req,    sizeof(req),
        outBuf.data(), outSize,
        &written
    );

    if (!ok || written < sizeof(ReadMemoryResponse)) return false;

    ReadMemoryResponse* resp = (ReadMemoryResponse*)outBuf.data();
    if (resp->BytesRead != size) return false;

    memcpy(buffer, resp->Buffer, size);
    return true;
}

// ============================================================
//  WriteMemoryRaw
// ============================================================
bool WriteMemoryRaw(uint64_t pid, uint64_t address, const void* buffer, size_t size) {
    if (!buffer || size == 0 || size > 0x10000000) return false;

    DWORD reqSize = (DWORD)(sizeof(WriteMemoryRequest) - 1 + size);
    std::vector<uint8_t> reqBuf(reqSize, 0);

    WriteMemoryRequest* req = (WriteMemoryRequest*)reqBuf.data();
    req->ProcessId = pid;
    req->Address   = address;
    req->Size      = (ULONG64)size;
    memcpy(req->Buffer, buffer, size);

    WriteMemoryResponse resp{};
    return g_Driver.Ioctl(
        IOCTL_WRITE_PROCESS_MEMORY,
        reqBuf.data(), reqSize,
        &resp, (DWORD)sizeof(resp)
    );
}

// ============================================================
//  ReadWString
// ============================================================
std::wstring ReadWString(uint64_t pid, uint64_t address, size_t maxChars) {
    std::vector<wchar_t> buf(maxChars + 1, 0);
    if (!ReadMemoryRaw(pid, address, buf.data(), maxChars * sizeof(wchar_t)))
        return {};
    buf[maxChars] = L'\0';
    return std::wstring(buf.data());
}

// ============================================================
//  ReadPointerChain
// ============================================================
uint64_t ReadPointerChain(uint64_t pid, uint64_t base,
                          std::initializer_list<uint64_t> offsets)
{
    uint64_t ptr = base;
    auto it  = offsets.begin();
    auto end = offsets.end();

    while (it != end) {
        uint64_t offset = *it++;
        if (it == end) {
            // Last offset — return the address, not the dereference
            return ptr + offset;
        }
        // Not last — dereference
        ptr = ReadMemory<uint64_t>(pid, ptr + offset);
        if (!ptr) return 0;
    }
    return ptr;
}
