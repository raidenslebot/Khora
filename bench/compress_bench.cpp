// HOW MANY BITS DOES KHORA NEED FOR A PAGE IT HAS NEVER READ?
//
// Compression is the least forgiving single number a model of language can be
// given, because the optimal code length for a message under a model is exactly
// the negative log probability that model assigns it. There is no partial
// credit, no threshold to pick, no metric to design in the same shape as the
// thing it measures. Either the bits come out smaller or they do not.
//
// dialect_bench measured perplexity on the same substrate and found the Plexus
// readout worse than a bigram. Perplexity is easy to nod at. Bits are not: they
// are directly comparable against tools that make no claim to understanding
// anything at all, and this file makes exactly that comparison.
//
// WHAT IS ACTUALLY BUILT HERE
//
//   A REAL CODER. A binary range coder (the LZMA carry-and-cache construction),
//   driven symbol-by-symbol by each model's next-token distribution through a
//   bisection over the cumulative distribution -- 16 binary decisions per token
//   for a ~35k vocabulary. It emits bytes. Those bytes are then DECODED back and
//   the reconstruction is compared to the input, byte for byte. Every number in
//   the table below comes from the length of a buffer that was decoded
//   successfully, not from a sum of -log2 p. A compressor that is never
//   round-tripped is a probability estimate wearing a bold name.
//
//   The encoder and the decoder are ONE FUNCTION (codec(), below) walking the
//   message with a direction flag, because two hand-written halves of a codec
//   drift, and a drifted decoder that still happens to reproduce the input is
//   the exact failure this check exists to catch.
//
//   LOSSLESS MEANS LOSSLESS, INCLUDING THE WORDS THE MODEL NEVER LEARNED. The
//   vocabulary is closed and built from the training split alone, so the test
//   text contains words with no symbol. dialect_bench could rewrite those to
//   <unk> and score them; a compressor cannot, because <unk> does not decode
//   back to "chrysoprase". So <unk> is an ESCAPE: coding it is followed by
//   coding the literal spelling through an order-0 byte model built on training
//   characters. Those bits are counted. Without this the round trip would be a
//   lie and the bit counts would be a measurement of a lossy channel.
//
// THE MODELS DRIVING IT
//   order-0        the training unigram (add-0.1). Word frequencies, no order.
//   order-2/3      interpolated Witten-Bell and interpolated Kneser-Ney. Proper
//                  smoothing, every one a normalised distribution with a
//                  positive floor on every symbol (no symbol can be uncodeable).
//   PLEXUS         the co-occurrence graph Khora actually runs on, read as a
//                  next-token distribution two ways -- raw co-occurrence and the
//                  PPMI that affinity() returns -- mixed with the unigram. Built
//                  inside this process from the TRAINING SPLIT ONLY; the shipped
//                  84k-node graph read the held-out books and using it would be
//                  leakage.
//   PLEXUS+3gram   the graph layered on top of the Kneser-Ney trigram, mixing
//                  weight tuned on dev. This is the only row that can answer
//                  "is the graph worth any bits at all on top of counting", and
//                  a tuned weight of 0.00 is a real answer.
//
// THE BASELINES ARE THE POINT
//   raw bytes; the order-0 byte entropy of the test text (which cheats -- it is
//   computed from the test text's own histogram); and REAL general-purpose
//   compressors shelled out to on this machine, each reported with the exact
//   command line used. gzip has never heard of English. If it wins, that is the
//   result, and it is a real one about the substrate.
//
// THE TRAINING-COST ASYMMETRY, WHICH IS NOT A FOOTNOTE
//   gzip is handed the test file and nothing else. Khora's models were fitted on
//   ~4M tokens and the decoder cannot run without them: the n-gram tables and the
//   vocabulary are NOT in the bitstream, and their size is printed below in
//   megabytes so the comparison can be made with eyes open. A model that needs a
//   corpus to beat a corpus-free tool has to beat it by enough to pay for the
//   corpus. To put a number on how much of gzip's disadvantage is the missing
//   corpus rather than the missing linguistics, each external tool is also run
//   PRIMED: compress(train ++ test) - compress(train), which is the cost of the
//   test text given the training text under that tool.
//
// WHAT THIS HARNESS CANNOT SEE
//   - Punctuation, capitalisation, and everything else the shared tokenizer
//     throws away. The message compressed here is the normalised token stream
//     (lowercase alnum words, one sentence per line). Every compressor in the
//     table sees THAT SAME FILE, so the comparison is fair, but these
//     bits-per-character are NOT comparable to published enwik8/text8 numbers.
//   - Whether a neural language model would beat all of this. It would, by a
//     lot. There is no transformer in this file. The comparison is Khora against
//     general-purpose compressors, not Khora against the field.
//   - Out-of-distribution text. Held-out books are held out, but the Reservoir is
//     public-domain prose of roughly one era.
//   - Whether the model tables could themselves be compressed and shipped. They
//     are reported at their in-memory size, which is an upper bound on a real
//     self-contained archive and a lower bound on nothing.
//   - The bits the range coder wastes on its own arithmetic. That is visible
//     rather than hidden: the ideal -sum log2 p is compared to the actual coded
//     size and the gap is printed per row. It can come out slightly NEGATIVE,
//     because clamping a branch probability into 16 bits rounds the very
//     smallest ones up and so undercharges for a handful of rare symbols. At the
//     magnitudes seen here (hundredths of a percent) it moves nothing, but a
//     large negative number would mean the coder was cheating and is worth
//     watching.

#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Id    = std::uint32_t;
using Clock = std::chrono::steady_clock;

constexpr Id kUnk = 0;   // slot 0: a held-out word the training split never kept
constexpr Id kEos = 1;   // slot 1: end of sentence, rendered as '\n'

// The escape alphabet: 256 byte values plus a terminator.
constexpr Id kChars = 257, kCharEnd = 256;

double secs(Clock::time_point a) {
    return std::chrono::duration<double>(Clock::now() - a).count();
}

// --- RANGE CODER --------------------------------------------------------------
//
// Binary, 32-bit range, 16-bit probabilities, LZMA's carry-through-a-cache-byte
// construction. Binary rather than multi-symbol on purpose: a multi-symbol coder
// divides its range by a fixed total, and with a 35k-word alphabet the total
// needed to give a rare word a nonzero slot is large enough that the division
// itself throws away a measurable fraction of a bit per token. A binary coder
// pays 16 well-conditioned decisions instead and the loss is negligible -- which
// is checkable, since the ideal -sum log2 p is reported next to the byte count.

constexpr std::uint32_t kTop   = 1u << 24;
constexpr std::uint32_t kPBits = 16;
constexpr std::uint32_t kPOne  = 1u << kPBits;

struct BitEnc {
    std::vector<std::uint8_t> out;
    std::uint64_t low  = 0;
    std::uint32_t range = 0xFFFFFFFFu;
    std::uint8_t  cache = 0;
    std::uint64_t cache_size = 1;   // the first shift emits one dummy byte; the
                                    // decoder's 5-byte priming reads it back

