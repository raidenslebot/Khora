// AT WHAT CONTEXT LENGTH DOES ENGLISH ACTUALLY REPEAT, AND HOW DOES THAT SCALE?
//
// The vigilance diagnostic found that in 24,000 tokens of prose, 100.00% of
// 8-word windows occur exactly once. That explains everything upstream: the
// temporal memory keys on an 8-deep context, so its 93% burst rate was never an
// architecture failure or a threshold choice. There was no recurrence to detect.
// It also explains why a thirty-line trigram table beat it outright on the same
// books -- a trigram sits at one of the few depths where prose repeats at all.
//
// But that measurement cannot yet distinguish two very different claims:
//
//   (a) ENGLISH has no 8-gram recurrence at any practical scale, or
//   (b) TWENTY-FOUR THOUSAND TOKENS has no 8-gram recurrence.
//
// If (b), the conclusion is an artifact of a small sample and the whole line of
// reasoning collapses: Khora's real corpus is 2.67M observations, a hundred
// times larger, and recurrence at n=4 or 5 might be perfectly usable there.
//
// This measures the scaling directly. Pure counting, no memory module involved,
// so it is fast enough to run over the whole reservoir.
//
// What to expect, and why it matters either way. Heaps' law says distinct types
// grow as K*N^beta without bound, and for n-grams at n >= 5 the exponent
// approaches 1 -- distinct contexts grow essentially LINEARLY with the stream,
// so the recurring fraction stays near zero no matter how much text arrives.
// The industrial data point is Google Web 1T: a trillion tokens, and they still
// had to threshold at count >= 40 and still stored 1.3 billion 4-grams. If the
// recurring fraction at n=8 stays flat as this corpus grows a hundredfold, then
// deep context is a dead end on prose at ANY scale reachable here, and the work
// is backoff and variable order rather than more tissue.

#include "khora/reservoir/reservoir.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!cur.empty()) {
            if (cur.size() >= 2) out.push_back(cur);
            cur.clear();
        }
    }
    return out;
}

struct Row { std::size_t distinct = 0; double recurring = 0.0; std::size_t most = 0; };

Row count_ngrams(const std::vector<std::string>& s, std::size_t n, std::size_t upto) {
    std::unordered_map<std::string, std::uint32_t> c;
    c.reserve(upto * 2);
    const std::size_t end = std::min(upto, s.size());
    for (std::size_t i = 0; i + n <= end; ++i) {
        std::string key;
        key.reserve(n * 8);
        for (std::size_t k = 0; k < n; ++k) { key += s[i + k]; key += '\x1f'; }
        ++c[key];
    }
    Row r;
    r.distinct = c.size();
    std::size_t once = 0;
    for (const auto& kv : c) {
        if (kv.second == 1) ++once;
        if (kv.second > r.most) r.most = kv.second;
    }
    r.recurring = c.empty() ? 0.0
        : 1.0 - static_cast<double>(once) / static_cast<double>(c.size());
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "data/reservoir";

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("no tomes at %s\n", dir.c_str()); return 1; }

    std::printf("At what context length does English repeat, and does scale help?\n\n");
    std::printf("  reading the whole reservoir ...\n");
    std::vector<std::string> stream;
    std::size_t books = 0;
    for (const auto& t : cat) {
        auto text = res.read(t.title);
        if (!text || text->size() < 5000) continue;
        auto ws = tokenize(*text);
        stream.insert(stream.end(), ws.begin(), ws.end());
        ++books;
    }
    std::printf("  %zu books, %zu tokens\n\n", books, stream.size());
    if (stream.size() < 50000) { std::printf("  too little text\n"); return 1; }

    // RECURRING FRACTION against corpus size, per context length. The question
    // is whether the columns fall as the rows grow.
    std::printf("  RECURRING FRACTION of n-word contexts (share seen more than once)\n\n");
    std::printf("     tokens |    n=1 |    n=2 |    n=3 |    n=4 |    n=5 |    n=8\n");
    std::printf("  ----------+--------+--------+--------+--------+--------+--------\n");

    std::vector<std::size_t> sizes;
    for (std::size_t s = 24000; s < stream.size(); s *= 4) sizes.push_back(s);
    sizes.push_back(stream.size());

    for (const std::size_t upto : sizes) {
        std::printf("  %9zu |", upto);
        for (const std::size_t n : {1u, 2u, 3u, 4u, 5u, 8u}) {
            const Row r = count_ngrams(stream, n, upto);
            std::printf(" %5.2f%% |", 100.0 * r.recurring);
        }
        std::printf("\n");
    }

    // And the absolute counts at full size, because "distinct contexts grow
    // linearly with the stream" is the claim that decides whether ANY
    // fixed-memory scheme can hold them.
    std::printf("\n  AT FULL CORPUS: distinct contexts, and growth against tokens\n");
    std::printf("     n | distinct  | distinct/token | max repeats\n");
    std::printf("  -----+-----------+----------------+------------\n");
    for (const std::size_t n : {1u, 2u, 3u, 4u, 5u, 8u}) {
        const Row r = count_ngrams(stream, n, stream.size());
        std::printf("  %4zu | %9zu |     %6.3f     | %10zu\n",
                    n, r.distinct,
                    static_cast<double>(r.distinct) / static_cast<double>(stream.size()),
                    r.most);
    }

    std::printf("\n  HOW TO READ IT\n");
    std::printf("    If the n=8 column stays near zero as tokens grow 100-fold,\n");
    std::printf("    then deep context is a dead end on prose at any scale this\n");
    std::printf("    project can reach, and the 93%% burst was a property of the\n");
    std::printf("    DATA rather than of the architecture -- which retires the\n");
    std::printf("    population idea, the encoding change, and any threshold\n");
    std::printf("    tuning all at once.\n");
    std::printf("\n    A distinct/token ratio near 1.0 means every context is new:\n");
    std::printf("    no fixed-size memory can hold them, because they arrive as\n");
    std::printf("    fast as the tokens do. That is Heaps' law with an exponent\n");
    std::printf("    near one, and it is why n-gram systems threshold the tail\n");
    std::printf("    instead of storing it.\n");
    return 0;
}
