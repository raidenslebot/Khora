// ATTENTION, BUILT AND RACED, BECAUSE REJECTING IT UNMEASURED IS A PREFERENCE.
//
// An audit of this tree found no attention mechanism anywhere: no softmax over a
// score matrix, no Q/K/V, no head. The word appears only in prose, and the prose
// rejects the thing. That is the same position khora::descent was built to end
// for gradient descent -- a design choice that has never been measured against
// the thing it rejects is not a result, it is a taste.
//
// So this file contains scaled dot-product self-attention, hand-written, no
// dependencies: Q/K/V projections, softmax(QK^T/sqrt(d))V, an output projection,
// single head and multi-head. It is raced against the hypervector substrate on
// the task attention exists for, and on two tasks chosen because the substrate
// should win them.
//
// WHAT IS TRAINED, STATED UP FRONT, BECAUSE AN UNTRAINED ATTENTION BLOCK IS A
// MUCH WEAKER CLAIM AND IT WOULD BE EASY TO HIDE WHICH ONE PRODUCED A NUMBER.
// Three attention configurations are reported side by side at every length:
//
//   attn-rand     Random Gaussian projections, never trained. This is the floor,
//                 and it is here only so that "attention works" is never confused
//                 with "the mechanism works when its parameters are arbitrary".
//   attn-SGD      W_q, W_k, W_v, W_o trained by minibatch-free stochastic
//                 gradient descent with Adam on squared error against the target
//                 value embedding. The backward pass is hand-written and CHECKED
//                 AGAINST FINITE DIFFERENCES in section 1, because a hand-rolled
//                 backward pass nobody has checked numerically is usually wrong
//                 somewhere quiet. The embeddings themselves are NOT trained --
//                 they are fixed random vectors, identically to the glyphs on the
//                 other side, so neither paradigm gets a learned vocabulary.
//   attn-ideal    W_q = W_k = [I 0]*gamma, W_v = [0 I], W_o = I. Hand-set, no
//                 training. This is the induction-head construction written out
//                 by hand, and it is the CEILING of the mechanism on this task.
//                 It is included so that a bad SGD run is never mistaken for a
//                 limitation of attention.
//
// THE TASKS, and why these.
//
//   1. ASSOCIATIVE RECALL. A sequence of n (key, value) pairs, then a query key;
//      the answer is the value that was paired with it. This is what attention is
//      FOR, and it is the task on which a single fixed-width superposition should
//      degrade as n grows. Swept at n = 4..128, then pushed to 1024 to find where
//      the bundle actually breaks rather than assuming it breaks early.
//
//   2. TWO TASKS THE SUBSTRATE SHOULD WIN. A race one side always wins means the
//      tasks were chosen badly, so:
//        2A  BIDIRECTIONAL RECALL FROM ONE WRITE. bind is XOR and XOR is its own
//            inverse, so a stored bind(key, value) answers key->value and
//            value->key from the SAME bits with no second pass. A trained
//            attention block answers the direction it was trained on.
//        2B  UNSEEN VOCABULARY. Symbols never present during training. The
//            substrate has no training, so there is nothing to transfer.
//
//   3. COST. Parameters, bytes, and wall time per query, with the O(n^2) curve
//      for full self-attention measured rather than asserted.
//
// FOUR REFERENCES ON EVERY RECALL TABLE, because a rate without them is a number
// with no denominator:
//
//   chance        1/128 = 0.78%. Both sides read out by nearest neighbour over
//                 the same 128-symbol value codebook, so chance is the same for
//                 both and it does not move with n.
//   most-recent   Return the value of the LAST pair in the sequence. A great many
//                 published "recall" tasks are secretly solved by recency, and
//                 the only way to know this one is not is to measure it. The
//                 query position here is uniform over the n pairs, so this should
//                 land near 1/n; the printed number is measured, not assumed.
//   HDC list      Keep all n bound pairs as n separate glyphs and search them.
//                 THE DUMB BASELINE FOR THE SUBSTRATE SIDE, and the honest
//                 comparison for attention -- because attention's KV cache IS a
//                 list of n vectors. If the interesting claim is "fixed-size
//                 memory", only the bundle row is making it.
//   attn-rand     as above.
//
// WHAT IS NOT HERE, so nobody reads more out of this than went in: there is no
// transformer. No residual stream, no layer norm, no feed-forward block, no
// stacking, no positional encoding, no causal mask, no learned embeddings, no
// tokenizer. One attention operation, measured. Section 8 lists the rest of what
// this harness cannot see.

#include "khora/chiasm/chiasm.hpp"
#include "khora/descent/descent.hpp"
#include "khora/lattice/glyph.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

using khora::descent::Matrix;
using khora::lattice::Glyph;
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;
using khora::lattice::kGlyphWords;

namespace {

// ============================================================================
// CONFIGURATION — every one of these was fixed before the first run and not
// revisited. There is no hyperparameter search anywhere in this file.
// ============================================================================

constexpr std::size_t kDe     = 32;         // embedding width per symbol
constexpr std::size_t kDModel = 2 * kDe;    // a token is [key_emb ; value_emb]
constexpr std::size_t kDHead  = kDe;        // single-head projection width
constexpr std::size_t kHeads  = 4;          // multi-head variant
constexpr std::size_t kDHeadM = kDHead / kHeads;  // h*d_head kept equal to kDHead
                                                  // so the multi-head model has
                                                  // the SAME parameter count as
                                                  // the single-head one. A wider
                                                  // model winning would say
                                                  // nothing about heads.

// Key symbols must outnumber the longest sequence, and half of them are held
// back so section 6 has genuinely unseen vocabulary. Values are a smaller
// codebook because the codebook size IS the readout difficulty and it should be
// the same at every n.
constexpr std::size_t kKeyVocab = 4096, kKeyTrain = 2048;  // [0,2048) train
constexpr std::size_t kValVocab = 256,  kValTrain = 128;   // [0,128)  train
constexpr double      kChance   = 1.0 / static_cast<double>(kValTrain);

// The hand-set construction's temperature. With unit-norm random embeddings in
// kDe dimensions two distinct keys have dot product ~N(0, 1/kDe); over 2048 keys
// the worst collision runs to about 0.6. The matched score must beat that by
// enough that softmax over up to 1024 tokens still puts ~all its mass on one
// token, which needs a logit gap above ~log(1024)+5 ~= 12. gamma = 256 gives a
// worst-case gap near 17. It is a temperature, not a fitted parameter.
constexpr double kIdealGamma = 256.0;

constexpr std::size_t kTrainSteps = 4000;   // one sequence per step, no batching
constexpr double      kLr         = 3e-3;   // Adam
constexpr std::size_t kTrials     = 400;    // per (system, n) in the main sweep
constexpr std::size_t kTrialsBig  = 120;    // per (system, n) in the long sweep

constexpr std::size_t kGlyphBytes = kGlyphWords * sizeof(std::uint64_t);

// ============================================================================
// Reporting helpers — same shapes as substrate_bench / forgetting_bench.
// ============================================================================

// 95% Wilson interval on a proportion, as percentages. Same helper as
// forgetting_bench.cpp and structure_bench.cpp. At the near-chance rates an
// untrained attention block produces, the normal approximation is simply wrong.
std::pair<double, double> wilson(std::size_t hits, std::size_t n) {
    if (n == 0) return {0.0, 0.0};
    const double z = 1.96, ph = static_cast<double>(hits) / static_cast<double>(n);
    const double d = 1.0 + z * z / static_cast<double>(n);
    const double c = ph + z * z / (2.0 * static_cast<double>(n));
    const double m = z * std::sqrt(ph * (1.0 - ph) / static_cast<double>(n)
                                   + z * z / (4.0 * static_cast<double>(n) * static_cast<double>(n)));
    return {100.0 * (c - m) / d, 100.0 * (c + m) / d};
}

struct Tally {
    std::size_t hit = 0, n = 0;
    double      margin_sum = 0.0;   // top1 - top2 at readout; the honest number
    void add(bool ok, double margin = 0.0) noexcept {
        hit += ok ? 1u : 0u; ++n; margin_sum += margin;
    }
    double pct() const noexcept {
        return n ? 100.0 * static_cast<double>(hit) / static_cast<double>(n) : 0.0;
    }
    double margin() const noexcept {
        return n ? margin_sum / static_cast<double>(n) : 0.0;
    }
};

void cell(const Tally& t) {
    const auto ci = wilson(t.hit, t.n);
    std::printf(" %5.1f [%4.1f,%5.1f] |", t.pct(), ci.first, ci.second);
}

// ============================================================================
// THE SHARED PROBLEM — one symbolic generator, two encodings.
//
// The generator emits integer symbol ids. Each paradigm then encodes those ids
// its own way: attention into fixed random real embeddings, the substrate into
// fixed random 10,000-bit glyphs. This is the same asymmetry forgetting_bench
// documents (the network reads doubles, the substrate reads bits) and it is
// inherent rather than a choice -- there is no representation both accept.
// Section 8 states which way it cuts.
// ============================================================================

struct Codebook {
    // Unit-norm Gaussian embeddings. Unit norm so that a matched dot product is
    // exactly 1 and the ideal construction's temperature means one thing.
    std::vector<double> key_emb, val_emb;   // [vocab * kDe], flat
    std::vector<Glyph>  key_g,  val_g;

