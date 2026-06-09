#include "khora/reservoir/aqueduct.hpp"

#include <algorithm>
#include <cctype>

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
        {"Dracula",                    "literature", "https://www.gutenberg.org/cache/epub/345/pg345.txt"},
        {"The Picture of Dorian Gray", "literature", "https://www.gutenberg.org/cache/epub/174/pg174.txt"},
        {"Alices Adventures in Wonderland", "literature", "https://www.gutenberg.org/cache/epub/11/pg11.txt"},
        {"Great Expectations",         "literature", "https://www.gutenberg.org/cache/epub/1400/pg1400.txt"},
        {"Jane Eyre",                  "literature", "https://www.gutenberg.org/cache/epub/1260/pg1260.txt"},
        {"Wuthering Heights",          "literature", "https://www.gutenberg.org/cache/epub/768/pg768.txt"},
        {"The Count of Monte Cristo",  "literature", "https://www.gutenberg.org/cache/epub/1184/pg1184.txt"},
        {"Crime and Punishment",       "literature", "https://www.gutenberg.org/cache/epub/2554/pg2554.txt"},
        {"Don Quixote",                "literature", "https://www.gutenberg.org/cache/epub/996/pg996.txt"},
        // science fiction / fiction of ideas
        {"The Time Machine",           "scifi",      "https://www.gutenberg.org/cache/epub/35/pg35.txt"},
        {"The War of the Worlds",      "scifi",      "https://www.gutenberg.org/cache/epub/36/pg36.txt"},
        // philosophy
        {"The Republic",               "philosophy", "https://www.gutenberg.org/cache/epub/1497/pg1497.txt"},
        {"Thus Spake Zarathustra",     "philosophy", "https://www.gutenberg.org/cache/epub/1998/pg1998.txt"},
        {"Meditations",                "philosophy", "https://www.gutenberg.org/cache/epub/2680/pg2680.txt"},
        {"Beyond Good and Evil",       "philosophy", "https://www.gutenberg.org/cache/epub/4363/pg4363.txt"},
        {"Leviathan",                  "philosophy", "https://www.gutenberg.org/cache/epub/3207/pg3207.txt"},
        {"Tao Te Ching",               "philosophy", "https://www.gutenberg.org/cache/epub/216/pg216.txt"},
        // science
        {"Relativity the Special and General Theory", "science", "https://www.gutenberg.org/cache/epub/30155/pg30155.txt"},
        {"The Origin of Species",      "science",    "https://www.gutenberg.org/cache/epub/1228/pg1228.txt"},
        {"The Voyage of the Beagle",   "science",    "https://www.gutenberg.org/cache/epub/944/pg944.txt"},
        // mathematics — the productive spine: reasoning by quantity, form, proof.
        // (Notation-heavy texts like Calculus Made Easy / Boole are TeX-only on
        // Gutenberg, no plain text; the prose-and-problems works carry through.)
        {"Amusements in Mathematics",  "mathematics","https://www.gutenberg.org/cache/epub/16713/pg16713.txt"},
        {"The Canterbury Puzzles",     "mathematics","https://www.gutenberg.org/cache/epub/27635/pg27635.txt"},
        // logic & scientific method — inference, the seed of the machine
        {"The Principles of Science",  "logic",      "https://www.gutenberg.org/cache/epub/74864/pg74864.txt"},
        // physics — matter, motion, light, energy
        {"Treatise on Light",          "physics",    "https://www.gutenberg.org/cache/epub/14725/pg14725.txt"},
        {"Six Lectures on Light",      "physics",    "https://www.gutenberg.org/cache/epub/14000/pg14000.txt"},
        // chemistry — substance and reaction
        {"The Chemical History of a Candle", "chemistry", "https://www.gutenberg.org/cache/epub/14474/pg14474.txt"},
        // engineering — machinery, manufacture, the calculating engine's father
        {"On the Economy of Machinery and Manufactures", "engineering", "https://www.gutenberg.org/cache/epub/4238/pg4238.txt"},
        // science — the broad productive sweep: heavens, life, the outline of it all
        {"The Story of the Heavens",   "science",    "https://www.gutenberg.org/cache/epub/27378/pg27378.txt"},
        {"The Descent of Man",         "science",    "https://www.gutenberg.org/cache/epub/2300/pg2300.txt"},
        {"The Outline of Science",     "science",    "https://www.gutenberg.org/cache/epub/20417/pg20417.txt"},
        // strategy
        {"The Art of War",             "strategy",   "https://www.gutenberg.org/cache/epub/132/pg132.txt"},
        {"The Prince",                 "strategy",   "https://www.gutenberg.org/cache/epub/1232/pg1232.txt"},
        // history
        {"Common Sense",               "history",    "https://www.gutenberg.org/cache/epub/147/pg147.txt"},
        {"The History of the Peloponnesian War", "history", "https://www.gutenberg.org/cache/epub/7142/pg7142.txt"},
        // economics & politics
        {"The Wealth of Nations",      "economics",  "https://www.gutenberg.org/cache/epub/3300/pg3300.txt"},
        {"The Communist Manifesto",    "economics",  "https://www.gutenberg.org/cache/epub/61/pg61.txt"},
        // psychology
        {"Dream Psychology",           "psychology", "https://www.gutenberg.org/cache/epub/15489/pg15489.txt"},
        // poetry & drama
        {"Leaves of Grass",            "poetry",     "https://www.gutenberg.org/cache/epub/1322/pg1322.txt"},
        {"Paradise Lost",              "poetry",     "https://www.gutenberg.org/cache/epub/26/pg26.txt"},
        {"The Complete Works of William Shakespeare", "drama", "https://www.gutenberg.org/cache/epub/100/pg100.txt"},
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

