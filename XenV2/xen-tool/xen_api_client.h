// xen_api_client.h — minimal WinHTTP client for the xen-api server.
//
// Provides:
//   - login(key) -> bearer token
//   - fetch_offsets(game) -> raw JSON string
//   - parse helpers (hex, string field)
//
// Header-only: include in main.cpp.  Pulls in winhttp.lib (added to the
// build script).
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <cstdint>
#include <vector>
#include <optional>
#include <unordered_map>

#pragma comment(lib, "winhttp.lib")

namespace xen_api {

struct Endpoint {
    std::wstring host;       // e.g. L"xen.example.com" or L"127.0.0.1"
    INTERNET_PORT port = 0;  // e.g. 443 or 8080
    bool         tls  = false;
};

// Parse "https://host:port" or "http://host[:port]" into an Endpoint.
inline std::optional<Endpoint> ParseUrl(const std::wstring& url) {
    Endpoint ep;
    std::wstring rest;
    if (url.rfind(L"https://", 0) == 0) { ep.tls = true;  ep.port = INTERNET_DEFAULT_HTTPS_PORT; rest = url.substr(8); }
    else if (url.rfind(L"http://", 0) == 0) { ep.tls = false; ep.port = INTERNET_DEFAULT_HTTP_PORT;  rest = url.substr(7); }
    else return std::nullopt;
    auto slash = rest.find(L'/');
    auto hp = rest.substr(0, slash);
    auto colon = hp.find(L':');
    if (colon == std::wstring::npos) {
        ep.host = hp;
    } else {
        ep.host = hp.substr(0, colon);
        try { ep.port = (INTERNET_PORT)std::stoi(hp.substr(colon + 1)); }
        catch (...) { return std::nullopt; }
    }
    if (ep.host.empty() || ep.port == 0) return std::nullopt;
    return ep;
}

// Lifetime-managed handle wrapper.
struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET hh) : h(hh) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    HINTERNET get() const { return h; }
    operator bool() const { return h != nullptr; }
};

struct Response {
    int          status = 0;
    std::string  body;
    bool         ok() const { return status >= 200 && status < 300; }
};

// Issue an HTTP request.  `verb` = L"GET", L"POST", L"PUT".  `body` may be
// empty.  Headers must be CRLF-separated; bearer token is appended if
// present.  Returns a Response on every code path; status==0 means we
// failed to even reach the server.
inline Response Request(const Endpoint& ep,
                        const std::wstring& verb,
                        const std::wstring& path,
                        const std::string&  body,
                        const std::wstring& bearer = L"",
                        const std::wstring& contentType = L"application/json")
{
    Response out;

    WinHttpHandle session(WinHttpOpen(L"xen-tool/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS,
                                      0));
    if (!session) return out;

    // Reasonable timeouts so a dead server doesn't block the GUI thread.
    DWORD t = 5000;
    WinHttpSetTimeouts(session.get(), t, t, t, t);

    WinHttpHandle conn(WinHttpConnect(session.get(), ep.host.c_str(), ep.port, 0));
    if (!conn) return out;

    DWORD reqFlags = ep.tls ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle req(WinHttpOpenRequest(conn.get(),
                                         verb.c_str(),
                                         path.c_str(),
                                         nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         reqFlags));
    if (!req) return out;

    // Build header block.
    std::wstring headers;
    if (!body.empty()) {
        headers += L"Content-Type: " + contentType + L"\r\n";
    }
    if (!bearer.empty()) {
        headers += L"Authorization: Bearer " + bearer + L"\r\n";
    }
    LPCWSTR hdrs = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    DWORD   hlen = headers.empty() ? 0 : (DWORD)-1;

    if (!WinHttpSendRequest(req.get(), hdrs, hlen,
                            (LPVOID)body.data(), (DWORD)body.size(),
                            (DWORD)body.size(), 0)) return out;
    if (!WinHttpReceiveResponse(req.get(), nullptr)) return out;

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    out.status = (int)status;

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req.get(), &avail) && avail > 0) {
        size_t prev = out.body.size();
        out.body.resize(prev + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.get(), out.body.data() + prev, avail, &read)) break;
        out.body.resize(prev + read);
    }
    return out;
}

// ============================================================== JSON ====

// Find the first JSON-string-valued field "<name>" and return its raw
// contents (with escapes left in place).  Returns empty optional if not
// found.  Keeps the parser tiny — we only consume the API's well-formed
// output, not arbitrary user JSON.
inline std::optional<std::string> JsonStringField(const std::string& json, const std::string& name) {
    std::string needle = "\"" + name + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '"') return std::nullopt;
    p++;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            char c = json[p + 1];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case '/': out += '/';  break;
                default:  out += c;    break;
            }
            p += 2;
        } else {
            out += json[p++];
        }
    }
    return out;
}

// Parse a hex string like "0x1234ABCD" or "0X1234abcd".  Empty / missing
// 0x prefix returns 0.
inline uint64_t ParseHex(const std::string& s) {
    size_t i = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        v <<= 4;
        if      (c >= '0' && c <= '9') v |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (uint64_t)(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') v |= (uint64_t)(10 + c - 'A');
        else { return 0; }
    }
    return v;
}

inline uint64_t JsonHexField(const std::string& json, const std::string& name) {
    auto s = JsonStringField(json, name);
    if (!s) return 0;
    return ParseHex(*s);
}