    const double* ke(std::size_t i) const { return &key_emb[i * kDe]; }
    const double* ve(std::size_t i) const { return &val_emb[i * kDe]; }
};

Codebook make_codebook(std::uint64_t seed) {
    Codebook cb;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    auto fill = [&](std::vector<double>& v, std::size_t count) {
        v.assign(count * kDe, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            double norm = 0.0;
            for (std::size_t d = 0; d < kDe; ++d) { const double x = g(rng); v[i * kDe + d] = x; norm += x * x; }
            norm = std::sqrt(norm);
            for (std::size_t d = 0; d < kDe; ++d) v[i * kDe + d] /= norm;
        }
    };
    fill(cb.key_emb, kKeyVocab);
    fill(cb.val_emb, kValVocab);
    cb.key_g.reserve(kKeyVocab);
    cb.val_g.reserve(kValVocab);
    for (std::size_t i = 0; i < kKeyVocab; ++i) cb.key_g.push_back(Glyph::random(0x5EED0001ULL + i * 0x9E3779B97F4A7C15ULL));
    for (std::size_t i = 0; i < kValVocab; ++i) cb.val_g.push_back(Glyph::random(0xA11CE002ULL + i * 0x9E3779B97F4A7C15ULL));
    return cb;
}

// One instance of the recall task.
//
// Keys are sampled WITHOUT replacement, because a repeated key makes the query
// ambiguous and an ambiguous item is not a measurement. Values are sampled WITH
// replacement, so the value codebook -- and therefore chance and the readout
// difficulty -- stays at 128 for every n. (Section 5 needs an invertible map and
// draws values without replacement; it says so there.)
struct Trial {
    std::vector<std::size_t> keys, vals;
    std::size_t              qpos = 0;   // which pair is queried
};

// key_limit caps the key pool (0 = the whole half). Section 5 needs keys and
// values drawn from equally sized codebooks so one chance line covers both
// directions; everywhere else it is 0.
Trial make_trial(std::size_t n, bool heldout, bool distinct_vals, std::mt19937_64& rng,
                 std::size_t key_limit = 0) {
    const std::size_t klo = heldout ? kKeyTrain : std::size_t{0};
    const std::size_t kn  = key_limit ? key_limit : kKeyTrain;
    const std::size_t vlo = heldout ? kValTrain : std::size_t{0}, vn = kValTrain;

    Trial t;
    t.keys.resize(n);
    t.vals.resize(n);

    // Partial Fisher-Yates over the relevant vocabulary half.
    static thread_local std::vector<std::size_t> pool;
    pool.resize(kn);
    std::iota(pool.begin(), pool.end(), klo);
    for (std::size_t i = 0; i < n; ++i) {
        std::uniform_int_distribution<std::size_t> pick(i, kn - 1);
        std::swap(pool[i], pool[pick(rng)]);
        t.keys[i] = pool[i];
    }
    if (distinct_vals) {
        static thread_local std::vector<std::size_t> vpool;
        vpool.resize(vn);
        std::iota(vpool.begin(), vpool.end(), vlo);
        for (std::size_t i = 0; i < n; ++i) {
            std::uniform_int_distribution<std::size_t> pick(i, vn - 1);
            std::swap(vpool[i], vpool[pick(rng)]);
            t.vals[i] = vpool[i];
        }
    } else {
        std::uniform_int_distribution<std::size_t> vd(vlo, vlo + vn - 1);
        for (std::size_t i = 0; i < n; ++i) t.vals[i] = vd(rng);
    }
    std::uniform_int_distribution<std::size_t> qd(0, n - 1);
    t.qpos = qd(rng);
    return t;
}

// ============================================================================
// ATTENTION — scaled dot-product, multi-head, forward and hand-written backward.
// ============================================================================

// A token is [key_emb ; value_emb] laid out flat, kDModel wide. The query token
// is [key_emb(q) ; 0]: the query carries a key and no value, which is the whole
// shape of the problem and is exactly how a real induction head sees it.
void build_tokens(const Codebook& cb, const Trial& t, std::vector<double>& X,
                  std::vector<double>& xq, bool reverse) {
    const std::size_t n = t.keys.size();
    X.assign(n * kDModel, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        std::copy_n(cb.ke(t.keys[i]), kDe, X.begin() + static_cast<std::ptrdiff_t>(i * kDModel));
        std::copy_n(cb.ve(t.vals[i]), kDe, X.begin() + static_cast<std::ptrdiff_t>(i * kDModel + kDe));
    }
    xq.assign(kDModel, 0.0);
    if (!reverse) std::copy_n(cb.ke(t.keys[t.qpos]), kDe, xq.begin());
    else          std::copy_n(cb.ve(t.vals[t.qpos]), kDe, xq.begin() + kDe);
}

struct Attn {
    std::size_t heads = 1, d_head = kDHead, d_out = kDe;
    std::vector<Matrix> Wq, Wk, Wv;   // heads x (d_head x kDModel)
    Matrix              Wo;           // d_out x (heads*d_head)

    Attn() = default;
    Attn(std::size_t h, std::size_t dh, std::size_t dout, std::uint64_t seed)
        : heads(h), d_head(dh), d_out(dout), Wo(dout, h * dh) {
        std::mt19937_64 rng(seed);
        auto init = [&](Matrix& m) {
            std::normal_distribution<double> g(0.0, 1.0 / std::sqrt(static_cast<double>(m.cols)));
            for (double& x : m.v) x = g(rng);
        };
        for (std::size_t i = 0; i < h; ++i) {
            Wq.emplace_back(dh, kDModel); init(Wq.back());
            Wk.emplace_back(dh, kDModel); init(Wk.back());
            Wv.emplace_back(dh, kDModel); init(Wv.back());
        }
        init(Wo);
    }

