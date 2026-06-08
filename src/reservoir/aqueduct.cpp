#include "khora/reservoir/aqueduct.hpp"

#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <string>
#endif

namespace khora::reservoir {

namespace {

struct ParsedUrl {
    bool        https = true;
    std::wstring host;
    std::wstring path;
    int          port = 443;
    bool         ok = false;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl p;
    std::string rest = url;
    if (rest.rfind("https://", 0) == 0) { p.https = true;  p.port = 443; rest = rest.substr(8); }
    else if (rest.rfind("http://", 0) == 0) { p.https = false; p.port = 80;  rest = rest.substr(7); }
    else return p;

    const std::size_t slash = rest.find('/');
    std::string host = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "/"  : rest.substr(slash);

    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
        try { p.port = std::stoi(host.substr(colon + 1)); } catch (...) {}
        host = host.substr(0, colon);
    }
    p.host.assign(host.begin(), host.end());
    p.path.assign(path.begin(), path.end());
    p.ok = !p.host.empty();
    return p;
}

} // namespace

#ifdef _WIN32
HttpResult http_get(const std::string& url, int timeout_ms) {
    HttpResult r;
    const ParsedUrl p = parse_url(url);
    if (!p.ok) { r.error = "bad url: " + url; return r; }

    HINTERNET session = WinHttpOpen(L"Khora-Aqueduct/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { r.error = "WinHttpOpen failed"; return r; }
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    HINTERNET connect = WinHttpConnect(session, p.host.c_str(),
                                       static_cast<INTERNET_PORT>(p.port), 0);
    if (!connect) { WinHttpCloseHandle(session); r.error = "WinHttpConnect failed"; return r; }

    const DWORD flags = p.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", p.path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        r.error = "WinHttpOpenRequest failed"; return r;
    }

    BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (sent) sent = WinHttpReceiveResponse(request, nullptr);
    if (!sent) {
        const DWORD e = GetLastError();
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        r.error = "request failed (WinHTTP error " + std::to_string(e) + ")";
        return r;
    }

    // Status code
    {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        r.status = static_cast<long>(code);
    }

    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), avail, &read) || read == 0) break;
        chunk.resize(read);
        body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (r.status >= 200 && r.status < 300) { r.ok = true; r.body = std::move(body); }
    else r.error = "HTTP status " + std::to_string(r.status);
    return r;
}
#else
HttpResult http_get(const std::string&, int) {
    HttpResult r; r.error = "http_get only implemented for Win32"; return r;
}
#endif

const std::vector<Source>& seed_catalog() {
    static const std::vector<Source> seeds = {
        // literature
        {"Pride and Prejudice",        "literature", "https://www.gutenberg.org/cache/epub/1342/pg1342.txt"},
        {"Frankenstein",               "literature", "https://www.gutenberg.org/cache/epub/84/pg84.txt"},
        {"Moby Dick",                  "literature", "https://www.gutenberg.org/cache/epub/2701/pg2701.txt"},
        {"The Adventures of Sherlock Holmes", "literature", "https://www.gutenberg.org/cache/epub/1661/pg1661.txt"},
        {"A Tale of Two Cities",       "literature", "https://www.gutenberg.org/cache/epub/98/pg98.txt"},
        {"Grimms Fairy Tales",         "literature", "https://www.gutenberg.org/cache/epub/2591/pg2591.txt"},
        // philosophy
        {"The Republic",               "philosophy", "https://www.gutenberg.org/cache/epub/1497/pg1497.txt"},
        {"Thus Spake Zarathustra",     "philosophy", "https://www.gutenberg.org/cache/epub/1998/pg1998.txt"},
        {"Meditations",                "philosophy", "https://www.gutenberg.org/cache/epub/2680/pg2680.txt"},
        {"Beyond Good and Evil",       "philosophy", "https://www.gutenberg.org/cache/epub/4363/pg4363.txt"},
        // science / math
        {"Relativity the Special and General Theory", "science", "https://www.gutenberg.org/cache/epub/30155/pg30155.txt"},
        {"Calculus Made Easy",         "math",       "https://www.gutenberg.org/cache/epub/33283/pg33283.txt"},
        {"The Origin of Species",      "science",    "https://www.gutenberg.org/cache/epub/1228/pg1228.txt"},
        // strategy
        {"The Art of War",             "strategy",   "https://www.gutenberg.org/cache/epub/132/pg132.txt"},
        {"The Prince",                 "strategy",   "https://www.gutenberg.org/cache/epub/1232/pg1232.txt"},
    };
    return seeds;
}

AdmitResult Aqueduct::acquire(const Source& src) {
    const HttpResult http = http_get(src.url);
    if (!http.ok) {
        ++failures_;
        AdmitResult r; r.title = src.title;
        r.error = "fetch failed: " + http.error;
        return r;
    }
    const AdmitResult r = reservoir_.admit(src.title, src.topic, src.url, http.body);
    if (r.ok) ++acquisitions_; else ++failures_;
    return r;
}

std::optional<AdmitResult> Aqueduct::forage(const std::string& topic) {
    for (const auto& s : seed_catalog()) {
        if (!topic.empty() && s.topic != topic) continue;
        if (reservoir_.has(s.title)) continue;
        return acquire(s);
    }
    return std::nullopt;
}

} // namespace khora::reservoir
