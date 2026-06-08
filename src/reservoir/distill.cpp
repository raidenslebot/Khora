#include "khora/reservoir/distill.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace khora::reservoir {

namespace {

std::string to_lower_copy(std::string_view s) {
    std::string o(s);
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

// Find the end of the Project Gutenberg start banner; return offset just
// past the banner line, or npos if not found.
std::size_t find_gutenberg_start(const std::string& lower) {
    static const char* marks[] = {
        "*** start of the project gutenberg",
        "*** start of this project gutenberg",
        "***start of the project gutenberg",
    };
    for (const char* m : marks) {
        const std::size_t p = lower.find(m);
        if (p != std::string::npos) {
            const std::size_t eol = lower.find('\n', p);
            return (eol == std::string::npos) ? lower.size() : eol + 1;
        }
    }
    return std::string::npos;
}

std::size_t find_gutenberg_end(const std::string& lower) {
    static const char* marks[] = {
        "*** end of the project gutenberg",
        "*** end of this project gutenberg",
        "***end of the project gutenberg",
        "end of the project gutenberg ebook",
    };
    std::size_t best = std::string::npos;
    for (const char* m : marks) {
        const std::size_t p = lower.find(m);
        if (p != std::string::npos) best = std::min(best, p);
    }
    return best;
}

bool valid_utf8_lead(unsigned char c, int& extra) {
    if (c < 0x80) { extra = 0; return true; }
    if ((c & 0xE0) == 0xC0) { extra = 1; return true; }
    if ((c & 0xF0) == 0xE0) { extra = 2; return true; }
    if ((c & 0xF8) == 0xF0) { extra = 3; return true; }
    return false;
}

} // namespace

std::string distill(std::string_view raw, DistillStats* stats) {
    DistillStats st;
    st.input_bytes = raw.size();

    std::string text(raw);

    // 1. Trim Gutenberg license envelope.
    {
        const std::string lower = to_lower_copy(text);
        const std::size_t start = find_gutenberg_start(lower);
        std::size_t begin = 0, end = text.size();
        if (start != std::string::npos) { begin = start; st.stripped_gutenberg_header = true; }
        const std::size_t foot = find_gutenberg_end(lower);
        if (foot != std::string::npos && foot >= begin) { end = foot; st.stripped_gutenberg_footer = true; }
        text = text.substr(begin, end - begin);
    }

    // 2. Single pass: drop CR, strip HTML tags, decode a few entities,
    //    drop stray control bytes, keep valid UTF-8.
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (c == '\r') continue;  // CRLF -> LF

        if (c == '<') {
            // Skip an HTML tag if it looks like one (closes with '>').
            const std::size_t close = text.find('>', i);
            if (close != std::string::npos && close - i < 200) {
                i = close;
                ++st.html_tags_removed;
                continue;
            }
        }

        if (c == '&') {
            // Decode common entities.
            static const std::pair<const char*, char> ents[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                {"&quot;", '"'}, {"&#39;", '\''}, {"&apos;", '\''},
                {"&nbsp;", ' '},
            };
            bool matched = false;
            for (const auto& [name, ch] : ents) {
                const std::size_t len = std::char_traits<char>::length(name);
                if (text.compare(i, len, name) == 0) {
                    out.push_back(ch);
                    i += len - 1;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }

        if (c == '\n' || c == '\t') { out.push_back(static_cast<char>(c)); continue; }
        if (c >= 0x20 && c < 0x7F) { out.push_back(static_cast<char>(c)); continue; }

        if (c >= 0x80) {
            // Validate a UTF-8 sequence; keep it whole or drop the lead.
            int extra = 0;
            if (valid_utf8_lead(c, extra) && i + static_cast<std::size_t>(extra) < text.size()) {
                bool ok = true;
                for (int k = 1; k <= extra; ++k) {
                    if ((static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80) { ok = false; break; }
                }
                if (ok) {
                    out.push_back(static_cast<char>(c));
                    for (int k = 1; k <= extra; ++k) out.push_back(text[i + static_cast<std::size_t>(k)]);
                    i += static_cast<std::size_t>(extra);
                    continue;
                }
            }
            ++st.control_bytes_dropped;
            continue;  // invalid byte
        }

        // remaining control byte
        ++st.control_bytes_dropped;
    }

    // 3. Trim trailing whitespace per line and collapse 3+ blank lines to 2.
    std::string tidy;
    tidy.reserve(out.size());
    {
        std::size_t i = 0;
        int consecutive_newlines = 0;
        std::string line;
        auto flush_line = [&]() {
            // rstrip
            std::size_t e = line.size();
            while (e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
            line.resize(e);
        };
        for (; i < out.size(); ++i) {
            if (out[i] == '\n') {
                flush_line();
                if (line.empty()) {
                    ++consecutive_newlines;
                    if (consecutive_newlines <= 2) tidy.push_back('\n');
                } else {
                    tidy += line;
                    tidy.push_back('\n');
                    consecutive_newlines = 1;
                }
                line.clear();
            } else {
                line.push_back(out[i]);
            }
        }
        flush_line();
        tidy += line;
    }
    // Trim leading/trailing blank lines.
    {
        std::size_t b = tidy.find_first_not_of("\n ");
        std::size_t e = tidy.find_last_not_of("\n ");
        if (b == std::string::npos) tidy.clear();
        else tidy = tidy.substr(b, e - b + 1);
    }

    st.output_bytes = tidy.size();
    if (stats) *stats = st;
    return tidy;
}

} // namespace khora::reservoir