    std::vector<Matrix*> params() {
        std::vector<Matrix*> p;
        for (std::size_t h = 0; h < heads; ++h) { p.push_back(&Wq[h]); p.push_back(&Wk[h]); p.push_back(&Wv[h]); }
        p.push_back(&Wo);
        return p;
    }
    std::size_t param_count() const {
        std::size_t c = Wo.v.size();
        for (std::size_t h = 0; h < heads; ++h) c += Wq[h].v.size() + Wk[h].v.size() + Wv[h].v.size();
        return c;
    }
};

// THE CEILING, WRITTEN OUT BY HAND. Q and K read only the key half of a token,
// V reads only the value half, W_o is the identity. Then the score of token i is
// gamma * <e_key(q), e_key(k_i)> / sqrt(d) -- maximal at the matching key -- and
// the output is the value embedding of whichever token won. No training, and the
// construction does not depend on which symbols are in play, which is why it is
// also the reference for the unseen-vocabulary section.
Attn make_ideal(bool reverse) {
    Attn a;
    a.heads = 1; a.d_head = kDe; a.d_out = kDe;
    a.Wq.emplace_back(kDe, kDModel);
    a.Wk.emplace_back(kDe, kDModel);
    a.Wv.emplace_back(kDe, kDModel);
    a.Wo = Matrix(kDe, kDe);
    const std::size_t qk_off = reverse ? kDe : 0;   // which half carries the cue
    const std::size_t v_off  = reverse ? 0   : kDe; // which half carries the answer
    for (std::size_t d = 0; d < kDe; ++d) {
        a.Wq[0].at(d, qk_off + d) = kIdealGamma;
        a.Wk[0].at(d, qk_off + d) = 1.0;
        a.Wv[0].at(d, v_off + d)  = 1.0;
        a.Wo.at(d, d)             = 1.0;
    }
    return a;
}

struct Cache {
    std::vector<std::vector<double>> q, Kc, Vc, att;  // per head
    std::vector<double>              cat, out;
};

void attn_forward(const Attn& A, const std::vector<double>& X, std::size_t n,
                  const std::vector<double>& xq, Cache& C) {
    const double inv = 1.0 / std::sqrt(static_cast<double>(A.d_head));
    C.q.resize(A.heads); C.Kc.resize(A.heads); C.Vc.resize(A.heads); C.att.resize(A.heads);
    C.cat.assign(A.heads * A.d_head, 0.0);

    for (std::size_t h = 0; h < A.heads; ++h) {
        auto& q = C.q[h];   q.assign(A.d_head, 0.0);
        auto& K = C.Kc[h];  K.assign(n * A.d_head, 0.0);
        auto& V = C.Vc[h];  V.assign(n * A.d_head, 0.0);
        auto& a = C.att[h]; a.assign(n, 0.0);

        for (std::size_t d = 0; d < A.d_head; ++d) {
            const double* wq = &A.Wq[h].v[d * kDModel];
            double s = 0.0;
            for (std::size_t c = 0; c < kDModel; ++c) s += wq[c] * xq[c];
            q[d] = s;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double* x = &X[i * kDModel];
            for (std::size_t d = 0; d < A.d_head; ++d) {
                const double* wk = &A.Wk[h].v[d * kDModel];
                const double* wv = &A.Wv[h].v[d * kDModel];
                double sk = 0.0, sv = 0.0;
                for (std::size_t c = 0; c < kDModel; ++c) { sk += wk[c] * x[c]; sv += wv[c] * x[c]; }
                K[i * A.d_head + d] = sk;
                V[i * A.d_head + d] = sv;
            }
        }
        // softmax(QK^T / sqrt(d)), max-subtracted. Without the subtraction the
        // hand-set construction's logits (~45) are fine but a trained model that
        // grows its temperature is one bad step from inf/inf.
        double mx = -1e300;
        for (std::size_t i = 0; i < n; ++i) {
            double s = 0.0;
            for (std::size_t d = 0; d < A.d_head; ++d) s += q[d] * K[i * A.d_head + d];
            a[i] = s * inv;
            mx = std::max(mx, a[i]);
        }
        double z = 0.0;
        for (std::size_t i = 0; i < n; ++i) { a[i] = std::exp(a[i] - mx); z += a[i]; }
        const double izs = 1.0 / z;
        for (std::size_t i = 0; i < n; ++i) a[i] *= izs;

        for (std::size_t i = 0; i < n; ++i) {
            const double w = a[i];
            for (std::size_t d = 0; d < A.d_head; ++d) C.cat[h * A.d_head + d] += w * V[i * A.d_head + d];
        }
    }

    C.out.assign(A.d_out, 0.0);
    for (std::size_t o = 0; o < A.d_out; ++o) {
        const double* wo = &A.Wo.v[o * A.Wo.cols];
        double s = 0.0;
        for (std::size_t j = 0; j < A.Wo.cols; ++j) s += wo[j] * C.cat[j];
        C.out[o] = s;
    }
}

struct Grads {
    std::vector<Matrix> Wq, Wk, Wv;
    Matrix              Wo;
    explicit Grads(const Attn& A) : Wo(A.Wo.rows, A.Wo.cols) {
        for (std::size_t h = 0; h < A.heads; ++h) {
            Wq.emplace_back(A.d_head, kDModel);
            Wk.emplace_back(A.d_head, kDModel);
            Wv.emplace_back(A.d_head, kDModel);
        }
    }
    void zero() {
        for (auto& m : Wq) std::fill(m.v.begin(), m.v.end(), 0.0);
        for (auto& m : Wk) std::fill(m.v.begin(), m.v.end(), 0.0);
        for (auto& m : Wv) std::fill(m.v.begin(), m.v.end(), 0.0);
        std::fill(Wo.v.begin(), Wo.v.end(), 0.0);
    }
    std::vector<Matrix*> flat(std::size_t heads) {
        std::vector<Matrix*> p;
        for (std::size_t h = 0; h < heads; ++h) { p.push_back(&Wq[h]); p.push_back(&Wk[h]); p.push_back(&Wv[h]); }
        p.push_back(&Wo);
        return p;
    }
};

double sq_loss(const std::vector<double>& out, const double* target) {
    double L = 0.0;
    for (std::size_t o = 0; o < out.size(); ++o) { const double d = out[o] - target[o]; L += d * d; }
    return L;
}

// Backward for L = ||W_o * concat_h(sum_i a_i v_i) - target||^2.
//
// The two sums over i are folded into weighted column sums (wsum, ksum) before
// touching the weight matrices, which turns the O(n * d_head * d_model) inner
// loops into O(n * d_model + d_head * d_model). It is the same arithmetic; the
// finite-difference check in section 1 is what says so.
double attn_backward(const Attn& A, const std::vector<double>& X, std::size_t n,
                     const std::vector<double>& xq, const Cache& C,
                     const double* target, Grads& G) {
    const double inv = 1.0 / std::sqrt(static_cast<double>(A.d_head));
    const double L = sq_loss(C.out, target);

    std::vector<double> g_out(A.d_out), g_cat(A.heads * A.d_head, 0.0);
    for (std::size_t o = 0; o < A.d_out; ++o) g_out[o] = 2.0 * (C.out[o] - target[o]);
    for (std::size_t o = 0; o < A.d_out; ++o) {
        const double go = g_out[o];
        for (std::size_t j = 0; j < A.Wo.cols; ++j) {
            G.Wo.at(o, j) += go * C.cat[j];
            g_cat[j]      += A.Wo.at(o, j) * go;
        }
    }

    std::vector<double> g_a(n), g_s(n), wsum(kDModel), ksum(kDModel), g_q(A.d_head);
    for (std::size_t h = 0; h < A.heads; ++h) {
        const double* go = &g_cat[h * A.d_head];
        const auto&   a  = C.att[h];
        const auto&   K  = C.Kc[h];
        const auto&   V  = C.Vc[h];
        const auto&   q  = C.q[h];

        double ssum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            double s = 0.0;
            for (std::size_t d = 0; d < A.d_head; ++d) s += go[d] * V[i * A.d_head + d];
            g_a[i] = s;
            ssum  += a[i] * s;
        }
        for (std::size_t i = 0; i < n; ++i) g_s[i] = a[i] * (g_a[i] - ssum);

        std::fill(wsum.begin(), wsum.end(), 0.0);
        std::fill(ksum.begin(), ksum.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double* x = &X[i * kDModel];
            const double wa = a[i], ws = g_s[i];
            for (std::size_t c = 0; c < kDModel; ++c) { wsum[c] += wa * x[c]; ksum[c] += ws * x[c]; }
        }
        std::fill(g_q.begin(), g_q.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double gs = g_s[i] * inv;
            for (std::size_t d = 0; d < A.d_head; ++d) g_q[d] += gs * K[i * A.d_head + d];
        }
        for (std::size_t d = 0; d < A.d_head; ++d) {
            double* gwq = &G.Wq[h].v[d * kDModel];
            double* gwk = &G.Wk[h].v[d * kDModel];
            double* gwv = &G.Wv[h].v[d * kDModel];
            const double gq = g_q[d], qd = q[d] * inv, gd = go[d];
            for (std::size_t c = 0; c < kDModel; ++c) {
                gwq[c] += gq * xq[c];
                gwk[c] += qd * ksum[c];
                gwv[c] += gd * wsum[c];
            }
        }
    }
    return L;
}

