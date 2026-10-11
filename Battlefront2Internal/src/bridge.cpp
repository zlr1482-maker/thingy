#include "bridge.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstdlib>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace bf2dev {
namespace {

constexpr INTERNET_PORT kBridgePort = 29472;
constexpr wchar_t kBridgeHost[] = L"127.0.0.1";

struct HttpHandle {
    HINTERNET h = nullptr;
    ~HttpHandle() { if (h) WinHttpCloseHandle(h); }
    HttpHandle() = default;
    explicit HttpHandle(HINTERNET value) : h(value) {}
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};

std::string GetEnv(const char* name) {
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value) return {};
    std::string out(value);
    free(value);
    return out;
}

BridgeResult Request(const wchar_t* verb, const wchar_t* path) {
    BridgeResult result;

    const std::string token = GetEnv("BF2_DEV_TOKEN");
    if (token.empty()) {
        result.message = "BF2_DEV_TOKEN is not set";
        return result;
    }

    HttpHandle session(WinHttpOpen(
        L"Battlefront2DevMenu/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.h) {
        result.message = "WinHttpOpen failed";
        return result;
    }

    WinHttpSetTimeouts(session.h, 300, 300, 500, 500);

    HttpHandle connection(WinHttpConnect(session.h, kBridgeHost, kBridgePort, 0));
    if (!connection.h) {
        result.message = "Kyber bridge is not running on 127.0.0.1:29472";
        return result;
    }

    HttpHandle request(WinHttpOpenRequest(
        connection.h,
        verb,
        path,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0));
    if (!request.h) {
        result.message = "WinHttpOpenRequest failed";
        return result;
    }

    std::wstring header = L"X-BF2-Dev-Token: ";
    header.append(token.begin(), token.end());
    header += L"\r\n";

    if (!WinHttpSendRequest(
            request.h,
            header.c_str(),
            static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        result.message = "Could not send request to Kyber bridge";
        return result;
    }

    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        result.message = "Kyber bridge did not return a response";
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(
            request.h,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        result.httpStatus = static_cast<int>(status);
    }

    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request.h, &available) && available > 0) {
        const size_t oldSize = body.size();
        body.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, body.data() + oldSize, available, &read)) break;
        body.resize(oldSize + read);
    }

    result.ok = result.httpStatus >= 200 && result.httpStatus < 300;
    result.message = body.empty()
        ? (result.ok ? "Kyber bridge accepted the request" : "Kyber bridge rejected the request")
        : body;
    return result;
}

} // namespace

BridgeResult BridgeHealthCheck() {
    return Request(L"GET", L"/bf2dev/health");
}

BridgeResult BridgeForceStartLobby() {
    return Request(L"POST", L"/bf2dev/force-start");
}

} // namespace bf2dev