    void shift_low() {
        if (static_cast<std::uint32_t>(low >> 32) != 0
            || static_cast<std::uint32_t>(low) < 0xFF000000u) {
            const std::uint8_t carry = static_cast<std::uint8_t>(low >> 32);
            std::uint8_t t = cache;
            do { out.push_back(static_cast<std::uint8_t>(t + carry)); t = 0xFF; }
            while (--cache_size);
            cache = static_cast<std::uint8_t>(low >> 24);
        }
        ++cache_size;
        low = (low << 8) & 0xFFFFFFFFull;
    }
    // p0 is P(bit == 0) scaled to kPOne, clamped into [1, kPOne-1] so neither
    // branch can be assigned zero range -- a zero-width branch is an unencodable
    // symbol, and the clamp is what makes "no symbol has probability 0" true in
    // integer arithmetic and not just in the doubles.
    void bit(int b, std::uint32_t p0) {
        const std::uint32_t bound = (range >> kPBits) * p0;
        if (b == 0) range = bound; else { low += bound; range -= bound; }
        while (range < kTop) { range <<= 8; shift_low(); }
    }
    void flush() { for (int i = 0; i < 5; ++i) shift_low(); }
};

struct BitDec {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* e = nullptr;
    std::uint32_t range = 0xFFFFFFFFu, code = 0;
    std::uint8_t next() { return (p < e) ? *p++ : 0u; }
    void init() { for (int i = 0; i < 5; ++i) code = (code << 8) | next(); }
    int bit(std::uint32_t p0) {
        const std::uint32_t bound = (range >> kPBits) * p0;
        int b;
        if (code < bound) { range = bound; b = 0; }
        else { code -= bound; range -= bound; b = 1; }
        while (range < kTop) { code = (code << 8) | next(); range <<= 8; }
        return b;
    }
};

// Quantise a conditional branch probability for the coder. Shared by encoder and
// decoder so both sides quantise identically; if they did not, the stream would
// desynchronise and the round-trip check would fail loudly, which is the point.
inline std::uint32_t quant(double clo, double cmid, double chi) {
    const double span = chi - clo;
    double f = (span > 0.0) ? (cmid - clo) / span : 0.5;
    if (!(f > 0.0)) f = 0.0;
    if (!(f < 1.0)) f = 1.0;
    std::uint32_t p0 = static_cast<std::uint32_t>(f * static_cast<double>(kPOne) + 0.5);
    if (p0 < 1) p0 = 1;
    if (p0 > kPOne - 1) p0 = kPOne - 1;
    return p0;
}

// One symbol from an alphabet of n, coded as a bisection over the model's
// cumulative distribution: "is the symbol below the midpoint?", ceil(log2 n)
// times. cdf(w) must be the mass strictly below w, non-decreasing, with
// cdf(0) == 0. The decoder walks the identical bisection.
template <class Cdf>
void put_sym(BitEnc& e, Id w, Id n, const Cdf& cdf) {
    Id lo = 0, hi = n;
    double clo = 0.0, chi = cdf(n);
    while (hi - lo > 1) {
        const Id mid = lo + (hi - lo) / 2;
        const double cmid = cdf(mid);
        const int b = (w < mid) ? 0 : 1;
        e.bit(b, quant(clo, cmid, chi));
        if (b == 0) { hi = mid; chi = cmid; } else { lo = mid; clo = cmid; }
    }
}

template <class Cdf>
Id get_sym(BitDec& d, Id n, const Cdf& cdf) {
    Id lo = 0, hi = n;
    double clo = 0.0, chi = cdf(n);
    while (hi - lo > 1) {
        const Id mid = lo + (hi - lo) / 2;
        const double cmid = cdf(mid);
        if (d.bit(quant(clo, cmid, chi)) == 0) { hi = mid; chi = cmid; }
        else                                   { lo = mid; clo = cmid; }
    }
    return lo;
}

// --- SPARSE MIXTURE MODELS ----------------------------------------------------
//
// Every model here is the same shape:
//
//   P(w|h) = sum_k coef_k * A_k(w)  +  base_coef * Pbase(w)
//
// where each A_k is supported on a SHORT sorted list (the successors actually
// observed after that context, or the graph neighbours of the previous word) and
// Pbase is a dense distribution over the whole vocabulary with a positive floor.
// This shape is not a convenience: it is what makes the coder affordable. The
// cumulative distribution at any w is one binary search per layer plus one array
// read, so the 16 bisection steps cost ~50 lookups instead of a 35k-wide sweep.
//
// A layer's weight for its i-th symbol is coef * (pref[i+1] - pref[i] - sub).
// `sub` is the Kneser-Ney absolute discount: subtracting D from each of the first
// i counts is exactly subtracting D*i from the prefix sum, which is why the same
// prefix array serves both the discounted and undiscounted models.
struct Layer {
    const Id*     sym  = nullptr;   // ascending
    const double* pref = nullptr;   // len+1 exclusive prefix sums of the counts
    std::uint32_t len  = 0;
    double        coef = 0.0;
    double        sub  = 0.0;
};

struct Dist {
    Layer  l[3];
    int    n = 0;
    double base_coef = 1.0;
    const std::vector<double>* base = nullptr;   // dense CDF, size V+1, ends at 1

    double cdf(Id w) const {
        double s = base_coef * (*base)[w];
        for (int i = 0; i < n; ++i) {
            const Layer& L = l[i];
            const std::size_t k =
                static_cast<std::size_t>(std::lower_bound(L.sym, L.sym + L.len, w) - L.sym);
            s += L.coef * (L.pref[k] - L.sub * static_cast<double>(k));
        }
        return s;
    }
};

// A context -> successors table in CSR form. Row r owns sym[soff[r], soff[r+1])
// and the len+1 prefix sums at pref[soff[r] + r ...]. Flat arrays rather than a
// map of vectors because there are ~1.5M contexts and per-context vector headers
// alone would cost more than the counts.
struct Rec { std::uint64_t c; Id w; };

struct Table {
    std::unordered_map<std::uint64_t, std::uint32_t> idx;
    std::vector<std::uint32_t> soff{0};
    std::vector<Id>            sym;
    std::vector<double>        pref;
    int row(std::uint64_t k) const {
        const auto it = idx.find(k);
        return (it == idx.end()) ? -1 : static_cast<int>(it->second);
    }
    std::size_t rows() const { return soff.size() - 1; }
    std::size_t bytes() const {
        return sym.size() * sizeof(Id) + pref.size() * sizeof(double)
             + idx.size() * (sizeof(std::uint64_t) + sizeof(std::uint32_t) + 3 * sizeof(void*));
    }
};

Table build_table(std::vector<Rec>& rec) {
    std::sort(rec.begin(), rec.end(), [](const Rec& x, const Rec& y) {
        return x.c != y.c ? x.c < y.c : x.w < y.w; });
    Table T;
    T.idx.reserve(rec.size() / 2 + 16);
    std::size_t i = 0;
    while (i < rec.size()) {
        const std::uint64_t c = rec[i].c;
        T.idx.emplace(c, static_cast<std::uint32_t>(T.soff.size() - 1));
        double acc = 0.0;
        T.pref.push_back(0.0);
        std::size_t j = i;
        while (j < rec.size() && rec[j].c == c) {
            const Id w = rec[j].w;
            std::size_t k = j;
            while (k < rec.size() && rec[k].c == c && rec[k].w == w) ++k;
            acc += static_cast<double>(k - j);
            T.sym.push_back(w);
            T.pref.push_back(acc);
            j = k;
        }
        T.soff.push_back(static_cast<std::uint32_t>(T.sym.size()));
        i = j;
    }
    return T;
}