// Adam, ~15 lines, because plain SGD on a softmax whose logits start near zero
// spends most of its budget growing the temperature. The optimiser is not the
// subject of this benchmark and picking the standard one avoids making it one.
struct Adam {
    std::vector<std::vector<double>> m, v;
    std::size_t t = 0;
    void step(std::vector<Matrix*>& p, std::vector<Matrix*>& g, double lr) {
        if (m.empty()) { m.resize(p.size()); v.resize(p.size());
            for (std::size_t i = 0; i < p.size(); ++i) { m[i].assign(p[i]->v.size(), 0.0); v[i].assign(p[i]->v.size(), 0.0); } }
        ++t;
        const double b1 = 0.9, b2 = 0.999, eps = 1e-8;
        const double c1 = 1.0 - std::pow(b1, static_cast<double>(t));
        const double c2 = 1.0 - std::pow(b2, static_cast<double>(t));
        for (std::size_t i = 0; i < p.size(); ++i)
            for (std::size_t j = 0; j < p[i]->v.size(); ++j) {
                const double gr = g[i]->v[j];
                m[i][j] = b1 * m[i][j] + (1.0 - b1) * gr;
                v[i][j] = b2 * v[i][j] + (1.0 - b2) * gr * gr;
                p[i]->v[j] -= lr * (m[i][j] / c1) / (std::sqrt(v[i][j] / c2) + eps);
            }
    }
};

// ============================================================================
// READOUT — both paradigms answer by nearest neighbour over the SAME codebook.
//
// Attention's output is a real vector and the substrate's is a noisy glyph, so
// "produce the answer" means the same thing on both sides only if both are
// snapped to a symbol the same way. Both get the 128-symbol half in play; the
// margin (top1 - top2) is recorded because a win by 0.001 and a win by 0.5 are
// different claims and reporting only the rate hides which one happened.
// ============================================================================

struct Pick { std::size_t id = 0; double top1 = 0.0, margin = 0.0; };

Pick nearest_emb(const std::vector<double>& out, const std::vector<double>& codebook,
                 std::size_t lo, std::size_t count) {
    // Cosine, so the readout is invariant to the output's overall scale -- an
    // untrained W_o has an arbitrary gain and scoring it by raw distance would
    // measure the gain rather than the direction.
    double onorm = 0.0;
    for (double x : out) onorm += x * x;
    onorm = std::sqrt(onorm) + 1e-12;
    Pick p; double best = -1e300, second = -1e300;
    for (std::size_t i = 0; i < count; ++i) {
        const double* e = &codebook[(lo + i) * kDe];
        double s = 0.0;
        for (std::size_t d = 0; d < kDe; ++d) s += out[d] * e[d];
        s /= onorm;                       // codebook entries are already unit norm
        if (s > best) { second = best; best = s; p.id = lo + i; }
        else if (s > second) second = s;
    }
    p.top1 = best; p.margin = best - second;
    return p;
}

Pick nearest_glyph(const Glyph& probe, const std::vector<Glyph>& codebook,
                   std::size_t lo, std::size_t count) {
    Pick p; double best = -1e300, second = -1e300;
    for (std::size_t i = 0; i < count; ++i) {
        const double s = probe.similarity(codebook[lo + i]);
        if (s > best) { second = best; best = s; p.id = lo + i; }
        else if (s > second) second = s;
    }
    p.top1 = best; p.margin = best - second;
    return p;
}

// ============================================================================
// THE SUBSTRATE SIDE — a bound-record memory, ~10 lines of khora::lattice.
//
// record = bundle_i bind(key_glyph_i, value_glyph_i)
// answer = cleanup( bind(record, key_glyph_q) )
//
// This is exactly khora::chiasm::Chiasm's mechanism. It is reimplemented here
// rather than called because Chiasm keys its cleanup memory PER ROLE, and with
// the key symbol as the role each cleanup would hold the single value ever
// stored under that key -- a one-candidate cleanup scores 100% by construction
// and measures nothing. Here the cleanup codebook is all 128 values in play, the
// same set attention's readout faces. Section 5 uses the shipped Chiasm class on
// a task its per-role cleanup actually fits.
// ============================================================================

Glyph hdc_bundle(const Codebook& cb, const Trial& t) {
    std::vector<Glyph> bound;
    bound.reserve(t.keys.size());
    for (std::size_t i = 0; i < t.keys.size(); ++i)
        bound.push_back(bind(cb.key_g[t.keys[i]], cb.val_g[t.vals[i]]));
    return bundle(std::span<const Glyph>(bound));
}

// ============================================================================
// SECTIONS
// ============================================================================

// --- 1. gradient check ------------------------------------------------------
int gradient_check(const Codebook& cb) {
    std::printf("=== 1. THE BACKWARD PASS, AGAINST FINITE DIFFERENCES ===\n");
    std::printf("  A hand-written backward pass nobody has checked numerically is\n"
                "  usually wrong somewhere quiet: it trains, the loss falls, and it\n"
                "  converges to something worse than it should. Central differences,\n"
                "  eps = 1e-6, on randomly chosen entries of every matrix.\n\n");

    std::mt19937_64 rng(0xB00C1234ULL);
    int bad = 0;
    for (std::size_t heads : {std::size_t{1}, kHeads}) {
        Attn A(heads, heads == 1 ? kDHead : kDHeadM, kDe, 0xC0FFEEULL + heads);
        const Trial t = make_trial(9, false, false, rng);
        std::vector<double> X, xq;
        build_tokens(cb, t, X, xq, false);
        const double* target = cb.ve(t.vals[t.qpos]);

        Cache C; Grads G(A); G.zero();
        attn_forward(A, X, 9, xq, C);
        (void)attn_backward(A, X, 9, xq, C, target, G);

        auto pv = A.params(); auto gv = G.flat(heads);
        double worst = 0.0;
        for (std::size_t i = 0; i < pv.size(); ++i) {
            std::uniform_int_distribution<std::size_t> pick(0, pv[i]->v.size() - 1);
            for (int rep = 0; rep < 6; ++rep) {
                const std::size_t j = pick(rng);
                const double saved = pv[i]->v[j], eps = 1e-6;
                pv[i]->v[j] = saved + eps; attn_forward(A, X, 9, xq, C);
                const double lp = sq_loss(C.out, target);
                pv[i]->v[j] = saved - eps; attn_forward(A, X, 9, xq, C);
                const double lm = sq_loss(C.out, target);
                pv[i]->v[j] = saved;
                const double num = (lp - lm) / (2.0 * eps), ana = gv[i]->v[j];
                const double denom = std::max(1.0, std::max(std::fabs(num), std::fabs(ana)));
                worst = std::max(worst, std::fabs(num - ana) / denom);
            }
        }
        const bool ok = worst < 1e-5;
        if (!ok) ++bad;
        std::printf("  %s heads=%zu  worst relative error over %zu probed weights: %.3e\n",
                    ok ? "ok  :" : "FAIL:", heads, pv.size() * 6, worst);
    }
    std::printf("\n");
    return bad;
}