// Extract the substring of the *object* value (incl. the surrounding {}) of
// a named field.  Used for { "entitlements": { ... }, ... } where we want
// to recurse into the inner block.  Handles nested braces and ignores
// braces inside strings.
inline std::optional<std::string> JsonSubObject(const std::string& json, const std::string& name) {
    std::string needle = "\"" + name + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '{') return std::nullopt;
    int    depth = 1;
    size_t start = p;
    p++;
    while (p < json.size() && depth > 0) {
        char c = json[p];
        if (c == '"') {
            p++;
            while (p < json.size() && json[p] != '"') {
                if (json[p] == '\\' && p + 1 < json.size()) p++;
                p++;
            }
        } else if (c == '{') depth++;
        else if   (c == '}') depth--;
        if (p < json.size()) p++;
    }
    if (depth != 0) return std::nullopt;
    return json.substr(start, p - start);
}

// Find the array value of a named field and return each top-level
// `{...}` object inside it as a substring.  Returns empty if no such
// array.  Honours nested braces and string escapes — fine for the
// well-formed JSON our API emits.
inline std::vector<std::string> JsonArrayObjects(const std::string& json,
                                                 const std::string& name) {
    std::vector<std::string> out;
    std::string needle = "\"" + name + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return out;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    if (p >= json.size() || json[p] != '[') return out;
    p++;
    while (p < json.size()) {
        while (p < json.size() && (json[p] == ' ' || json[p] == ',' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) p++;
        if (p >= json.size() || json[p] == ']') break;
        if (json[p] != '{') { p++; continue; }
        size_t start = p; int depth = 1; p++;
        while (p < json.size() && depth > 0) {
            char c = json[p];
            if (c == '"') {
                p++;
                while (p < json.size() && json[p] != '"') {
                    if (json[p] == '\\' && p + 1 < json.size()) p++;
                    p++;
                }
            } else if (c == '{') depth++;
            else if   (c == '}') depth--;
            if (p < json.size()) p++;
        }
        out.push_back(json.substr(start, p - start));
    }
    return out;
}

// Parse a numeric (non-quoted) field, e.g. "expires": 1717000000.
inline int64_t JsonIntField(const std::string& json, const std::string& name) {
    std::string needle = "\"" + name + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return 0;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
    int64_t sign = 1, v = 0;
    if (p < json.size() && json[p] == '-') { sign = -1; p++; }
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') {
        v = v * 10 + (int64_t)(json[p] - '0');
        p++;
    }
    return v * sign;
}

// ============================================================ helpers ===

inline std::wstring Utf8ToWide(const std::string& in) {
    if (in.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, in.data(), (int)in.size(), out.data(), n);
    return out;
}

// ============================================================ high-level

struct Entitlement {
    int64_t      expires = 0;          // unix timestamp
    std::string  tier    = "standard";
};

struct Client {
    Endpoint                                       endpoint;
    std::wstring                                   token;     // bearer
    std::string                                    lastError;
    std::unordered_map<std::string, Entitlement>   entitlements;  // game -> ent

    // Returns true on 200 + token populated.  Also parses the entitlements
    // map so the cheat menu can grey out non-licensed game cards.
    bool Login(const std::string& key) {
        std::string body = "{\"key\":\"" + key + "\"}";
        Response r = Request(endpoint, L"POST", L"/api/auth/login", body);
        if (r.status == 0) { lastError = "could not reach server"; return false; }
        if (!r.ok()) {
            auto err = JsonStringField(r.body, "error");
            lastError = err.value_or("http " + std::to_string(r.status));
            return false;
        }
        auto tok = JsonStringField(r.body, "token");
        if (!tok) { lastError = "server returned no token"; return false; }
        token = Utf8ToWide(*tok);
        entitlements.clear();
        if (auto block = JsonSubObject(r.body, "entitlements")) {
            // The block looks like:
            //   {"evrima":{"expires":N,"tier":"standard"},"bob":{"expires":...}}
            // We probe each game name we care about.
            const char* known[] = { "evrima", "bob", "fortnite" };
            for (auto* g : known) {
                auto sub = JsonSubObject(*block, g);
                if (!sub) continue;
                Entitlement e;
                e.expires = JsonIntField(*sub, "expires");
                auto t    = JsonStringField(*sub, "tier");
                if (t) e.tier = *t;
                entitlements[g] = e;
            }
        }
        lastError.clear();
        return true;
    }

    bool HasEntitlement(const std::string& game) const {
        return entitlements.find(game) != entitlements.end();
    }

    // Generic authenticated GET.  Returns the response body or "" on any
    // non-2xx / network error (lastError is set).
    std::string Get(const std::string& path) {
        if (token.empty()) { lastError = "not logged in"; return ""; }
        Response r = Request(endpoint, L"GET", Utf8ToWide(path), "", token);
        if (r.status == 0) { lastError = "could not reach server"; return ""; }
        if (!r.ok()) {
            auto err = JsonStringField(r.body, "error");
            lastError = err.value_or("http " + std::to_string(r.status));
            return "";
        }
        lastError.clear();
        return r.body;
    }

    // GET /api/offsets/<game>.  Returns raw JSON body or empty on failure.
    std::string FetchOffsets(const std::string& game) {
        if (token.empty()) { lastError = "not logged in"; return ""; }
        std::wstring path = L"/api/offsets/" + Utf8ToWide(game);
        Response r = Request(endpoint, L"GET", path, "", token);
        if (r.status == 0) { lastError = "could not reach server"; return ""; }
        if (!r.ok()) {
            auto err = JsonStringField(r.body, "error");
            lastError = err.value_or("http " + std::to_string(r.status));
            return "";
        }
        lastError.clear();
        return r.body;
    }

    // Quick health probe — doesn't require auth.  Returns true if /api/version
    // responded 2xx.
    bool Ping() {
        Response r = Request(endpoint, L"GET", L"/api/version", "");
        return r.ok();
    }
};

} // namespace xen_api