// A Plexus row re-keyed into model ids: sorted neighbours with prefix sums for
// two readouts of the same edges -- raw co-occurrence count, and the PPMI that
// affinity() returns and that utter() steers by.
struct PRow {
    std::vector<Id>     nb;
    std::vector<double> pref[2];   // nb.size()+1 each
    double              tot[2] = {0.0, 0.0};
};

struct Model {
    std::string name;
    int    order = 0;      // 0 unigram only, 2 bigram, 3 trigram
    bool   kn    = false;  // Kneser-Ney (else Witten-Bell)
    double D     = 0.75;
    int    plex  = -1;     // -1 none, 0 raw co-occurrence, 1 PPMI
    double lam   = 0.0;    // Plexus mixing weight
};

struct World {
    Id V = 0, Bos = 0;
    const Table* T3  = nullptr;   // (a,b) -> w, raw counts
    const Table* T2  = nullptr;   // b -> w, raw counts
    const Table* T2c = nullptr;   // b -> w, CONTINUATION counts (distinct a), for KN
    const std::vector<double>* uni_cdf  = nullptr;
    const std::vector<double>* cont_cdf = nullptr;
    const std::vector<PRow>*   prow     = nullptr;

    static void add_ngram(Dist& d, double& run, const Table& T,
                          std::uint64_t key, const Model& m) {
        const int r = T.row(key);
        if (r < 0) return;                       // unseen context: all mass backs off
        const std::uint32_t s0 = T.soff[static_cast<std::size_t>(r)];
        const std::uint32_t s1 = T.soff[static_cast<std::size_t>(r) + 1];
        const double len = static_cast<double>(s1 - s0);
        Layer& L = d.l[d.n++];
        L.sym  = T.sym.data() + s0;
        L.pref = T.pref.data() + s0 + static_cast<std::size_t>(r);
        L.len  = s1 - s0;
        const double tot = L.pref[L.len];
        if (m.kn) {
            // mass on seen words = (tot - D*types)/tot, backoff = D*types/tot.
            // Every stored count is >= 1 > D, so no discounted weight is negative
            // and the layer stays monotone.
            L.sub  = m.D;
            L.coef = run / tot;
            run   *= m.D * len / tot;
        } else {
            // Witten-Bell: a history seen in many different ways trusts itself.
            L.sub  = 0.0;
            L.coef = run / (tot + len);
            run   *= len / (tot + len);
        }
    }

    Dist make(const Model& m, Id a, Id b) const {
        Dist d;
        double run = 1.0;
        if (m.plex >= 0 && m.lam > 0.0 && b != Bos) {
            const PRow& r = (*prow)[b];
            if (r.tot[m.plex] > 0.0) {
                Layer& L = d.l[d.n++];
                L.sym  = r.nb.data();
                L.pref = r.pref[m.plex].data();
                L.len  = static_cast<std::uint32_t>(r.nb.size());
                L.sub  = 0.0;
                L.coef = run * m.lam / r.tot[m.plex];
                run   *= (1.0 - m.lam);
            }
        }
        if (m.order >= 3)
            add_ngram(d, run, *T3, (static_cast<std::uint64_t>(a) << 32) | b, m);
        if (m.order >= 2)
            add_ngram(d, run, (m.kn && m.order >= 3) ? *T2c : *T2,
                      static_cast<std::uint64_t>(b), m);
        d.base_coef = run;
        // Kneser-Ney's lowest order is the CONTINUATION distribution -- how many
        // distinct words precede w, not how often w occurs. That is the whole
        // idea: "francisco" is frequent but follows only "san".
        d.base = m.kn ? cont_cdf : uni_cdf;
        return d;
    }
};

// --- THE MESSAGE ---------------------------------------------------------------
//
// The thing being compressed. Token ids, with the literal spelling of every
// out-of-vocabulary word kept alongside in order of occurrence, because the
// decoder has to put them back.
struct Msg {
    std::vector<Id>          sym;
    std::vector<std::string> oov;
    std::size_t              words = 0;   // word tokens, </s> excluded
    std::string              text;        // the rendered bytes every compressor sees
};

std::string render(const std::vector<Id>& sym, const std::vector<std::string>& oov,
                   const std::vector<std::string>& vword) {
    std::string s;
    std::size_t oi = 0;
    bool first = true;
    for (const Id w : sym) {
        if (w == kEos) { s += '\n'; first = true; continue; }
        if (!first) s += ' ';
        first = false;
        s += (w == kUnk) ? oov[oi++] : vword[w];
    }
    return s;
}

// --- CODEC ---------------------------------------------------------------------
//
// ENCODE and DECODE are one walk with a flag. Context advance, layer resolution,
// escape handling: identical code on both passes, so the decoder cannot silently
// diverge from the encoder's model. `ideal` accumulates -sum log2 p, the length
// the coder would reach with infinite precision; the gap to 8*buf.size() is the
// coder's own overhead and is reported rather than assumed away.
struct CodecResult {
    std::vector<std::uint8_t> buf;
    double                    ideal = 0.0;   // bits, everything
    double                    esc   = 0.0;   // bits, the literal spellings only
    std::vector<Id>           sym;
    std::vector<std::string>  oov;
};

void codec(bool enc, const World& W, const Model& m, const std::vector<double>& char_cdf,
           const Msg& in, std::size_t n_sym, CodecResult& R) {
    BitEnc e;
    BitDec d;
    if (!enc) { d.p = R.buf.data(); d.e = R.buf.data() + R.buf.size(); d.init(); }
    const auto ccdf = [&](Id c) { return char_cdf[c]; };

    Id a = W.Bos, b = W.Bos;
    std::size_t oi = 0;
    for (std::size_t i = 0; i < n_sym; ++i) {
        const Dist ds = W.make(m, a, b);
        const auto cdf = [&](Id w) { return ds.cdf(w); };
        Id w;
        if (enc) {
            w = in.sym[i];
            R.ideal += -std::log2(std::max(cdf(w + 1) - cdf(w), 1e-300));
            put_sym(e, w, W.V, cdf);
        } else {
            w = get_sym(d, W.V, cdf);
            R.sym.push_back(w);
        }
        if (w == kUnk) {
            // Escape. The model has no symbol for this word, so its letters go
            // through an order-0 byte model fitted on TRAINING characters only.
            std::string s;
            if (enc) {
                s = in.oov[oi++];   // charged for below, in R.esc
                for (const unsigned char ch : s) {
                    R.esc += -std::log2(std::max(ccdf(ch + 1u) - ccdf(ch), 1e-300));
                    put_sym(e, ch, kChars, ccdf);
                }
                R.esc += -std::log2(std::max(ccdf(kCharEnd + 1) - ccdf(kCharEnd), 1e-300));
                put_sym(e, kCharEnd, kChars, ccdf);
            } else {
                for (;;) {
                    const Id c = get_sym(d, kChars, ccdf);
                    if (c == kCharEnd) break;
                    s.push_back(static_cast<char>(c));
                    if (s.size() > 128) break;   // desync guard; a trip shows as a
                                                 // round-trip mismatch, not silence
                }
                R.oov.push_back(s);
            }
        }
        if (w == kEos) { a = b = W.Bos; } else { a = b; b = w; }
    }
    R.ideal += R.esc;
    if (enc) { e.flush(); R.buf = std::move(e.out); }
}