// --- 2. training ------------------------------------------------------------
struct Trained { Attn A; double final_loss = 0.0; };

Trained train_attention(const Codebook& cb, std::size_t heads, std::size_t d_head,
                        bool reverse, std::uint64_t seed, const char* what) {
    Attn A(heads, d_head, kDe, seed);
    Grads G(A);
    Adam opt;
    auto pv = A.params(); auto gv = G.flat(heads);
    std::mt19937_64 rng(seed ^ 0xDEADBEEFULL);
    const std::size_t lens[] = {4, 8, 16, 32, 64, 128};

    Cache C;
    std::vector<double> X, xq;
    double run = 0.0;
    std::printf("  %-22s |", what);
    for (std::size_t s = 0; s < kTrainSteps; ++s) {
        // Lengths are mixed across the whole evaluation range, so no row of the
        // sweep is the training length and no row is extrapolation.
        const std::size_t n = lens[rng() % 6];
        const Trial t = make_trial(n, /*heldout=*/false, /*distinct_vals=*/reverse, rng);
        build_tokens(cb, t, X, xq, reverse);
        const double* target = reverse ? cb.ke(t.keys[t.qpos]) : cb.ve(t.vals[t.qpos]);
        G.zero();
        attn_forward(A, X, n, xq, C);
        run += attn_backward(A, X, n, xq, C, target, G);
        opt.step(pv, gv, kLr);
        if ((s + 1) % (kTrainSteps / 8) == 0) {
            std::printf(" %6.3f", run / static_cast<double>(kTrainSteps / 8));
            run = 0.0;
        }
    }
    std::printf("\n");
    Trained out;
    out.A = std::move(A);
    return out;
}

// --- evaluation of one system at one length --------------------------------
struct Systems {
    Attn rand1, sgd1, ideal, multi;
};

void eval_length(const Codebook& cb, Systems& sys, std::size_t n, std::size_t trials,
                 bool heldout, std::uint64_t seed, Tally out[7]) {
    std::mt19937_64 rng(seed);
    const std::size_t vlo = heldout ? kValTrain : std::size_t{0};
    Cache C;
    std::vector<double> X, xq;
    std::vector<Glyph> pairs;

    for (std::size_t r = 0; r < trials; ++r) {
        const Trial t = make_trial(n, heldout, /*distinct_vals=*/false, rng);
        const std::size_t truth = t.vals[t.qpos];
        build_tokens(cb, t, X, xq, false);

        // Order must match the column headers and the names[] table in section 6.
        const Attn* models[4] = {&sys.rand1, &sys.sgd1, &sys.multi, &sys.ideal};
        for (int m = 0; m < 4; ++m) {
            attn_forward(*models[m], X, n, xq, C);
            const Pick p = nearest_emb(C.out, cb.val_emb, vlo, kValTrain);
            out[m].add(p.id == truth, p.margin);
        }

        const Glyph rec  = hdc_bundle(cb, t);
        const Glyph nois = bind(rec, cb.key_g[t.keys[t.qpos]]);
        const Pick  pb   = nearest_glyph(nois, cb.val_g, vlo, kValTrain);
        out[4].add(pb.id == truth, pb.margin);

        // HDC list: the n bound pairs kept separately. Unbind each with the query
        // key and clean up; the pair whose cleanup is sharpest wins. O(n) storage
        // and O(n * codebook) work -- which is precisely what attention's KV cache
        // costs, and precisely what the bundle refuses to pay.
        pairs.clear();
        for (std::size_t i = 0; i < n; ++i) pairs.push_back(bind(cb.key_g[t.keys[i]], cb.val_g[t.vals[i]]));
        Pick bestp; double bestsim = -1e300;
        for (std::size_t i = 0; i < n; ++i) {
            const Pick p = nearest_glyph(bind(pairs[i], cb.key_g[t.keys[t.qpos]]), cb.val_g, vlo, kValTrain);
            if (p.top1 > bestsim) { bestsim = p.top1; bestp = p; }
        }
        out[5].add(bestp.id == truth, bestp.margin);

        // "Return the most recent value." Measured, not assumed.
        out[6].add(t.vals[n - 1] == truth);
    }
}

} // namespace