ForageResult Aqueduct::forage_search(const std::string& topic) {
    ForageResult fr;
    fr.topic = topic;
    if (topic.empty()) { fr.error = "empty topic"; return fr; }

    // URL-encode the query (keep alnum, everything else -> '+').
    std::string q;
    for (unsigned char c : topic) {
        if (std::isalnum(c)) q += static_cast<char>(c);
        else                 q += '+';
    }
    // Search Project Gutenberg itself (the reachable host; the Gutendex API is
    // flaky/rate-limited). The results page is HTML listing books as
    // /ebooks/<id> with a <span class="title">; sort by downloads for substance.
    const std::string search_url =
        "https://www.gutenberg.org/ebooks/search/?query=" + q + "&sort_order=downloads";
    const HttpResult resp = http_get(search_url, 25000);
    if (!resp.ok) { fr.error = "search failed: " + resp.error; ++failures_; return fr; }
    const std::string& body = resp.body;

    // First /ebooks/<number> in the results, and the title that follows it.
    std::string id;
    for (std::size_t pos = body.find("/ebooks/"); pos != std::string::npos;
         pos = body.find("/ebooks/", pos + 8)) {
        std::size_t s = pos + 8;
        std::string num;
        while (s < body.size() && std::isdigit(static_cast<unsigned char>(body[s]))) num += body[s++];
        if (num.empty()) continue;
        id = num;
        if (const auto tp = body.find("class=\"title\">", pos); tp != std::string::npos) {
            const auto t1 = tp + 14;
            const auto t2 = body.find('<', t1);
            if (t2 != std::string::npos) fr.title = body.substr(t1, t2 - t1);
        }
        break;
    }
    if (id.empty()) { fr.error = "no Gutenberg result for '" + topic + "'"; return fr; }

    // Tidy the title (collapse whitespace).
    {
        std::string t;
        for (char c : fr.title) t += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        const auto a = t.find_first_not_of(' ');
        const auto b = t.find_last_not_of(' ');
        fr.title = (a == std::string::npos) ? topic : t.substr(a, b - a + 1);
    }
    if (fr.title.empty()) fr.title = topic;

    const std::string text_url =
        "https://www.gutenberg.org/cache/epub/" + id + "/pg" + id + ".txt";
    fr.source_url = text_url;

    if (reservoir_.has(fr.title)) { fr.ok = true; fr.error = "already held"; return fr; }

    const HttpResult text = http_get(text_url, 30000);
    if (!text.ok) { fr.error = "fetch failed: " + text.error; ++failures_; return fr; }

    const AdmitResult ar = reservoir_.admit(fr.title, topic, text_url, text.body);
    fr.ok = ar.ok;
    if (ar.ok) ++acquisitions_;
    else { fr.error = ar.error.empty() ? "admit failed" : ar.error; ++failures_; }
    return fr;
}

} // namespace khora::reservoir