// Ideal cost only, no coding. Used for tuning on dev, so the tuned quantity is
// the same quantity that later gets coded.
double xent_bits(const World& W, const Model& m, const Msg& msg) {
    double bits = 0.0;
    Id a = W.Bos, b = W.Bos;
    for (const Id w : msg.sym) {
        const Dist ds = W.make(m, a, b);
        bits += -std::log2(std::max(ds.cdf(w + 1) - ds.cdf(w), 1e-300));
        if (w == kEos) { a = b = W.Bos; } else { a = b; b = w; }
    }
    return bits;
}

// --- EXTERNAL COMPRESSORS ------------------------------------------------------
//
// Shelled out to, not reimplemented. A hand-rolled LZ77 would be weaker than a
// tuned production coder and the comparison would be worth nothing.
std::string subst(std::string t, const char* key, const std::string& val) {
    for (std::size_t p; (p = t.find(key)) != std::string::npos; )
        t.replace(p, std::strlen(key), val);
    return t;
}

long long run_ext(const std::string& tpl, const std::string& in, const std::string& out) {
    std::error_code ec;
    std::filesystem::remove(out, ec);
    const std::string cmd = subst(subst(tpl, "%IN%", in), "%OUT%", out);
    // cmd.exe strips the outermost pair of quotes from its command string, so a
    // command that begins with a quoted program path needs one more pair around
    // the whole thing or the redirect ends up inside the program name.
    const std::string wrapped = "\"" + cmd + " 2>nul\"";
    if (std::system(wrapped.c_str()) != 0) { /* tool may still have written output */ }
    const auto sz = std::filesystem::file_size(out, ec);
    return ec ? -1 : static_cast<long long>(sz);
}

struct Ext { std::string name, tpl; long long test = -1, primed = -1; };

} // namespace