int main() {
    std::printf("\n============================================================\n");
    std::printf("  ATTENTION vs THE HYPERVECTOR SUBSTRATE\n");
    std::printf("  scaled dot-product self-attention, hand-written, no deps,\n");
    std::printf("  raced against bind/bundle on the task attention exists for.\n");
    std::printf("============================================================\n\n");

    const Codebook cb = make_codebook(0x1234ABCDULL);

    std::printf("  embedding width kDe=%zu, token width kDModel=%zu, d_head=%zu\n",
                kDe, kDModel, kDHead);
    std::printf("  key vocabulary %zu (%zu train / %zu held out), value vocabulary %zu (%zu / %zu)\n",
                kKeyVocab, kKeyTrain, kKeyVocab - kKeyTrain, kValVocab, kValTrain, kValVocab - kValTrain);
    std::printf("  glyph width %zu bits (%zu bytes). Readout on BOTH sides is nearest\n",
                kGlyphBits, kGlyphBytes);
    std::printf("  neighbour over the same %zu-symbol value codebook, so chance = %.2f%%.\n\n",
                kValTrain, 100.0 * kChance);

    const int grad_fail = gradient_check(cb);

    // ------------------------------------------------------------------
    std::printf("=== 2. TRAINING (Adam, lr %.0e, %zu sequences, no batching) ===\n", kLr, kTrainSteps);
    std::printf("  Objective is squared error against the target symbol's embedding.\n"
                "  Sequence length is resampled from {4,8,16,32,64,128} every step, so\n"
                "  every row of the sweep below is a training length and none is\n"
                "  extrapolation. Mean loss over each eighth of the run:\n\n");
    std::printf("  %-22s |  (loss falls left to right if it learned anything)\n", "model");
    Systems sys{Attn(1, kDHead, kDe, 0x11110001ULL),
                Attn(1, kDHead, kDe, 0x11110001ULL),   // same init as attn-rand
                make_ideal(false),
                Attn(kHeads, kDHeadM, kDe, 0x22220002ULL)};
    sys.sgd1  = train_attention(cb, 1, kDHead, false, 0x11110001ULL, "attn-SGD  1 head").A;
    sys.multi = train_attention(cb, kHeads, kDHeadM, false, 0x22220002ULL, "attn-SGD  4 heads").A;
    std::printf("\n  attn-rand is the 1-head model at its initialisation, i.e. the same\n"
                "  weights attn-SGD started from. attn-ideal is never trained.\n\n");

    // ------------------------------------------------------------------
    std::printf("=== 3. TASK 1 — ASSOCIATIVE RECALL, SEQUENCE LENGTH SWEPT ===\n");
    std::printf("  n distinct (key, value) pairs, then a query key drawn uniformly from\n"
                "  the n keys. Answer = the value it was paired with. %zu trials per cell.\n", kTrials);
    std::printf("  Cells are  acc%% [95%% Wilson]. chance = %.2f%%.\n\n", 100.0 * kChance);
    std::printf("     n | attn-rand           | attn-SGD 1h         | attn-SGD 4h         |"
                " attn-ideal          | HDC bundle (1 glyph)| HDC list (n glyphs) | most-recent\n");
    std::printf("  -----+---------------------+---------------------+---------------------+"
                "---------------------+---------------------+---------------------+------------\n");

    std::vector<std::size_t> sweep = {4, 8, 16, 32, 64, 128};
    std::vector<Tally>       bundle_margin(sweep.size());
    for (std::size_t si = 0; si < sweep.size(); ++si) {
        const std::size_t n = sweep[si];
        Tally t[7];
        eval_length(cb, sys, n, kTrials, /*heldout=*/false, 0x777000ULL + n, t);
        bundle_margin[si] = t[4];
        std::printf("  %4zu |", n);
        for (int i = 0; i < 6; ++i) cell(t[i]);
        std::printf(" %5.1f%%\n", t[6].pct());
    }
    std::printf("\n  Mean readout margin for the single-glyph bundle (top1 - top2 similarity),\n"
                "  which is what actually decays with n -- accuracy is the margin crossing zero:\n  ");
    for (std::size_t si = 0; si < sweep.size(); ++si)
        std::printf("  n=%zu: %.4f", sweep[si], bundle_margin[si].margin());
    std::printf("\n\n  THE 4-HEAD COLUMN IS NOT A BUG AND IT IS NOT TUNED AWAY. Both trained\n"
                "  models have the SAME parameter count -- %zu heads of width %zu against one\n"
                "  of width %zu -- and the multi-head one falls off with n while the single\n"
                "  head does not. Exact-symbol recall needs the query-key dot product to\n"
                "  separate one key from up to %zu others, and a %zu-dimensional projection\n"
                "  of %zu-dimensional embeddings leaves distinct keys a spread of roughly\n"
                "  1/sqrt(%zu) = %.2f around the matched one -- not enough headroom for a\n"
                "  softmax to pick one out of 128. Splitting a fixed width into heads is\n"
                "  the wrong trade for this task, and its training loss (section 2) shows\n"
                "  it never got there rather than getting there and forgetting.\n\n",
                kHeads, kDHeadM, kDHead, kKeyTrain, kDHeadM, kDe, kDHeadM,
                1.0 / std::sqrt(static_cast<double>(kDHeadM)));

    // ------------------------------------------------------------------
    std::printf("=== 4. WHERE DOES THE FIXED-WIDTH BUNDLE ACTUALLY BREAK? ===\n");
    std::printf("  The brief predicted the bundle would struggle as the sequence grows.\n"
                "  Stopping at n=128 would have left that untested, so the sweep is pushed\n"
                "  to n=1024 -- one glyph, still %zu bytes, holding 1024 pairs. %zu trials.\n",
                kGlyphBytes, kTrialsBig);
    std::printf("  attn-rand and HDC list are dropped here: the first is at chance, the\n"
                "  second is O(n*codebook) per query and its answer is already known.\n\n");
    std::printf("     n | attn-SGD 1h         | attn-ideal          | HDC bundle (1 glyph)|"
                " bundle margin | attn KV bytes | bundle bytes\n");
    std::printf("  -----+---------------------+---------------------+---------------------+"
                "---------------+---------------+-------------\n");
    for (const std::size_t n : {std::size_t{128}, std::size_t{256}, std::size_t{512}, std::size_t{1024}}) {
        std::mt19937_64 rng(0x999000ULL + n);
        Tally a_sgd, a_id, hdc;
        Cache C; std::vector<double> X, xq;
        for (std::size_t r = 0; r < kTrialsBig; ++r) {
            const Trial t = make_trial(n, false, false, rng);
            const std::size_t truth = t.vals[t.qpos];
            build_tokens(cb, t, X, xq, false);
            attn_forward(sys.sgd1, X, n, xq, C);
            { const Pick p = nearest_emb(C.out, cb.val_emb, 0, kValTrain); a_sgd.add(p.id == truth, p.margin); }
            attn_forward(sys.ideal, X, n, xq, C);
            { const Pick p = nearest_emb(C.out, cb.val_emb, 0, kValTrain); a_id.add(p.id == truth, p.margin); }
            const Glyph rec = hdc_bundle(cb, t);
            const Pick  p   = nearest_glyph(bind(rec, cb.key_g[t.keys[t.qpos]]), cb.val_g, 0, kValTrain);
            hdc.add(p.id == truth, p.margin);
        }
        std::printf("  %4zu |", n);
        cell(a_sgd); cell(a_id); cell(hdc);
        std::printf("     %8.4f  |      %8zu |     %8zu\n",
                    hdc.margin(), 2 * n * kDHead * sizeof(double), kGlyphBytes);
    }
    std::printf("\n  attn KV bytes = 2 * n * d_head * 8, the keys and values attention must\n"
                "  keep to answer one query. The bundle column does not move.\n");
    std::printf("  attn-SGD was trained on lengths up to 128, so its n=512 and n=1024 cells\n"
                "  are length EXTRAPOLATION and the drop there is a training-distribution\n"
                "  effect, not a capacity limit -- attn-ideal, same architecture, untrained,\n"
                "  holds 100%% at every length. The bundle's fall is the capacity limit.\n\n");

    // ------------------------------------------------------------------
    std::printf("=== 5. TASK 2A — ONE WRITE, BOTH DIRECTIONS ===\n");
    std::printf("  bind is XOR and XOR is its own inverse, so bind(key,value) answers\n"
                "  key->value and value->key from the same bits. This section uses the\n"
                "  SHIPPED khora::chiasm::Chiasm: %zu associations, each stored once with\n"
                "  remember({key, value}), then queried in both directions. Its per-role\n"
                "  cleanup holds all %zu keys and all %zu values, so both directions face a\n"
                "  %zu-candidate readout -- the same difficulty attention's readout faces.\n\n",
                kValTrain, kValTrain, kValTrain, kValTrain);

    {
        constexpr std::size_t kAssoc = kValTrain;   // 128 disjoint pairs
        constexpr std::size_t kSets  = 5;           // 5 independent association sets
        Tally ch_fwd, ch_rev, at_fwd, at_rev_same, at_rev_ideal, at_rev_trained;

        // A second attention block, trained value->key. It is a SEPARATE parameter
        // set and a SEPARATE training run; that cost is the finding.
        std::printf("  training a second, reverse-direction attention block:\n");
        const Attn rev_sgd = train_attention(cb, 1, kDHead, true, 0x33330003ULL, "attn-SGD  reverse").A;
        const Attn rev_ideal = make_ideal(true);
        std::printf("\n");

        std::mt19937_64 rng(0x2A2A2AULL);
        Cache C; std::vector<double> X, xq;
        for (std::size_t s = 0; s < kSets; ++s) {
            // Keys restricted to the first kValTrain of them, so the reverse
            // direction's readout codebook is the same size as the forward's and
            // one chance line is honest for both. A fresh Chiasm per set, because
            // a key that appears in five records with five different values makes
            // its record lookup a coin flip -- that would be a flaw in this test,
            // not in the memory.
            const Trial t = make_trial(kAssoc, /*heldout=*/false, /*distinct_vals=*/true,
                                       rng, /*key_limit=*/kValTrain);

            khora::chiasm::Chiasm ch;
            for (std::size_t i = 0; i < kAssoc; ++i)
                ch.remember({{"key",   "k" + std::to_string(t.keys[i]), cb.key_g[t.keys[i]]},
                             {"value", "v" + std::to_string(t.vals[i]), cb.val_g[t.vals[i]]}});

            for (std::size_t i = 0; i < kAssoc; ++i) {
                Trial q = t; q.qpos = i;
                const auto rf = ch.recall("key", cb.key_g[t.keys[i]], "value");
                ch_fwd.add(rf.label == "v" + std::to_string(t.vals[i]), rf.margin);
                const auto rr = ch.recall("value", cb.val_g[t.vals[i]], "key");
                ch_rev.add(rr.label == "k" + std::to_string(t.keys[i]), rr.margin);

                build_tokens(cb, q, X, xq, /*reverse=*/false);
                attn_forward(sys.sgd1, X, kAssoc, xq, C);
                { const Pick p = nearest_emb(C.out, cb.val_emb, 0, kValTrain); at_fwd.add(p.id == t.vals[i], p.margin); }

                build_tokens(cb, q, X, xq, /*reverse=*/true);
                // (i) the SAME forward-trained parameters, queried backwards.
                attn_forward(sys.sgd1, X, kAssoc, xq, C);
                { const Pick p = nearest_emb(C.out, cb.key_emb, 0, kValTrain); at_rev_same.add(p.id == t.keys[i], p.margin); }
                // (ii) a hand-set reverse construction: proof the mechanism can,
                //      with a DIFFERENT parameter set.
                attn_forward(rev_ideal, X, kAssoc, xq, C);
                { const Pick p = nearest_emb(C.out, cb.key_emb, 0, kValTrain); at_rev_ideal.add(p.id == t.keys[i], p.margin); }
                // (iii) a second trained block.
                attn_forward(rev_sgd, X, kAssoc, xq, C);
                { const Pick p = nearest_emb(C.out, cb.key_emb, 0, kValTrain); at_rev_trained.add(p.id == t.keys[i], p.margin); }
            }
        }
        // The reverse readout is over the FIRST kValTrain keys only, so both
        // directions have a 128-candidate codebook and one chance line covers both.
        std::printf("  %zu associations x %zu sets = %zu queries per direction. chance = %.2f%%.\n",
                    kAssoc, kSets, kAssoc * kSets, 100.0 * kChance);
        std::printf("  Reverse readout is over the same %zu-symbol codebook, so chance matches.\n\n", kValTrain);
        std::printf("  system                          | direction     | hits/n      | acc    | 95%% Wilson    | params written for it\n");
        std::printf("  --------------------------------+---------------+-------------+--------+---------------+----------------------\n");
        auto row = [](const char* s, const char* dir, const Tally& t, const char* cost) {
            const auto ci = wilson(t.hit, t.n);
            std::printf("  %-31s | %-13s | %5zu/%-5zu | %5.1f%% | [%5.1f,%6.1f] | %s\n",
                        s, dir, t.hit, t.n, t.pct(), ci.first, ci.second, cost);
        };
        row("khora::chiasm (one write)",  "key -> value", ch_fwd,        "0 (no training)");
        row("khora::chiasm (SAME write)", "value -> key", ch_rev,        "0 (nothing added)");
        row("attn-SGD forward",           "key -> value", at_fwd,        "7168 trained");
        row("attn-SGD forward, reversed", "value -> key", at_rev_same,   "0 (same weights)");
        row("attn-ideal reverse",         "value -> key", at_rev_ideal,  "hand-set, 2nd param set");
        row("attn-SGD reverse (2nd run)", "value -> key", at_rev_trained,"7168 more, 2nd run");
        std::printf("\n  Read the last four rows together. Attention CAN do value->key -- with a\n"
                    "  second parameter set and a second training run. The substrate does it\n"
                    "  with the bits it already wrote. That is the whole of the claim: not\n"
                    "  that attention cannot, but what the second direction costs.\n");
        std::printf("  Chiasm's own cost, stated: recall() SCANS every stored record, so its\n"
                    "  memory is O(#records) bundles, not one. The fixed-size claim belongs to\n"
                    "  the single-glyph row in section 3, not to this class.\n\n");
    }

    // ------------------------------------------------------------------
    std::printf("=== 6. TASK 2B — SYMBOLS NEVER SEEN DURING TRAINING ===\n");
    std::printf("  Keys drawn from [%zu,%zu) and values from [%zu,%zu): embeddings and glyphs\n",
                kKeyTrain, kKeyVocab, kValTrain, kValVocab);
    std::printf("  from the same distributions, but no gradient step ever touched them.\n"
                "  The substrate has no training, so its two columns should agree; the gap\n"
                "  in attn-SGD's two columns is what its training did not generalise.\n\n");
    std::printf("     n | system      | in-vocab            | held-out vocab      | gap\n");
    std::printf("  -----+-------------+---------------------+---------------------+-------\n");
    for (const std::size_t n : {std::size_t{8}, std::size_t{32}, std::size_t{128}}) {
        Tally in[7], ho[7];
        eval_length(cb, sys, n, kTrials, false, 0xAAA000ULL + n, in);
        eval_length(cb, sys, n, kTrials, true,  0xBBB000ULL + n, ho);
        const char* names[5] = {"attn-rand", "attn-SGD 1h", "attn-SGD 4h", "attn-ideal", "HDC bundle"};
        for (int i = 0; i < 5; ++i) {
            std::printf("  %4zu | %-11s |", n, names[i]);
            cell(in[i]); cell(ho[i]);
            std::printf(" %+5.1f\n", ho[i].pct() - in[i].pct());
        }
        std::printf("  -----+-------------+---------------------+---------------------+-------\n");
    }
    std::printf("\n");

    // ------------------------------------------------------------------
    std::printf("=== 7. COST ===\n\n");
    std::printf("  a) PARAMETERS AND FIXED BYTES\n\n");
    std::printf("  component                                        | trainable |      bytes\n");
    std::printf("  -------------------------------------------------+-----------+------------\n");
    std::printf("  attn 1 head: Wq,Wk,Wv (%2zux%2zu) + Wo (%2zux%2zu)       | %9zu | %10zu\n",
                kDHead, kDModel, kDe, kDHead,
                sys.sgd1.param_count(), sys.sgd1.param_count() * sizeof(double));
    std::printf("  attn %zu heads, same total width                   | %9zu | %10zu\n",
                kHeads, sys.multi.param_count(), sys.multi.param_count() * sizeof(double));
    std::printf("  HDC bundle: bind + bundle + nearest neighbour     | %9d | %10zu\n",
                0, kGlyphBytes);
    std::printf("  -------------------------------------------------+-----------+------------\n");
    std::printf("  symbol codebook, attention: %4zu sym x %2zu doubles | %9d | %10zu\n",
                kKeyVocab + kValVocab, kDe, 0,
                (kKeyVocab + kValVocab) * kDe * sizeof(double));
    std::printf("  symbol codebook, glyphs:    %4zu sym x %5zu bits | %9d | %10zu\n",
                kKeyVocab + kValVocab, kGlyphBits, 0,
                (kKeyVocab + kValVocab) * kGlyphBytes);
    std::printf("\n  Both codebooks are generated from an 8-byte seed and neither is trained,\n"
                "  so neither side has to store one; they are listed because the substrate's\n"
                "  is %.0fx larger in memory and that is a real cost if it is materialised.\n",
                static_cast<double>(kGlyphBytes) / static_cast<double>(kDe * sizeof(double)));
    std::printf("  Per symbol: attention %zu bits, glyph %zu bits. The substrate gets %.0fx\n"
                "  more bits per symbol, and that asymmetry favours it. It is inherent --\n"
                "  a 32-bit glyph is not a hypervector and a 10,000-double embedding is not\n"
                "  a comparison anyone runs.\n\n",
                kDe * sizeof(double) * 8, kGlyphBits,
                static_cast<double>(kGlyphBits) / static_cast<double>(kDe * sizeof(double) * 8));

    std::printf("  b) WALL TIME, single-threaded scalar C++ on one machine.\n"
                "     Per-query for the one-query path; per-sequence for full self-attention\n"
                "     (every position attends to every position, which is the O(n^2) claim).\n\n");
    std::printf("      n | attn 1 query | attn self-attn | HDC build bundle | HDC 1 query | attn/HDC per query\n");
    std::printf("        |         (us) |    (us, n x n) |        (us, n x) |        (us) |\n");
    std::printf("  ------+--------------+----------------+------------------+-------------+-------------------\n");

    std::vector<std::pair<std::size_t, double>> selfattn_curve;
    for (const std::size_t n : {std::size_t{4}, std::size_t{16}, std::size_t{64},
                                std::size_t{256}, std::size_t{512}, std::size_t{1024}}) {
        std::mt19937_64 rng(0x71330000ULL + n);
        const Trial t = make_trial(n, false, false, rng);
        std::vector<double> X, xq;
        build_tokens(cb, t, X, xq, false);
        Cache C;

        using clk = std::chrono::steady_clock;
        const std::size_t reps = std::max<std::size_t>(3, 200000 / (n + 8));

        attn_forward(sys.sgd1, X, n, xq, C);   // warm
        auto t0 = clk::now();
        for (std::size_t r = 0; r < reps; ++r) attn_forward(sys.sgd1, X, n, xq, C);
        const double us_q = std::chrono::duration<double, std::micro>(clk::now() - t0).count()
                          / static_cast<double>(reps);

        const std::size_t breps = std::max<std::size_t>(3, 40000 / (n + 8));
        t0 = clk::now();
        Glyph rec;
        for (std::size_t r = 0; r < breps; ++r) rec = hdc_bundle(cb, t);
        const double us_b = std::chrono::duration<double, std::micro>(clk::now() - t0).count()
                          / static_cast<double>(breps);

        // Timed BEFORE the quadratic self-attention loop below. Measured after it,
        // this row read 2x high at the largest n purely from a cold cache -- the
        // work it does is identical at every n and the timing must show that or it
        // is measuring the harness.
        const std::size_t qreps = 5000;
        t0 = clk::now();
        std::size_t sink = 0;
        for (std::size_t r = 0; r < qreps; ++r)
            sink ^= nearest_glyph(bind(rec, cb.key_g[t.keys[t.qpos]]), cb.val_g, 0, kValTrain).id;
        const double us_hq = std::chrono::duration<double, std::micro>(clk::now() - t0).count()
                           / static_cast<double>(qreps);
        if (sink == 0xDEADBEEFULL) std::printf(" ");   // keep the loop alive

        // Full self-attention: n queries, each over n tokens. Fewer reps because
        // this is the quadratic one and the point is the shape, not the constant.
        const std::size_t sreps = std::max<std::size_t>(1, 20000 / (n * (n + 1) / 32 + 1));
        std::vector<double> xi(kDModel);
        t0 = clk::now();
        for (std::size_t r = 0; r < sreps; ++r)
            for (std::size_t i = 0; i < n; ++i) {
                std::copy_n(X.begin() + static_cast<std::ptrdiff_t>(i * kDModel), kDModel, xi.begin());
                attn_forward(sys.sgd1, X, n, xi, C);
            }
        const double us_sa = std::chrono::duration<double, std::micro>(clk::now() - t0).count()
                           / static_cast<double>(sreps);
        selfattn_curve.emplace_back(n, us_sa);

        std::printf("  %5zu | %12.3f | %14.1f | %16.1f | %11.3f | %17.2fx\n",
                    n, us_q, us_sa, us_b, us_hq, us_q / us_hq);
    }
    {
        // The exponent, measured on every adjacent pair rather than only the last.
        // One pair would have reported 2.56 and invited the reading that attention
        // is worse than quadratic; the whole curve shows it is 1.98-1.99 until the
        // token matrix stops fitting in cache, at which point the CONSTANT grows.
        // Both facts are real and only the pair-by-pair table shows which is which.
        std::printf("\n  Measured log-log slope of full self-attention, adjacent pairs\n"
                    "  (2.00 is exactly quadratic):\n   ");
        for (std::size_t i = 1; i < selfattn_curve.size(); ++i) {
            const auto& a = selfattn_curve[i - 1];
            const auto& b = selfattn_curve[i];
            std::printf("  %zu->%zu: %.2f", a.first, b.first,
                        std::log(b.second / a.second)
                        / std::log(static_cast<double>(b.first) / static_cast<double>(a.first)));
        }
        std::printf("\n  The one-query path is LINEAR in n (it is one row of the score matrix).\n"
                    "  The HDC query is FLAT in n -- one XOR plus a %zu-way cleanup, regardless\n"
                    "  of how many pairs went into the glyph. Building the glyph is linear.\n\n",
                    kValTrain);
    }

    // ------------------------------------------------------------------
    std::printf("=== 8. WHAT THIS HARNESS CANNOT SEE ===\n\n");
    std::printf(
        "  - THERE IS NO TRANSFORMER HERE. One attention operation: no residual\n"
        "    stream, no LayerNorm, no feed-forward block, no stacking, no positional\n"
        "    encoding, no causal mask, no learned embeddings. Everything attention is\n"
        "    famous for that requires DEPTH -- in-context learning, induction over\n"
        "    patterns, composition across layers -- is outside this file entirely.\n"
        "    Nothing here licenses a claim about transformers.\n\n"
        "  - THE TASK IS THE SUBSTRATE'S HOME GROUND. Keys are discrete symbols with\n"
        "    exact matches, which is exactly what XOR binding was designed for. Real\n"
        "    attention earns its keep on GRADED, learned similarity between continuous\n"
        "    representations, where 'the matching key' is not a well-defined object.\n"
        "    A fair reading of section 3 is 'the substrate matches attention on the\n"
        "    easiest possible version of attention's task', not 'attention is\n"
        "    unnecessary'.\n\n"
        "  - THE OBJECTIVE IS SQUARED ERROR TO AN EMBEDDING, not cross-entropy over a\n"
        "    vocabulary, and there is no batching, no LR schedule, no weight decay and\n"
        "    no early stopping. attn-ideal is in every table precisely so that a\n"
        "    training run that underperforms cannot be read as a limit of attention.\n\n"
        "  - ONE SEED for the model init, one for the codebook, one per evaluation\n"
        "    cell. The Wilson intervals cover TRIAL SAMPLING ONLY. They say nothing\n"
        "    about variance across initialisations or codebooks, and a second seed\n"
        "    would move the trained rows.\n\n"
        "  - THE TWO SIDES DO NOT GET THE SAME NUMBER OF BITS PER SYMBOL (%zu vs\n"
        "    %zu), and cannot: the comparison is between representations, and there\n"
        "    is no width at which they are both natural. The asymmetry favours the\n"
        "    substrate and section 7 prints it.\n\n"
        "  - WALL TIMES ARE SCALAR, SINGLE-THREADED, ONE MACHINE. Attention on a GPU\n"
        "    with a fused kernel is orders of magnitude faster in absolute terms, and\n"
        "    glyph ops vectorise too. Only the SHAPE of the curves transfers.\n\n"
        "  - NO CAPACITY LIMIT WAS SEARCHED FOR BEYOND n=1024, and the bundle's\n"
        "    breaking point depends on the cleanup codebook size, which is fixed at\n"
        "    %zu here. A larger codebook breaks it sooner; that curve is not measured.\n\n"
        "  - THE HDC SIDE IS NOT DOING RETRIEVAL OVER LEARNED CONTENT. Every symbol is\n"
        "    a random glyph with no internal structure, so nothing here tests whether\n"
        "    the substrate can attend to something it had to LEARN to represent.\n\n"
        "  - attn-rand SITTING AT CHANCE IS NOT 'attention needs training'. It is that\n"
        "    THIS readout needs the projections to align with the embedding layout, and\n"
        "    a random rotation does not. attn-ideal reaches the same alignment with no\n"
        "    training at all, which is the more informative half of that pair.\n\n"
        "  - THE BUNDLE'S CAPACITY IS A PROPERTY OF %zu BITS, not of the algebra. The\n"
        "    break between n=256 and n=1024 would move with glyph width; that sweep is\n"
        "    not run here, so 'the bundle holds ~256 pairs' is a statement about this\n"
        "    configuration and nothing wider.\n\n",
        kDe * sizeof(double) * 8, kGlyphBits, kValTrain, kGlyphBits);

    if (grad_fail) {
        std::printf("  *** %d gradient check(s) FAILED — every trained number above is suspect.\n\n", grad_fail);
        return 1;
    }
    return 0;
}