int main(int argc, char** argv) {
    const std::string  dir       = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t  train_cap = (argc > 2) ? std::stoul(argv[2]) : 4000000;
    const std::size_t  held_cap  = (argc > 3) ? std::stoul(argv[3]) : 300000;
    const std::uint32_t min_count = (argc > 4) ? static_cast<std::uint32_t>(std::stoul(argv[4])) : 3;
    const std::size_t  prime_cap = (argc > 5) ? std::stoul(argv[5]) : 4000000;   // bytes

    std::printf("HOW MANY BITS DOES KHORA NEED FOR A PAGE IT HAS NEVER READ?\n");
    std::printf("==========================================================\n\n");

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    auto cat = res.catalog();
    if (cat.empty()) { std::printf("  empty catalog at %s\n", dir.c_str()); return 0; }
    std::sort(cat.begin(), cat.end(),
              [](const khora::reservoir::Tome& a, const khora::reservoir::Tome& b) {
                  return a.title < b.title; });

    // --- SPLIT BY BOOK, exactly as dialect_bench does, so the two are comparable.
    // Splitting inside a book leaks its vocabulary and its proper nouns into the
    // counts, and the resulting bit count measures the split, not the model.
    using Text = std::vector<std::vector<std::string>>;
    const auto t_read = Clock::now();
    Text tr_text, dev_text, te_text;
    std::size_t tr_books = 0, dev_books = 0, te_books = 0;
    std::size_t tr_all = 0, dev_all = 0, te_all = 0;
    {
        std::size_t i = 0;
        for (const auto& t : cat) {
            auto text = res.read(t.title);
            if (!text || text->size() < 40000) continue;
            const std::size_t bucket = i % 5;   // 0 test, 1 dev, 2..4 train
            ++i;
            Text* dst        = (bucket == 0) ? &te_text  : (bucket == 1) ? &dev_text  : &tr_text;
            std::size_t* nb  = (bucket == 0) ? &te_books : (bucket == 1) ? &dev_books : &tr_books;
            std::size_t* nt  = (bucket == 0) ? &te_all   : (bucket == 1) ? &dev_all   : &tr_all;
            ++(*nb);
            for (auto& sent : khora::lexicon::tokenize_sentences(*text)) {
                if (sent.empty()) continue;
                *nt += sent.size();
                dst->push_back(std::move(sent));
            }
        }
    }
    std::printf("corpus  %zu train books (%zu tokens), %zu dev (%zu), %zu test (%zu)"
                "   [read+tokenize %.1fs]\n",
                tr_books, tr_all, dev_books, dev_all, te_books, te_all, secs(t_read));

    // Cap by dropping whole sentences at a uniform rate rather than truncating the
    // book list, which would hand the model a handful of authors. No n-gram here
    // crosses a sentence boundary, so dropping sentences costs only corpus size.
    const auto subsample = [](Text& v, std::size_t have, std::size_t cap, std::uint64_t seed) {
        if (have <= cap) return;
        std::mt19937_64 rng(seed);
        const double keep = static_cast<double>(cap) / static_cast<double>(have);
        Text out;
        out.reserve(static_cast<std::size_t>(static_cast<double>(v.size()) * keep) + 16);
        for (auto& s : v)
            if (std::uniform_real_distribution<double>(0.0, 1.0)(rng) < keep)
                out.push_back(std::move(s));
        v.swap(out);
    };
    subsample(tr_text,  tr_all,  train_cap, 20260825ull);
    subsample(dev_text, dev_all, held_cap,  20260826ull);
    subsample(te_text,  te_all,  held_cap,  20260827ull);

    // --- VOCABULARY, closed, from the training split alone -----------------------
    std::unordered_map<std::string, std::uint64_t> raw;
    for (const auto& s : tr_text) for (const auto& w : s) ++raw[w];
    std::unordered_map<std::string, Id> vid;
    std::vector<std::string> vword = {"<unk>", "</s>"};
    vid.emplace("<unk>", kUnk);
    vid.emplace("</s>", kEos);
    for (const auto& [w, c] : raw)
        if (c >= min_count) { vid.emplace(w, static_cast<Id>(vword.size())); vword.push_back(w); }
    const Id V   = static_cast<Id>(vword.size());
    const Id Bos = V;   // context-only, deliberately outside V, never a target
    std::printf("vocab   %zu training types, %u kept at count>=%u"
                " (V includes <unk> and </s>; <s> is context-only and not in V)\n",
                raw.size(), V, min_count);

    // --- TRAINING STREAM, plus the character counts the escape model needs -------
    std::vector<Id> tr;
    std::vector<double> char_cnt(kChars, 1.0);   // add-1: no byte value is uncodeable
    {
        std::size_t n = 0;
        for (const auto& s : tr_text) n += s.size() + 1;
        tr.reserve(n);
        for (const auto& sent : tr_text) {
            for (const auto& w : sent) {
                const auto it = vid.find(w);
                tr.push_back(it == vid.end() ? kUnk : it->second);
                for (const unsigned char ch : w) char_cnt[ch] += 1.0;
                char_cnt[kCharEnd] += 1.0;
            }
            tr.push_back(kEos);
        }
    }
    Text().swap(tr_text);   // the training strings are done; only ids are needed now

    // --- COUNT TABLES ------------------------------------------------------------
    const auto t_count = Clock::now();
    Table T3, T2, T2c;
    std::vector<double> cont1(V, 0.0);   // N1+(. w): distinct words that precede w
    {
        const auto walk = [&](auto&& emit) {
            Id a = Bos, b = Bos;
            for (const Id w : tr) {
                emit(a, b, w);
                if (w == kEos) { a = b = Bos; } else { a = b; b = w; }
            }
        };
        std::vector<Rec> rec;
        rec.reserve(tr.size());
        walk([&](Id a, Id b, Id w) {
            rec.push_back({(static_cast<std::uint64_t>(a) << 32) | b, w}); });
        T3 = build_table(rec);
        rec.clear(); rec.shrink_to_fit();

        rec.reserve(tr.size());
        walk([&](Id, Id b, Id w) { rec.push_back({static_cast<std::uint64_t>(b), w}); });
        T2 = build_table(rec);
        rec.clear(); rec.shrink_to_fit();

        // Continuation counts for Kneser-Ney's middle order: one record per
        // DISTINCT trigram, so the aggregated count of (b,w) is the number of
        // distinct a that ever preceded it.
        rec.reserve(T3.sym.size());
        for (const auto& [key, r] : T3.idx) {
            const Id b = static_cast<Id>(key & 0xffffffffu);
            for (std::uint32_t j = T3.soff[r]; j < T3.soff[r + 1]; ++j)
                rec.push_back({static_cast<std::uint64_t>(b), T3.sym[j]});
        }
        T2c = build_table(rec);
        rec.clear(); rec.shrink_to_fit();

        // ...and for its lowest order, straight off the distinct bigrams.
        for (std::size_t r = 0; r < T2.rows(); ++r)
            for (std::uint32_t j = T2.soff[r]; j < T2.soff[r + 1]; ++j)
                cont1[T2.sym[j]] += 1.0;
    }
    std::printf("counts  trigram contexts %zu / %zu entries | bigram %zu / %zu"
                " | KN-continuation bigram %zu / %zu   [%.1fs]\n",
                T3.rows(), T3.sym.size(), T2.rows(), T2.sym.size(),
                T2c.rows(), T2c.sym.size(), secs(t_count));

    // --- BASE DISTRIBUTIONS -------------------------------------------------------
    // Floored with a 1e-6 uniform so no symbol anywhere has probability zero. A
    // zero-probability symbol is not a bad prediction, it is an uncodeable one.
    const auto make_cdf = [&](const std::vector<double>& mass) {
        double s = 0.0;
        for (const double m : mass) s += m;
        std::vector<double> c(V + 1, 0.0);
        const double eps = 1e-6;
        double acc = 0.0;
        for (Id w = 0; w < V; ++w) {
            c[w] = acc;
            acc += (1.0 - eps) * mass[w] / s + eps / static_cast<double>(V);
        }
        c[V] = 1.0;
        return c;
    };
    std::vector<double> uni_mass(V, 0.1), cont_mass(V, 0.1);
    {
        for (const Id w : tr) uni_mass[w] += 1.0;
        for (Id w = 0; w < V; ++w) cont_mass[w] += cont1[w];
    }
    const std::vector<double> uni_cdf  = make_cdf(uni_mass);
    const std::vector<double> cont_cdf = make_cdf(cont_mass);
    std::vector<double> char_cdf(kChars + 1, 0.0);
    {
        double s = 0.0;
        for (const double m : char_cnt) s += m;
        double acc = 0.0;
        for (Id c = 0; c < kChars; ++c) { char_cdf[c] = acc; acc += char_cnt[c] / s; }
        char_cdf[kChars] = 1.0;
    }

    // --- THE PLEXUS, BUILT ON THE TRAINING SPLIT ONLY -----------------------------
    // The shipped 84k-node graph read every book in the catalogue including the
    // held-out ones, so coding with it would be coding a message the model has
    // already seen. This one is rebuilt here at the shipped max_degree of 160.
    std::vector<PRow> prow(V + 1);
    std::size_t plex_nodes = 0, plex_deg = 0;
    unsigned long long plex_edges = 0;
    {
        const auto t0 = Clock::now();
        khora::plexus::Plexus plex;
        plex.set_max_degree(160);
        std::vector<std::string> buf;
        for (const Id w : tr) {
            buf.push_back(vword[w]);
            // observe() recomputes an O(V) partition function per call, so it is
            // fed in large blocks; per-sentence calls alone would dominate the run.
            if (buf.size() >= 400000) { plex.observe(buf, 3); buf.clear(); }
        }
        if (!buf.empty()) plex.observe(buf, 3);
        plex.prune_all();
        plex_nodes = plex.vocabulary_size();
        plex_edges = plex.edge_count();
        plex_deg   = plex.max_degree();

        std::vector<Id> p2lm(plex.vocabulary_size(), V);
        for (std::size_t p = 0; p < plex.vocabulary_size(); ++p) {
            const auto it = vid.find(std::string(plex.node_name(p)));
            if (it != vid.end()) p2lm[p] = it->second;
        }
        for (std::size_t p = 0; p < plex.vocabulary_size(); ++p) {
            const Id lm = p2lm[p];
            if (lm >= V) continue;
            const std::string a(plex.node_name(p));
            std::vector<std::pair<Id, std::pair<double, double>>> es;
            for (const auto& [nb, c] : plex.neighbours(p)) {
                const Id lnb = p2lm[nb];
                if (lnb >= V) continue;
                es.emplace_back(lnb, std::make_pair(static_cast<double>(c),
                                                    plex.affinity(a, plex.node_name(nb))));
            }
            std::sort(es.begin(), es.end(),
                      [](const auto& x, const auto& y) { return x.first < y.first; });
            PRow& r = prow[lm];
            r.nb.reserve(es.size());
            for (int k = 0; k < 2; ++k) r.pref[k].reserve(es.size() + 1);
            for (int k = 0; k < 2; ++k) r.pref[k].push_back(0.0);
            for (const auto& [w, ww] : es) {
                r.nb.push_back(w);
                r.tot[0] += ww.first;  r.pref[0].push_back(r.tot[0]);
                r.tot[1] += ww.second; r.pref[1].push_back(r.tot[1]);
            }
        }
        std::printf("plexus  %zu nodes, %llu edges, max_degree %zu, rebuilt from the"
                    " training split   [%.1fs]\n",
                    plex_nodes, plex_edges, plex_deg, secs(t0));
    }

    // --- THE HELD-OUT MESSAGES -----------------------------------------------------
    const auto build_msg = [&](const Text& txt) {
        Msg m;
        for (const auto& sent : txt) {
            for (const auto& w : sent) {
                const auto it = vid.find(w);
                if (it == vid.end()) { m.sym.push_back(kUnk); m.oov.push_back(w); }
                else                  m.sym.push_back(it->second);
                ++m.words;
            }
            m.sym.push_back(kEos);
        }
        m.text = render(m.sym, m.oov, vword);
        return m;
    };
    const Msg DEV = build_msg(dev_text);
    const Msg TE  = build_msg(te_text);
    std::printf("message test %zu word tokens + %zu sentence ends = %zu symbols,"
                " %zu bytes rendered, %.2f%% OOV (escaped as literal spellings, not dropped)\n",
                TE.words, TE.sym.size() - TE.words, TE.sym.size(), TE.text.size(),
                100.0 * static_cast<double>(TE.oov.size()) / static_cast<double>(TE.words));
    std::printf("        dev %zu word tokens (tuning only; no number in the final table"
                " is its own tuning set)\n", DEV.words);

    World W;
    W.V = V; W.Bos = Bos;
    W.T3 = &T3; W.T2 = &T2; W.T2c = &T2c;
    W.uni_cdf = &uni_cdf; W.cont_cdf = &cont_cdf; W.prow = &prow;

    // --- WRITE THE FILES EVERY COMPRESSOR SEES ---------------------------------------
    // Byte-identical input for the arithmetic coder and for gzip. If they saw
    // different files the comparison would be worthless.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path();
    const std::string f_test  = (tmp / "khora_compress_test.txt").string();
    const std::string f_train = (tmp / "khora_compress_train.txt").string();
    const std::string f_both  = (tmp / "khora_compress_both.txt").string();
    {
        std::FILE* f = std::fopen(f_test.c_str(), "wb");
        if (f) { std::fwrite(TE.text.data(), 1, TE.text.size(), f); std::fclose(f); }
    }
    // A prefix of the TRAINING text, in the same normalised form, so an external
    // tool can be given the same corpus advantage the n-gram models had.
    std::size_t prime_bytes = 0;
    double train_text_bytes = 0.0;   // the whole training text, rendered, for the
                                     // primed-baseline caveat below
    for (const Id w : tr) train_text_bytes += static_cast<double>(vword[w].size() + 1);
    {
        std::string prime;
        prime.reserve(prime_cap + 64);
        bool first = true;
        for (const Id w : tr) {
            if (w == kEos) { prime += '\n'; first = true; }
            else { if (!first) prime += ' '; first = false; prime += vword[w]; }
            if (prime.size() >= prime_cap) break;
        }
        prime_bytes = prime.size();
        std::FILE* f = std::fopen(f_train.c_str(), "wb");
        if (f) { std::fwrite(prime.data(), 1, prime.size(), f); std::fclose(f); }
        f = std::fopen(f_both.c_str(), "wb");
        if (f) {
            std::fwrite(prime.data(), 1, prime.size(), f);
            std::fwrite(TE.text.data(), 1, TE.text.size(), f);
            std::fclose(f);
        }
    }

    // --- TUNE ON DEV -------------------------------------------------------------
    std::printf("\n--- tuned on DEV (bits/token, ideal), reported on TEST ---\n");
    double bestD = 0.75;
    {
        std::printf("  kneser-ney D (n=3) ");
        double best = 1e300;
        for (const double D : {0.5, 0.7, 0.85, 0.95, 0.99}) {
            const double b = xent_bits(W, {"", 3, true, D, -1, 0.0}, DEV)
                           / static_cast<double>(DEV.words);
            std::printf("  D=%.2f %.4f", D, b);
            if (b < best) { best = b; bestD = D; }
        }
        std::printf("   -> D=%.2f\n", bestD);
    }
    int    px_ro = 0;    double px_lam = 0.0;   // Plexus alone, over the unigram
    int    pn_ro = 0;    double pn_lam = 0.0;   // Plexus layered on the KN trigram
    {
        static const char* ro_name[2] = {"raw co-occurrence", "PPMI (affinity())"};
        double best = 1e300;
        for (int ro = 0; ro < 2; ++ro) {
            std::printf("  plexus %-18s over unigram ", ro_name[ro]);
            for (const double l : {0.0, 0.15, 0.3, 0.45, 0.6, 0.8}) {
                const double b = xent_bits(W, {"", 0, false, 0.0, ro, l}, DEV)
                               / static_cast<double>(DEV.words);
                std::printf(" l=%.2f %.3f", l, b);
                if (b < best) { best = b; px_ro = ro; px_lam = l; }
            }
            std::printf("\n");
        }
        std::printf("    -> %s, lambda=%.2f  (lambda=0 IS the unigram, exactly)\n",
                    ro_name[px_ro], px_lam);
        best = 1e300;
        for (int ro = 0; ro < 2; ++ro) {
            std::printf("  plexus %-18s over KN n=3  ", ro_name[ro]);
            for (const double l : {0.0, 0.05, 0.15, 0.3, 0.5}) {
                const double b = xent_bits(W, {"", 3, true, bestD, ro, l}, DEV)
                               / static_cast<double>(DEV.words);
                std::printf(" l=%.2f %.3f", l, b);
                if (b < best) { best = b; pn_ro = ro; pn_lam = l; }
            }
            std::printf("\n");
        }
        std::printf("    -> %s, lambda=%.2f  (lambda=0 means the graph is worth"
                    " nothing on top of counting)\n", ro_name[pn_ro], pn_lam);
    }

    // --- ENCODE, DECODE, VERIFY ----------------------------------------------------
    struct Row { std::string name; std::size_t bytes; double ideal, esc; bool ok; double sec; };
    std::vector<Row> rows;
    std::vector<Model> models = {
        {"order-0 unigram (add-0.1)",                0, false, 0.0,    -1, 0.0},
        {"order-2 Witten-Bell",                      2, false, 0.0,    -1, 0.0},
        {"order-3 Witten-Bell",                      3, false, 0.0,    -1, 0.0},
        {"order-2 Kneser-Ney",                       2, true,  bestD,  -1, 0.0},
        {"order-3 Kneser-Ney",                       3, true,  bestD,  -1, 0.0},
    };
    {
        char nm[96];
        std::snprintf(nm, sizeof nm, "PLEXUS %s over unigram (l=%.2f)",
                      px_ro ? "PPMI" : "cooc", px_lam);
        models.push_back({nm, 0, false, 0.0, px_ro, px_lam});
        std::snprintf(nm, sizeof nm, "PLEXUS %s + order-3 KN (l=%.2f)",
                      pn_ro ? "PPMI" : "cooc", pn_lam);
        models.push_back({nm, 3, true, bestD, pn_ro, pn_lam});
    }

    // 8 bytes of header: the symbol count, so the decoder knows when to stop.
    // Counted against every arithmetic-coded row.
    constexpr std::size_t kHdr = 8;
    std::printf("\n--- round trip: encode, decode, compare to the input byte for byte ---\n");
    for (const auto& m : models) {
        const auto t0 = Clock::now();
        CodecResult enc;
        codec(true, W, m, char_cdf, TE, TE.sym.size(), enc);
        CodecResult dec;
        dec.buf = enc.buf;
        codec(false, W, m, char_cdf, TE, TE.sym.size(), dec);
        const std::string back = render(dec.sym, dec.oov, vword);
        const bool ok = (dec.sym == TE.sym) && (dec.oov == TE.oov) && (back == TE.text);
        std::printf("  %-40s %9zu bytes -> decoded %zu symbols, %zu bytes : %s\n",
                    m.name.c_str(), enc.buf.size() + kHdr, dec.sym.size(), back.size(),
                    ok ? "IDENTICAL" : "*** MISMATCH ***");
        rows.push_back({m.name, enc.buf.size() + kHdr, enc.ideal, enc.esc, ok, secs(t0)});
    }
    // NEGATIVE CONTROL. A verification that cannot fail is not a verification.
    // Flip one byte in the middle of one stream and confirm the decode stops
    // matching; if it still matched, every IDENTICAL above would be evidence of
    // nothing at all.
    {
        CodecResult enc;
        codec(true, W, models[0], char_cdf, TE, TE.sym.size(), enc);
        CodecResult dec;
        dec.buf = enc.buf;
        dec.buf[dec.buf.size() / 2] ^= 0x5Au;
        codec(false, W, models[0], char_cdf, TE, TE.sym.size(), dec);
        const bool still = (dec.sym == TE.sym)
                        && (render(dec.sym, dec.oov, vword) == TE.text);
        std::printf("  [negative control] one byte flipped mid-stream -> %s\n",
                    still ? "*** STILL IDENTICAL, THE CHECK IS VACUOUS ***"
                          : "decode differs, as it must");
    }

    // --- EXTERNAL COMPRESSORS -------------------------------------------------------
    std::printf("\n--- probing this machine for general-purpose compressors ---\n");
    std::vector<Ext> exts;
    {
        const std::vector<Ext> cand = {
            {"gzip -9",        "gzip -9 -c \"%IN%\" > \"%OUT%\""},
            {"gzip -9 (git)",  "\"C:\\Program Files\\Git\\usr\\bin\\gzip.exe\" -9 -c \"%IN%\" > \"%OUT%\""},
            {"bzip2 -9",       "bzip2 -9 -c \"%IN%\" > \"%OUT%\""},
            {"xz -9e",         "xz -9e -c \"%IN%\" > \"%OUT%\""},
            {"zstd -19",       "zstd -19 -q -f -o \"%OUT%\" \"%IN%\""},
            {"brotli -q 11",   "brotli -q 11 -f -o \"%OUT%\" \"%IN%\""},
            {"7z -mx=9 (LZMA2)",
             "\"C:\\Program Files\\7-Zip\\7z.exe\" a -t7z -mx=9 -bso0 -bsp0 \"%OUT%\" \"%IN%\""},
        };
        const std::string probe_out = (tmp / "khora_compress_probe.bin").string();
        bool have_gzip = false;
        for (const auto& c : cand) {
            const long long n = run_ext(c.tpl, f_test, probe_out);
            const bool good = n > 0 && static_cast<std::size_t>(n) < TE.text.size();
            std::printf("  %-20s %s%s\n", c.name.c_str(),
                        good ? "available" : "not on this box",
                        (good && c.name.rfind("gzip", 0) == 0 && have_gzip)
                            ? " (skipped, gzip already found)" : "");
            if (!good) continue;
            if (c.name.rfind("gzip", 0) == 0) { if (have_gzip) continue; have_gzip = true; }
            Ext e = c;
            e.test = n;
            exts.push_back(e);
        }
        std::error_code ec;
        std::filesystem::remove(probe_out, ec);
    }
    {
        const auto t0 = Clock::now();
        const std::string o1 = (tmp / "khora_compress_a.bin").string();
        const std::string o2 = (tmp / "khora_compress_b.bin").string();
        for (auto& e : exts) {
            const long long a = run_ext(e.tpl, f_train, o1);
            const long long b = run_ext(e.tpl, f_both,  o2);
            if (a > 0 && b > a) e.primed = b - a;
        }
        std::error_code ec;
        std::filesystem::remove(o1, ec);
        std::filesystem::remove(o2, ec);
        std::printf("  primed runs (compress(train ++ test) - compress(train)) over a"
                    " %.1f MB training prefix  [%.1fs]\n",
                    static_cast<double>(prime_bytes) / 1e6, secs(t0));
    }

    // --- THE TABLE --------------------------------------------------------------------
    const double bytes_msg = static_cast<double>(TE.text.size());
    const double n_tok     = static_cast<double>(TE.words);
    const auto line = [&](const char* what, double bits, const char* note) {
        std::printf("  %-40s | %12.0f | %7.4f | %7.3f | %s\n",
                    what, bits, bits / bytes_msg, bits / n_tok, note);
    };

    std::printf("\n=== COMPRESSED SIZE OF THE HELD-OUT TEXT ===\n");
    std::printf("  %zu bytes, %zu word tokens, %zu sentences. bits/char divides by BYTES\n"
                "  of the rendered file; bits/token divides by WORD TOKENS (</s> excluded\n"
                "  from the denominator, included in the bits). The two differ by ~%.2fx and\n"
                "  are constantly conflated, so both are printed.\n\n",
                TE.text.size(), TE.words, TE.sym.size() - TE.words,
                bytes_msg / n_tok);
    std::printf("  %-40s | %12s | %7s | %7s | %s\n",
                "", "bits", "bits/ch", "bits/tok", "notes");
    std::printf("  -----------------------------------------+--------------+---------"
                "+---------+------------------------\n");
    line("raw (8 bits per byte)", bytes_msg * 8.0, "no model at all");
    {
        double h = 0.0;
        std::vector<double> f(256, 0.0);
        for (const unsigned char c : TE.text) f[c] += 1.0;
        for (const double c : f)
            if (c > 0.0) { const double p = c / bytes_msg; h -= p * std::log2(p); }
        line("order-0 byte entropy of the TEST text", h * bytes_msg,
             "cheats: fitted on the test text");
    }
    for (const auto& e : exts) {
        char nm[96];
        std::snprintf(nm, sizeof nm, "%s  [external]", e.name.c_str());
        line(nm, static_cast<double>(e.test) * 8.0, "no corpus, knows no English");
    }
    std::printf("  -----------------------------------------+--------------+---------"
                "+---------+------------------------\n");
    for (const auto& r : rows) {
        char note[64];
        std::snprintf(note, sizeof note, "%s, coder overhead %+.2f%%",
                      r.ok ? "round trip OK" : "ROUND TRIP FAILED",
                      100.0 * (static_cast<double>(r.bytes) * 8.0 - r.ideal) / r.ideal);
        line(r.name.c_str(), static_cast<double>(r.bytes) * 8.0, note);
    }
    std::printf("  -----------------------------------------+--------------+---------"
                "+---------+------------------------\n");
    for (const auto& e : exts) {
        if (e.primed < 0) continue;
        char nm[96];
        std::snprintf(nm, sizeof nm, "%s PRIMED on %.1fMB train", e.name.c_str(),
                      static_cast<double>(prime_bytes) / 1e6);
        line(nm, static_cast<double>(e.primed) * 8.0, "same corpus advantage");
    }
    std::printf("\n  exact invocations (%%IN%% is the %zu-byte test file):\n", TE.text.size());
    for (const auto& e : exts)
        std::printf("    %-20s %s\n", e.name.c_str(),
                    subst(subst(e.tpl, "%IN%", "test.txt"), "%OUT%", "out.bin").c_str());

    // --- THE TRAINING-COST ASYMMETRY ---------------------------------------------------
    const std::size_t model_bytes = T3.bytes() + T2.bytes() + T2c.bytes()
                                  + (uni_cdf.size() + cont_cdf.size()) * sizeof(double);
    std::size_t vocab_bytes = 0;
    for (const auto& w : vword) vocab_bytes += w.size() + 1;
    std::size_t plex_bytes = 0;
    for (const auto& r : prow)
        plex_bytes += r.nb.size() * sizeof(Id) + (r.pref[0].size() + r.pref[1].size()) * sizeof(double);

    std::printf("\n=== WHAT EACH SIDE HAD TO BE GIVEN ===\n");
    std::printf("  gzip and friends      : the test file. Nothing else. No corpus, no\n"
                "                          vocabulary, no notion that this is English.\n");
    std::printf("  the PRIMED rows       : the same tools handed a %.1f MB prefix of the\n"
                "                          training text -- %.0f%% of the %.1f MB the n-gram\n"
                "                          models were fitted on. Capped for runtime, not for\n"
                "                          fairness: more prefix could only help them, though\n"
                "                          gzip's 32 KB window and bzip2's 900 KB blocks can\n"
                "                          barely reach across it in any case.\n",
                static_cast<double>(prime_bytes) / 1e6,
                100.0 * static_cast<double>(prime_bytes) / std::max(1.0, train_text_bytes),
                train_text_bytes / 1e6);
    std::printf("  Khora's n-gram models : %zu training tokens over %zu books, %.1f MB of\n"
                "                          count tables + %.2f MB of vocabulary strings.\n"
                "                          NONE of that is in the bitstream -- the decoder\n"
                "                          cannot run without it, and the numbers above do\n"
                "                          not charge for it.\n",
                tr.size(), tr_books, static_cast<double>(model_bytes) / 1e6,
                static_cast<double>(vocab_bytes) / 1e6);
    std::printf("  Khora's Plexus        : the same corpus, %.1f MB of graph rows.\n",
                static_cast<double>(plex_bytes) / 1e6);
    std::printf("  A self-contained archive would have to ship the model, and at %.0f MB\n"
                "  against a %.2f MB payload it would lose to gzip by two orders of\n"
                "  magnitude. The table above is the CONDITIONAL cost -- bits for the text\n"
                "  GIVEN the model -- which is the right question for 'how much does the\n"
                "  system understand' and the wrong one for 'is this a good archiver'.\n",
                static_cast<double>(model_bytes + plex_bytes) / 1e6, bytes_msg / 1e6);

    // --- THE READ, computed from the numbers above so it cannot drift from them ---------
    {
        std::size_t best_k = 0;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (rows[i].ok && rows[i].bytes < rows[best_k].bytes) best_k = i;
        std::size_t plex_only = 0, kn3 = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].name.rfind("PLEXUS", 0) == 0 && rows[i].name.find("unigram") != std::string::npos)
                plex_only = i;
            if (rows[i].name == "order-3 Kneser-Ney") kn3 = i;
        }
        bool all_ok = true;
        for (const auto& r : rows) all_ok = all_ok && r.ok;

        std::printf("\n=== THE READ ===\n");
        std::printf("  round trip           %s (%zu of %zu models decoded byte-identical)\n",
                    all_ok ? "VERIFIED" : "FAILED",
                    static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
                                                           [](const Row& r) { return r.ok; })),
                    rows.size());
        std::printf("  best Khora model     %-36s %.3f bits/token, %.4f bits/char\n",
                    rows[best_k].name.c_str(),
                    static_cast<double>(rows[best_k].bytes) * 8.0 / n_tok,
                    static_cast<double>(rows[best_k].bytes) * 8.0 / bytes_msg);
        std::printf("  Plexus alone         %-36s %.3f bits/token\n",
                    rows[plex_only].name.c_str(),
                    static_cast<double>(rows[plex_only].bytes) * 8.0 / n_tok);
        std::printf("  the graph on top of counting is worth lambda=%.2f on dev"
                    " -- %s\n", pn_lam,
                    pn_lam <= 0.0 ? "nothing at all"
                                  : "a nonzero but tuned-in weight, read the KN+PLEXUS row");
        for (const auto& e : exts) {
            const double ext_bpt = static_cast<double>(e.test) * 8.0 / n_tok;
            const double khora_bpt = static_cast<double>(rows[best_k].bytes) * 8.0 / n_tok;
            std::printf("  vs %-18s %7.3f bits/token -- Khora is %.2fx %s%s\n",
                        e.name.c_str(), ext_bpt,
                        ext_bpt > khora_bpt ? ext_bpt / khora_bpt : khora_bpt / ext_bpt,
                        ext_bpt > khora_bpt ? "SMALLER" : "LARGER",
                        e.primed > 0
                            ? "" : "");
            if (e.primed > 0)
                std::printf("     %-18s %7.3f bits/token when primed on the same corpus"
                            " -- Khora is %.2fx %s\n", "(primed)",
                            static_cast<double>(e.primed) * 8.0 / n_tok,
                            (static_cast<double>(e.primed) * 8.0 / n_tok) > khora_bpt
                                ? (static_cast<double>(e.primed) * 8.0 / n_tok) / khora_bpt
                                : khora_bpt / (static_cast<double>(e.primed) * 8.0 / n_tok),
                            (static_cast<double>(e.primed) * 8.0 / n_tok) > khora_bpt
                                ? "SMALLER" : "LARGER");
        }
        std::printf("  (kneser-ney) minus (unigram) is what word ORDER is worth here:"
                    " %.3f bits/token.\n",
                    static_cast<double>(rows[0].bytes - rows[kn3].bytes) * 8.0 / n_tok);
        std::printf("  %.1f%% of the best model's bits (%.0f of %.0f, ideal) went on spelling\n"
                    "  out the %zu tokens whose words the closed vocabulary never learned.\n"
                    "  gzip pays no such tax: it has no vocabulary to fall outside of.\n",
                    100.0 * rows[best_k].esc / rows[best_k].ideal,
                    rows[best_k].esc, rows[best_k].ideal, TE.oov.size());

        // The line that decides how this result should be read. If the UNIGRAM --
        // a table of word frequencies with no notion of order, context, or
        // meaning -- already beats every general-purpose compressor here, then
        // "Khora beats gzip" is mostly a statement about having been handed a
        // corpus, and only the margin above the unigram is about modelling.
        {
            double best_ext = 1e300;
            const char* best_ext_name = "(none found)";
            for (const auto& e : exts) {
                const double b = static_cast<double>(e.primed > 0 ? e.primed : e.test) * 8.0 / n_tok;
                if (b < best_ext) { best_ext = b; best_ext_name = e.name.c_str(); }
            }
            const double uni_bpt = static_cast<double>(rows[0].bytes) * 8.0 / n_tok;
            std::printf("  READ IT THIS WAY: the strongest external bar is %s at %.3f\n"
                        "  bits/token (best of its plain and primed runs). The UNIGRAM -- word\n"
                        "  frequencies alone, no order, no context, no graph -- already sits at\n"
                        "  %.3f, %s it. So most of the margin is the corpus, not the\n"
                        "  modelling. Order is worth a further %.3f bits/token on top; the\n"
                        "  Plexus, the part of this that is actually Khora's own machinery, is\n"
                        "  worth %.3f over the unigram and %.3f over the trigram.\n",
                        best_ext_name, best_ext, uni_bpt,
                        uni_bpt < best_ext ? "already under" : "still above",
                        static_cast<double>(rows[0].bytes - rows[kn3].bytes) * 8.0 / n_tok,
                        static_cast<double>(rows[0].bytes - rows[plex_only].bytes) * 8.0 / n_tok,
                        static_cast<double>(rows[kn3].bytes - rows[rows.size() - 1].bytes) * 8.0 / n_tok);
        }
    }

    std::error_code ec;
    std::filesystem::remove(f_test, ec);
    std::filesystem::remove(f_train, ec);
    std::filesystem::remove(f_both, ec);
    return 0;
}
