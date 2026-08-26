// CATASTROPHIC FORGETTING, MEASURED ON BOTH PARADIGMS AT ONCE.
//
// Train a gradient-trained network on task A, then on task B without ever
// showing it A again, and its accuracy on A collapses. That failure is famous,
// it is the reason continual-learning research exists, and nothing in this tree
// had ever checked whether the hypervector substrate is actually free of it.
//
// The structural argument for immunity is one sentence: storing an association
// is bundling a new term, and bundling never modifies the terms already there.
// A gradient step, by contrast, moves every weight in the network, including the
// ones that were carrying task A. If the argument is right, the retention curve
// for the substrate is flat where the network's falls off a cliff.
//
// WHAT IS RUN. T tasks arrive in a stream. Each task is a K-way classification
// over kDim-dimensional real vectors drawn from K fresh Gaussian blobs, so every
// task is a genuinely new problem with its own class labels. Each system sees
// task t's training data once, in order, and NEVER revisits an earlier task.
// After every task, all systems are re-measured on the held-out test set of
// EVERY task seen so far.
//
// FOUR SYSTEMS, because two would not have been enough to know what caused what:
//
//   MLP (SGD)     khora::descent::Mlp, one shared head over all T*K classes,
//                 trained sequentially. The paradigm under indictment.
//   HDC proto     One prototype glyph per class = bundle of that class's
//                 training encodings. Built when its task arrives, never touched
//                 again. Classify by nearest prototype.
//   HDC 1-glyph   ONE glyph for everything: bundle over all training examples of
//                 bind(encode(x), class_glyph(y)). Fixed size no matter how much
//                 data arrives. Classify by unbinding the query and cleaning up
//                 against the class glyphs. This is the variant that can actually
//                 saturate, and finding where it does is the point of part B.
//   1-NN stored   Keep every training encoding, answer with the nearest one. The
//                 DUMB BASELINE FOR THE HDC SIDE: it is trivially immune to
//                 forgetting because it is a list. If the substrate's retention
//                 is no better than this, the retention is not evidence for
//                 hyperdimensional anything, it is evidence for not overwriting.
//
// TWO REFERENCES THAT MAKE THE COMPARISON FAIR, and without which it is worthless:
//
//   chance        Printed per row, because it moves: prediction is restricted to
//                 the classes seen so far, so chance is 1/(K*(t+1)), not a
//                 constant. Comparing a late row against an early row without
//                 this is reading the denominator as a result.
//   MLP joint     The same network, same architecture, same seed, same number of
//                 passes over each example -- but trained on ALL tasks shuffled
//                 together. This is what the MLP could do if it were allowed to
//                 revisit data. Sequential-vs-joint is the size of the forgetting;
//                 sequential-vs-HDC without it would just be an unfair fight.
//
// TWO PROTOCOLS, both reported, because they measure different things and the
// literature uses both:
//
//   class-incremental  No task ID at test time. argmax over every class seen.
//                      This is the hard, honest setting.
//   task-aware         The task ID is given, argmax restricted to that task's K
//                      classes. This is the MLP's best case and it is included
//                      precisely because it is the MLP's best case.
//
// WHAT THIS HARNESS CANNOT SEE:
//
//   - The tasks are isotropic Gaussian blobs with independently drawn centres.
//     Real task streams share structure, and shared structure is exactly what a
//     network can transfer and a prototype memory cannot. These numbers say
//     nothing about transfer, only about retention.
//   - Nothing here is an anti-forgetting method. EWC, replay buffers, and
//     parameter isolation all exist and all recover a large part of what the
//     plain MLP loses. The MLP row is plain SGD, which is the correct control
//     for "does the substrate get this for free", and the wrong control for
//     "is the substrate better than the state of the art". It is not a claim
//     about the state of the art.
//   - The MLP reads raw doubles; the HDC side reads the same vectors quantised
//     to kLevels levels per dimension. That asymmetry favours the MLP, and it is
//     inherent -- the substrate is binary.
//   - One seed, one stream order. The retention curves are single runs; the
//     Wilson intervals cover test-set sampling, NOT the variance across task
//     streams or initialisations. A second seed would move the levels.
//   - Learning rate, epochs and batch size for the MLP were fixed at plausible
//     values (0.10 / 300 / 16, matching descent_test's settings) before the
//     first run and not revisited. A tuned MLP forgets somewhat less. Hidden
//     width IS swept, in section 7, because the first run raised the obvious
//     objection that 64 units for 10 four-blob tasks is simply too much slack
//     for interference to happen; every width carries its own joint reference
//     so a width that cannot learn is never mistaken for a width that forgot.
//   - "Fresh" accuracy is printed for every task and every width so it is
//     visible that each system did learn each task before losing it. Forgetting
//     something you never learned is not forgetting.
//   - The one-glyph memory's accuracy is NOT monotone in the number of tasks.
//     It is re-sealed from the whole term set at every checkpoint and the
//     interference pattern between a query and 40-plus class glyphs is
//     idiosyncratic, so it wanders by ten or twenty points between adjacent
//     rows. The trend is the result; any single row is not.

#include "khora/descent/descent.hpp"
#include "khora/lattice/glyph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <vector>

using khora::descent::Mlp;
using khora::lattice::Glyph;
using khora::lattice::bind;
using khora::lattice::bundle;
using khora::lattice::kGlyphBits;
using khora::lattice::kGlyphWords;
using khora::lattice::position_glyph;

namespace {

// --- fixed configuration ----------------------------------------------------
constexpr std::size_t kDim           = 32;   // features per example
constexpr std::size_t kClassesPerTsk = 4;
constexpr std::size_t kTrainPerClass = 32;
constexpr std::size_t kLevels        = 16;   // quantisation levels per feature
constexpr double      kClip          = 2.5;  // feature range mapped onto levels
constexpr double      kBlobSigma     = 0.35; // within-class spread; centres ~ N(0,1)
constexpr std::size_t kHidden        = 64;
constexpr std::size_t kEpochs        = 300;
constexpr double      kLr            = 0.10;
constexpr std::size_t kBatch         = 16;

constexpr std::size_t kGlyphBytes = kGlyphWords * sizeof(std::uint64_t);

// 95% Wilson interval on a proportion, as percentages. Same helper as
// substrate_bench.cpp and extraction_bench.cpp -- at the near-zero rates a
// forgotten task produces, the normal approximation is simply wrong.
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
    void   add(bool ok) noexcept { hit += ok ? 1u : 0u; ++n; }
    void   merge(const Tally& o) noexcept { hit += o.hit; n += o.n; }
    double pct() const noexcept { return n ? 100.0 * static_cast<double>(hit) / static_cast<double>(n) : 0.0; }
};

void cell(const Tally& t) {
    const auto ci = wilson(t.hit, t.n);
    std::printf(" %5.1f%% [%4.1f,%5.1f] |", t.pct(), ci.first, ci.second);
}

// --- the task stream --------------------------------------------------------
//
// PERMUTED FEATURES, and the first version of this bench got it wrong.
//
// The obvious generator draws fresh blob centres per task from N(0,1)^kDim. That
// was tried first and it is a broken experiment: in 32 dimensions two
// independent centres sit ~8 apart while a blob is ~2 wide, so every task lands
// in its own empty region of input space and the tasks NEVER COMPETE for the
// same weights. Measured, that generator gave the sequential MLP 100% task-aware
// retention on task 1 after all 10 tasks -- no forgetting at all, because there
// was no interference to forget from. A benchmark on which the failure cannot
// occur cannot be evidence that something is immune to it.
//
// So: ONE set of K blob centres, shared by every task, and each task applies its
// own random permutation of the kDim feature axes. This is the permuted-MNIST
// construction, which is the standard catastrophic-forgetting benchmark and is
// standard for exactly this reason -- every task has the identical input
// marginal, occupies the identical region, and demands a DIFFERENT mapping from
// those inputs to labels. The shared weights have to be reused, so learning task
// t+1 has to overwrite something.
//
// The class ids stay global (task t owns ids 4t..4t+3), so a single head over
// all T*K classes is well defined and class-incremental scoring is meaningful.
struct Task {
    std::vector<std::vector<double>> train_x, test_x;
    std::vector<std::size_t>         train_y, test_y;  // GLOBAL class ids
};

std::vector<Task> make_tasks(std::size_t T, std::size_t test_per_class, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> centre(0.0, 1.0), noise(0.0, kBlobSigma);

    std::vector<std::vector<double>> base(kClassesPerTsk, std::vector<double>(kDim));
    for (auto& c : base)
        for (double& v : c) v = centre(rng);

    std::vector<Task> tasks(T);
    std::vector<std::size_t> perm(kDim);
    for (std::size_t t = 0; t < T; ++t) {
        std::iota(perm.begin(), perm.end(), std::size_t{0});
        std::shuffle(perm.begin(), perm.end(), rng);
        for (std::size_t k = 0; k < kClassesPerTsk; ++k) {
            const std::size_t gid = t * kClassesPerTsk + k;
            auto emit = [&](std::vector<std::vector<double>>& xs, std::vector<std::size_t>& ys,
                            std::size_t count) {
                for (std::size_t i = 0; i < count; ++i) {
                    std::vector<double> x(kDim);
                    for (std::size_t j = 0; j < kDim; ++j)
                        x[j] = base[k][perm[j]] + noise(rng);
                    xs.push_back(std::move(x));
                    ys.push_back(gid);
                }
            };
            emit(tasks[t].train_x, tasks[t].train_y, kTrainPerClass);
            emit(tasks[t].test_x,  tasks[t].test_y,  test_per_class);
        }
    }
    return tasks;
}

// --- the encoder ------------------------------------------------------------
//
// Standard HDC scalar encoding. Level glyphs are a linear chain: lvl[0] is
// random and each step flips a fixed slice of fresh bits, so lvl[0] and
// lvl[kLevels-1] are orthogonal and similarity falls linearly with level
// distance. Without that the substrate would treat 0.1 and 0.2 as unrelated and
// no blob would ever be a blob.
//
// A feature vector becomes bundle over i of bind(position_glyph(i+1), lvl[q_i]).
// position_glyph(0) is the identity glyph, hence the +1.
class Codebook {
public:
    explicit Codebook(std::uint64_t seed) {
        levels_.reserve(kLevels);
        Glyph cur = Glyph::random(seed);
        levels_.push_back(cur);
        std::vector<std::uint32_t> idx(kGlyphBits);
        std::iota(idx.begin(), idx.end(), 0u);
        std::shuffle(idx.begin(), idx.end(), std::mt19937_64(seed ^ 0x5EEDULL));
        const std::size_t per = (kGlyphBits / 2) / (kLevels - 1);
        std::size_t at = 0;
        for (std::size_t j = 1; j < kLevels; ++j) {
            for (std::size_t p = 0; p < per; ++p) cur.flip_bit(idx[at++]);
            levels_.push_back(cur);
        }
    }

    Glyph encode(const std::vector<double>& x) const {
        std::vector<Glyph> parts;
        parts.reserve(kDim);
        for (std::size_t i = 0; i < kDim; ++i)
            parts.push_back(bind(position_glyph(i + 1), levels_[quant(x[i])]));
        return bundle(std::span<const Glyph>(parts));
    }

    // The encoder is part of the model and its bytes are counted as such. The
    // position glyphs come from a shared static 256-entry table in the lattice;
    // only the kDim actually used are charged here.
    static std::size_t bytes() { return (kLevels + kDim) * kGlyphBytes; }

private:
    static std::size_t quant(double v) {
        const double c = std::clamp(v, -kClip, kClip);
        const double t = (c + kClip) / (2.0 * kClip);
        const auto   q = static_cast<std::size_t>(t * static_cast<double>(kLevels - 1) + 0.5);
        return std::min(q, kLevels - 1);
    }
    std::vector<Glyph> levels_;
};

// --- system 2: one prototype glyph per class --------------------------------
//
// learn_class() is called exactly once per class, when its task arrives. No
// later call reads or writes an earlier prototype. That is the whole immunity
// argument, in code, and it is worth noticing how little there is of it.
struct ProtoHdc {
    std::vector<Glyph> proto;   // indexed by global class id, in arrival order

    void learn_class(const std::vector<Glyph>& enc) {
        proto.push_back(bundle(std::span<const Glyph>(enc)));
    }
    std::size_t predict(const Glyph& e, std::size_t lo, std::size_t hi) const {
        std::size_t best = lo;
        double best_s = -2.0;
        for (std::size_t c = lo; c < hi && c < proto.size(); ++c) {
            const double s = e.similarity(proto[c]);
            if (s > best_s) { best_s = s; best = c; }
        }
        return best;
    }
    std::size_t bytes() const { return proto.size() * kGlyphBytes + Codebook::bytes(); }
};

// --- system 3: everything in ONE glyph --------------------------------------
//
// memory = majority over all bind(encode(x_i), class_glyph(y_i)). Writes are
// append-only in exactly the sense the claim needs, and the footprint does not
// grow: a streaming implementation keeps one integer counter per bit position
// and emits the majority glyph, which is bit-identical to re-running bundle()
// over the accumulated terms. This bench keeps the terms and re-bundles, because
// that is three lines instead of thirty; the memory column charges the counter
// form, which is what an honest deployment would use.
struct OneGlyphHdc {
    std::vector<Glyph> terms;         // bookkeeping only, NOT charged as memory
    std::vector<Glyph> class_glyph;   // the cleanup codebook, one per class
    Glyph              memory;

    explicit OneGlyphHdc(std::size_t n_classes) {
        class_glyph.reserve(n_classes);
        for (std::size_t c = 0; c < n_classes; ++c)
            class_glyph.push_back(Glyph::random(0x0FFE7710ULL + 0x9E3779B9ULL * (c + 1)));
    }
    void add(const Glyph& e, std::size_t y) { terms.push_back(bind(e, class_glyph[y])); }
    void seal() { memory = bundle(std::span<const Glyph>(terms)); }

    // Unbind the query out of the superposition, then clean up against the class
    // glyphs. margin_out receives (best - runner-up), the number that says
    // whether a hit was a retrieval or a coin flip that landed.
    std::size_t predict(const Glyph& e, std::size_t lo, std::size_t hi,
                        double* margin_out = nullptr) const {
        const Glyph q = bind(e, memory);
        std::size_t best = lo;
        double b1 = -2.0, b2 = -2.0;
        for (std::size_t c = lo; c < hi; ++c) {
            const double s = q.similarity(class_glyph[c]);
            if (s > b1)      { b2 = b1; b1 = s; best = c; }
            else if (s > b2) { b2 = s; }
        }
        if (margin_out) *margin_out = b1 - b2;
        return best;
    }
    std::size_t bytes(std::size_t classes_seen) const {
        // one uint32 counter per bit + the sealed glyph + the class codebook.
        return kGlyphBits * sizeof(std::uint32_t) + kGlyphBytes
               + classes_seen * kGlyphBytes + Codebook::bytes();
    }
};

// --- system 4: keep everything, answer with the nearest ---------------------
struct StoreAll {
    std::vector<Glyph>       item;
    std::vector<std::size_t> label;

    void add(const Glyph& e, std::size_t y) { item.push_back(e); label.push_back(y); }
    std::size_t predict(const Glyph& e, std::size_t lo, std::size_t hi) const {
        std::size_t best = lo;
        double best_s = -2.0;
        for (std::size_t i = 0; i < item.size(); ++i) {
            if (label[i] < lo || label[i] >= hi) continue;
            const double s = e.similarity(item[i]);
            if (s > best_s) { best_s = s; best = label[i]; }
        }
        return best;
    }
    std::size_t bytes() const {
        return item.size() * (kGlyphBytes + sizeof(std::size_t)) + Codebook::bytes();
    }
};

// argmax over a restricted range of the MLP's output head. Restricting matters:
// the head has all T*K units from the start, and letting it answer with a class
// it has never seen would score it against a different chance level than the
// others.
std::size_t mlp_predict(const Mlp& net, const std::vector<double>& x,
                        std::size_t lo, std::size_t hi) {
    const std::vector<double> z = net.forward(x);
    std::size_t best = lo;
    for (std::size_t k = lo + 1; k < hi && k < z.size(); ++k)
        if (z[k] > z[best]) best = k;
    return best;
}

std::size_t mlp_bytes(std::size_t n_in, std::size_t n_hid, std::size_t n_out) {
    return (n_in * n_hid + n_hid + n_hid * n_out + n_out) * sizeof(double);
}

// One complete sequential run plus its own joint reference, at a given hidden
// width. Used by the capacity sweep in section 7 and nowhere else; the main run
// in part A is deliberately NOT refactored through this, because part A also
// needs the per-checkpoint curves and merging the two would obscure both.
struct MlpRun {
    Tally fresh;        // task-aware, task t right after learning task t
    Tally t1_ci, t1_ta; // task 1 at the end of the stream
    Tally avg_ci;       // all tasks at the end of the stream
    Tally joint_ci;     // the same width trained jointly, all tasks
};

MlpRun run_mlp(const std::vector<Task>& tasks, std::size_t hidden, std::size_t n_classes) {
    const std::size_t T = tasks.size();
    MlpRun r;
    Mlp net(kDim, hidden, n_classes, 0x51DE1);
    std::uint64_t s = 4242;
    for (std::size_t t = 0; t < T; ++t) {
        for (std::size_t e = 0; e < kEpochs; ++e)
            net.train_epoch(tasks[t].train_x, tasks[t].train_y, kLr, kBatch, s);
        const std::size_t lo = t * kClassesPerTsk, hi = lo + kClassesPerTsk;
        for (std::size_t i = 0; i < tasks[t].test_x.size(); ++i)
            r.fresh.add(mlp_predict(net, tasks[t].test_x[i], lo, hi) == tasks[t].test_y[i]);
    }
    for (std::size_t t = 0; t < T; ++t) {
        const std::size_t lo = t * kClassesPerTsk, hi = lo + kClassesPerTsk;
        for (std::size_t i = 0; i < tasks[t].test_x.size(); ++i) {
            const std::size_t truth = tasks[t].test_y[i];
            const bool c = mlp_predict(net, tasks[t].test_x[i], 0, n_classes) == truth;
            r.avg_ci.add(c);
            if (t == 0) {
                r.t1_ci.add(c);
                r.t1_ta.add(mlp_predict(net, tasks[t].test_x[i], lo, hi) == truth);
            }
        }
    }
    std::vector<std::vector<double>> all_x;
    std::vector<std::size_t>         all_y;
    for (const auto& tk : tasks)
        for (std::size_t i = 0; i < tk.train_x.size(); ++i) {
            all_x.push_back(tk.train_x[i]);
            all_y.push_back(tk.train_y[i]);
        }
    Mlp joint(kDim, hidden, n_classes, 0x51DE1);
    std::uint64_t js = 4242;
    for (std::size_t e = 0; e < kEpochs; ++e) joint.train_epoch(all_x, all_y, kLr, kBatch, js);
    for (const auto& tk : tasks)
        for (std::size_t i = 0; i < tk.test_x.size(); ++i)
            r.joint_ci.add(mlp_predict(joint, tk.test_x[i], 0, n_classes) == tk.test_y[i]);
    return r;
}

enum Sys { SYS_MLP = 0, SYS_PROTO, SYS_ONE, SYS_NN, SYS_COUNT };
const char* const kSysName[SYS_COUNT] = {"MLP (SGD)", "HDC proto", "HDC 1-glyph", "1-NN stored"};

// acc[system][checkpoint][task]
using Curve = std::vector<std::vector<Tally>>;

void print_header(const char* what, std::size_t per_task_n) {
    std::printf("\n  %s   (n = %zu test examples per task, held out)\n", what, per_task_n);
    std::printf("  after | cls  | chance |");
    for (int s = 0; s < SYS_COUNT; ++s) std::printf(" %-19s |", kSysName[s]);
    std::printf("\n  task  | seen |        |");
    for (int s = 0; s < SYS_COUNT; ++s) std::printf("  acc  [95%% Wilson] |");
    std::printf("\n  ------+------+--------+");
    for (int s = 0; s < SYS_COUNT; ++s) std::printf("---------------------+");
    std::printf("\n");
}

} // namespace

int main() {
    std::printf("FORGETTING — does bundling actually make the substrate immune?\n");
    std::printf("  %zu tasks x %zu classes over %zu-dim Gaussian blobs (sigma %.2f). ONE shared set\n",
                std::size_t{10}, kClassesPerTsk, kDim, kBlobSigma);
    std::printf("  of centres, a fresh random permutation of the feature axes per task, so every\n");
    std::printf("  task occupies the same input region and competes for the same weights.\n");
    std::printf("  %zu training examples per class, seen ONCE, in order, never revisited.\n",
                kTrainPerClass);

    // ========================================================================
    // PART A — the retention curves
    // ========================================================================
    constexpr std::size_t T = 10;
    constexpr std::size_t kTestPerClass = 250;
    const std::size_t     n_classes = T * kClassesPerTsk;
    const std::size_t     per_task_n = kTestPerClass * kClassesPerTsk;

    const std::vector<Task> tasks = make_tasks(T, kTestPerClass, 0xF0E5E77ULL);
    const Codebook          book(0xC0DEB0075ULL);

    // Encode once. Encodings never change, so re-encoding at every checkpoint
    // would be the dominant cost of the whole bench for no reason.
    std::vector<std::vector<Glyph>> train_g(T), test_g(T);
    for (std::size_t t = 0; t < T; ++t) {
        for (const auto& x : tasks[t].train_x) train_g[t].push_back(book.encode(x));
        for (const auto& x : tasks[t].test_x)  test_g[t].push_back(book.encode(x));
    }

    Mlp         mlp(kDim, kHidden, n_classes, 0x51DE1);
    ProtoHdc    proto;
    OneGlyphHdc one(n_classes);
    StoreAll    nn;

    std::array<Curve, SYS_COUNT> ci, ta;   // class-incremental, task-aware
    for (auto& c : ci) c.assign(T, std::vector<Tally>(T));
    for (auto& c : ta) c.assign(T, std::vector<Tally>(T));

    std::vector<std::size_t> one_terms(T, 0), nn_items(T, 0);
    std::vector<std::size_t> mem_mlp(T, 0), mem_proto(T, 0), mem_one(T, 0), mem_nn(T, 0);

    std::uint64_t shuffle_seed = 4242;
    for (std::size_t t = 0; t < T; ++t) {
        // --- learn task t, and only task t ---------------------------------
        for (std::size_t e = 0; e < kEpochs; ++e)
            mlp.train_epoch(tasks[t].train_x, tasks[t].train_y, kLr, kBatch, shuffle_seed);

        for (std::size_t k = 0; k < kClassesPerTsk; ++k) {
            std::vector<Glyph> of_class;
            for (std::size_t i = 0; i < train_g[t].size(); ++i)
                if (tasks[t].train_y[i] == t * kClassesPerTsk + k) of_class.push_back(train_g[t][i]);
            proto.learn_class(of_class);
        }
        for (std::size_t i = 0; i < train_g[t].size(); ++i) {
            one.add(train_g[t][i], tasks[t].train_y[i]);
            nn.add(train_g[t][i], tasks[t].train_y[i]);
        }
        one.seal();

        const std::size_t seen = (t + 1) * kClassesPerTsk;
        one_terms[t] = one.terms.size();
        nn_items[t]  = nn.item.size();
        mem_mlp[t]   = mlp_bytes(kDim, kHidden, n_classes);
        mem_proto[t] = proto.bytes();
        mem_one[t]   = one.bytes(seen);
        mem_nn[t]    = nn.bytes();

        // --- re-measure every task seen so far ------------------------------
        for (std::size_t s = 0; s <= t; ++s) {
            const std::size_t lo = s * kClassesPerTsk, hi = lo + kClassesPerTsk;
            for (std::size_t i = 0; i < test_g[s].size(); ++i) {
                const std::size_t truth = tasks[s].test_y[i];
                const auto& xv = tasks[s].test_x[i];
                const auto& gv = test_g[s][i];
                ci[SYS_MLP]  [t][s].add(mlp_predict(mlp, xv, 0, seen) == truth);
                ci[SYS_PROTO][t][s].add(proto.predict(gv, 0, seen)    == truth);
                ci[SYS_ONE]  [t][s].add(one.predict(gv, 0, seen)      == truth);
                ci[SYS_NN]   [t][s].add(nn.predict(gv, 0, seen)       == truth);
                ta[SYS_MLP]  [t][s].add(mlp_predict(mlp, xv, lo, hi)  == truth);
                ta[SYS_PROTO][t][s].add(proto.predict(gv, lo, hi)     == truth);
                ta[SYS_ONE]  [t][s].add(one.predict(gv, lo, hi)       == truth);
                ta[SYS_NN]   [t][s].add(nn.predict(gv, lo, hi)        == truth);
            }
        }
    }

    // --- 1. THE HEADLINE: retention of task 1 --------------------------------
    std::printf("\n=== 1. RETENTION OF TASK 1 — accuracy on task 1 after learning tasks 1..t ===\n");
    print_header("class-incremental: no task ID given, argmax over every class seen", per_task_n);
    for (std::size_t t = 0; t < T; ++t) {
        const std::size_t seen = (t + 1) * kClassesPerTsk;
        std::printf("  %5zu | %4zu | %5.2f%% |", t + 1, seen, 100.0 / static_cast<double>(seen));
        for (int s = 0; s < SYS_COUNT; ++s) cell(ci[s][t][0]);
        std::printf("\n");
    }

    print_header("task-aware: the task ID is given, argmax over that task's 4 classes", per_task_n);
    for (std::size_t t = 0; t < T; ++t) {
        std::printf("  %5zu | %4zu | %5.2f%% |", t + 1, (t + 1) * kClassesPerTsk,
                    100.0 / static_cast<double>(kClassesPerTsk));
        for (int s = 0; s < SYS_COUNT; ++s) cell(ta[s][t][0]);
        std::printf("\n");
    }

    // --- 2. average over every task seen so far ------------------------------
    std::printf("\n=== 2. AVERAGE OVER ALL TASKS SEEN SO FAR ===\n");
    print_header("class-incremental; n grows to (t+1) x per-task n", per_task_n);
    for (std::size_t t = 0; t < T; ++t) {
        const std::size_t seen = (t + 1) * kClassesPerTsk;
        std::printf("  %5zu | %4zu | %5.2f%% |", t + 1, seen, 100.0 / static_cast<double>(seen));
        for (int s = 0; s < SYS_COUNT; ++s) {
            Tally agg;
            for (std::size_t k = 0; k <= t; ++k) agg.merge(ci[s][t][k]);
            cell(agg);
        }
        std::printf("\n");
    }
    print_header("task-aware", per_task_n);
    for (std::size_t t = 0; t < T; ++t) {
        std::printf("  %5zu | %4zu | %5.2f%% |", t + 1, (t + 1) * kClassesPerTsk,
                    100.0 / static_cast<double>(kClassesPerTsk));
        for (int s = 0; s < SYS_COUNT; ++s) {
            Tally agg;
            for (std::size_t k = 0; k <= t; ++k) agg.merge(ta[s][t][k]);
            cell(agg);
        }
        std::printf("\n");
    }

    // --- 3. did each system ever learn each task? ----------------------------
    //
    // Forgetting a task you never learned is not forgetting. This is the
    // diagonal: accuracy on task t immediately after learning task t.
    std::printf("\n=== 3. FRESH ACCURACY — task t measured immediately after learning task t ===\n");
    std::printf("  task-aware, so this is each system's ceiling on the task in isolation.\n");
    std::printf("  task |");
    for (int s = 0; s < SYS_COUNT; ++s) std::printf(" %-11s |", kSysName[s]);
    std::printf("\n  -----+");
    for (int s = 0; s < SYS_COUNT; ++s) std::printf("-------------+");
    std::printf("\n");
    for (std::size_t t = 0; t < T; ++t) {
        std::printf("  %4zu |", t + 1);
        for (int s = 0; s < SYS_COUNT; ++s) std::printf(" %10.1f%% |", ta[s][t][t].pct());
        std::printf("\n");
    }

    // --- 4. the reference the MLP is owed ------------------------------------
    std::printf("\n=== 4. THE HONEST UPPER REFERENCE — the SAME MLP trained on ALL tasks jointly ===\n");
    {
        std::vector<std::vector<double>> all_x;
        std::vector<std::size_t>         all_y;
        for (std::size_t t = 0; t < T; ++t)
            for (std::size_t i = 0; i < tasks[t].train_x.size(); ++i) {
                all_x.push_back(tasks[t].train_x[i]);
                all_y.push_back(tasks[t].train_y[i]);
            }
        // Same epoch count over the union, so every example is visited the same
        // number of times as in the sequential run. Equal data, equal passes,
        // equal architecture, equal seed -- the only difference is the ORDER.
        Mlp joint(kDim, kHidden, n_classes, 0x51DE1);
        std::uint64_t js = 4242;
        for (std::size_t e = 0; e < kEpochs; ++e)
            joint.train_epoch(all_x, all_y, kLr, kBatch, js);

        Tally jc, jt, j1c, j1t;
        for (std::size_t s = 0; s < T; ++s) {
            const std::size_t lo = s * kClassesPerTsk, hi = lo + kClassesPerTsk;
            for (std::size_t i = 0; i < tasks[s].test_x.size(); ++i) {
                const std::size_t truth = tasks[s].test_y[i];
                const bool c = mlp_predict(joint, tasks[s].test_x[i], 0, n_classes) == truth;
                const bool a = mlp_predict(joint, tasks[s].test_x[i], lo, hi) == truth;
                jc.add(c); jt.add(a);
                if (s == 0) { j1c.add(c); j1t.add(a); }
            }
        }
        std::printf("  same architecture, same seed, same %zu epochs, all %zu examples shuffled together.\n",
                    kEpochs, all_x.size());
        std::printf("  protocol          | scope            |   hits/n    |  acc   | 95%% Wilson\n");
        std::printf("  ------------------+------------------+-------------+--------+------------------\n");
        auto row = [](const char* p, const char* sc, const Tally& x) {
            const auto w = wilson(x.hit, x.n);
            std::printf("  %-17s | %-16s | %5zu/%-5zu | %5.1f%% | [%5.1f%%, %5.1f%%]\n",
                        p, sc, x.hit, x.n, x.pct(), w.first, w.second);
        };
        row("class-increment.", "task 1 only", j1c);
        row("class-increment.", "all 10 tasks", jc);
        row("task-aware", "task 1 only", j1t);
        row("task-aware", "all 10 tasks", jt);
        std::printf("\n  The gap between this and the sequential MLP row at t=10 IS the forgetting,\n"
                    "  and nothing else: same network, same data, same passes, only the order\n"
                    "  differs. The joint run separates all %zu classes without difficulty, so the\n"
                    "  sequential run's loss cannot be blamed on the problem being too hard.\n",
                    n_classes);
    }

    // --- 5. what it costs ----------------------------------------------------
    std::printf("\n=== 5. MEMORY — bytes held by each system after t tasks ===\n");
    std::printf("  after | MLP (SGD) | HDC proto | HDC 1-glyph | 1-NN stored | 1-glyph | stored\n");
    std::printf("  task  |    bytes  |    bytes  |      bytes  |      bytes  |  terms  |  items\n");
    std::printf("  ------+-----------+-----------+-------------+-------------+---------+-------\n");
    for (std::size_t t = 0; t < T; ++t)
        std::printf("  %5zu | %9zu | %9zu | %11zu | %11zu | %7zu | %6zu\n",
                    t + 1, mem_mlp[t], mem_proto[t], mem_one[t], mem_nn[t],
                    one_terms[t], nn_items[t]);
    std::printf("\n  What grows and what does not, stated plainly:\n"
                "    MLP          fixed. The head is sized for all %zu classes up front.\n"
                "    HDC proto    grows with the number of CLASSES (%zu bytes each), not with\n"
                "                 the number of examples. Adding data to a known class is free.\n"
                "    HDC 1-glyph  fixed. %zu bit counters plus one glyph plus the class codebook.\n"
                "                 It is the only system here whose footprint ignores the data.\n"
                "    1-NN stored  grows with EVERY EXAMPLE. If it retains best, that is what\n"
                "                 storing everything buys, and it is not a substrate property.\n"
                "  All HDC rows include the %zu-byte encoder codebook (%zu level + %zu position\n"
                "  glyphs); it is part of the model and pretending otherwise would flatter them.\n",
                n_classes, kGlyphBytes, kGlyphBits, Codebook::bytes(), kLevels, kDim);

    // ========================================================================
    // PART B — where the substrate DOES break
    // ========================================================================
    //
    // Part A cannot answer this: at 10 tasks the one-glyph memory holds 1,280
    // bundled terms and the prototype memory holds 40 prototypes, and neither is
    // obviously near its limit. substrate_bench put the capacity crossover of a
    // single glyph between 512 and 1,024 INDEPENDENT components; the terms here
    // are correlated (32 examples of one class produce 32 near-identical bound
    // pairs), so the wall should sit somewhere else and the only way to know
    // where is to keep going.
    //
    // The MLP is not run here. Part A already shows it at chance on task 1 by
    // task 2, and 40 sequential trainings over a 160-way head would dominate the
    // runtime to re-establish a zero.
    std::printf("\n=== 6. WHERE THE HDC SIDE BREAKS — 40 tasks, same generator, smaller test set ===\n");
    {
        constexpr std::size_t TL = 40;
        constexpr std::size_t kProbeTestPerClass = 50;
        const std::size_t     lclasses = TL * kClassesPerTsk;

        const std::vector<Task> lt = make_tasks(TL, kProbeTestPerClass, 0xB1657EA3ULL);
        std::vector<std::vector<Glyph>> ltr(TL), lte(TL);
        for (std::size_t t = 0; t < TL; ++t) {
            for (const auto& x : lt[t].train_x) ltr[t].push_back(book.encode(x));
            for (const auto& x : lt[t].test_x)  lte[t].push_back(book.encode(x));
        }

        ProtoHdc    lproto;
        OneGlyphHdc lone(lclasses);

        std::printf("  task 1 retention and the running average, class-incremental.\n");
        std::printf("  margin is the one-glyph memory's (best class sim - runner-up sim) on task 1:\n");
        std::printf("  the number that says whether a hit was a retrieval or a coin flip.\n\n");
        std::printf("  after | cls  | 1-glyph | chance | HDC proto task 1    | HDC 1-glyph task 1  | proto avg | 1-gl avg |  1-glyph\n");
        std::printf("  task  | seen |  terms  |        |  acc  [95%% Wilson]  |  acc  [95%% Wilson]  |  seen     |  seen    |  margin\n");
        std::printf("  ------+------+---------+--------+---------------------+---------------------+-----------+----------+---------\n");

        for (std::size_t t = 0; t < TL; ++t) {
            for (std::size_t k = 0; k < kClassesPerTsk; ++k) {
                std::vector<Glyph> of_class;
                for (std::size_t i = 0; i < ltr[t].size(); ++i)
                    if (lt[t].train_y[i] == t * kClassesPerTsk + k) of_class.push_back(ltr[t][i]);
                lproto.learn_class(of_class);
            }
            for (std::size_t i = 0; i < ltr[t].size(); ++i) lone.add(ltr[t][i], lt[t].train_y[i]);
            lone.seal();

            const std::size_t seen = (t + 1) * kClassesPerTsk;
            const bool report = (t < 3) || ((t + 1) % 4 == 0) || (t + 1 == TL);
            if (!report) continue;

            Tally p1, o1, pa, oa;
            double margin_sum = 0.0;
            std::size_t margin_n = 0;
            for (std::size_t s = 0; s <= t; ++s) {
                for (std::size_t i = 0; i < lte[s].size(); ++i) {
                    const std::size_t truth = lt[s].test_y[i];
                    const bool pk = lproto.predict(lte[s][i], 0, seen) == truth;
                    double m = 0.0;
                    const bool ok = lone.predict(lte[s][i], 0, seen, &m) == truth;
                    pa.add(pk); oa.add(ok);
                    if (s == 0) { p1.add(pk); o1.add(ok); margin_sum += m; ++margin_n; }
                }
            }
            const auto cp = wilson(p1.hit, p1.n);
            const auto co = wilson(o1.hit, o1.n);
            std::printf("  %5zu | %4zu | %7zu | %5.2f%% | %5.1f%% [%4.1f,%5.1f] | %5.1f%% [%4.1f,%5.1f] |"
                        " %8.1f%% | %7.1f%% | %+7.4f\n",
                        t + 1, seen, lone.terms.size(), 100.0 / static_cast<double>(seen),
                        p1.pct(), cp.first, cp.second, o1.pct(), co.first, co.second,
                        pa.pct(), oa.pct(),
                        margin_n ? margin_sum / static_cast<double>(margin_n) : 0.0);
        }
        std::printf("\n  n for the task-1 columns is %zu; the two average columns run to %zu at t=%zu.\n",
                    kProbeTestPerClass * kClassesPerTsk,
                    kProbeTestPerClass * kClassesPerTsk * TL, TL);
        std::printf("  Two different failure modes are visible in this one table, and they must\n"
                    "  not be confused:\n"
                    "    CROWDING   affects both HDC systems and the joint MLP alike. More classes\n"
                    "               means more ways to be wrong. Chance falls with it, so read the\n"
                    "               accuracy against the chance column, not against the first row.\n"
                    "    SATURATION affects the one-glyph memory ONLY. Its margin shrinks toward\n"
                    "               zero as terms accumulate, which is capacity, not crowding, and\n"
                    "               it is a property of bundling that no amount of data helps.\n");
    }

    // ========================================================================
    // PART C — is the MLP's survival a property of SGD, or just spare capacity?
    // ========================================================================
    //
    // Part A found something the headline claim did not predict: the sequential
    // MLP loses most of its class-incremental accuracy but keeps 100% TASK-AWARE
    // accuracy on task 1 after all 10 tasks. Its features are intact; what it
    // lost is the calibration BETWEEN tasks in the shared head. That is a real
    // and documented failure mode, and it is not the total representational
    // collapse the word "catastrophic" implies.
    //
    // The obvious suspect is slack: 64 hidden units in a 32-dim input space,
    // asked to hold 10 problems that each need about 4 directions. If that is
    // the explanation, squeezing the width should produce the classic collapse;
    // if the task-aware line stays flat even at width 4, the effect is about
    // this task family, not about capacity. Either answer is worth having and
    // neither is a knob being turned until the result looks better -- the joint
    // reference is re-run at every width, so a width that simply cannot learn
    // is visible as a low joint number rather than mistaken for forgetting.
    std::printf("\n=== 7. IS THE MLP'S SURVIVAL JUST SPARE CAPACITY? — hidden width swept ===\n");
    std::printf("  n = %zu for the task-1 columns, %zu for the all-task columns.\n",
                per_task_n, per_task_n * T);
    std::printf("  hidden |   bytes | fresh  | seq task1 | seq task1 | seq all  | JOINT all | forget\n");
    std::printf("   units |         | task-aw|  task-aw  |  class-in |  class-in|  class-in | (joint-seq)\n");
    std::printf("  -------+---------+--------+-----------+-----------+----------+-----------+--------\n");
    for (const std::size_t h : {std::size_t{4}, std::size_t{8}, std::size_t{16},
                                std::size_t{32}, std::size_t{64}}) {
        const MlpRun r = run_mlp(tasks, h, n_classes);
        std::printf("  %6zu | %7zu | %5.1f%% |  %6.1f%%  |  %6.1f%%  | %6.1f%%  |  %6.1f%%  | %+6.1f\n",
                    h, mlp_bytes(kDim, h, n_classes), r.fresh.pct(), r.t1_ta.pct(),
                    r.t1_ci.pct(), r.avg_ci.pct(), r.joint_ci.pct(),
                    r.joint_ci.pct() - r.avg_ci.pct());
    }
    std::printf("\n  fresh = task-aware accuracy on task t immediately after learning task t,\n"
                "  pooled over all %zu tasks. A width whose fresh number is low never learned\n"
                "  the tasks in the first place and its forgetting column means nothing.\n", T);

    std::printf("\n");
    return 0;
}
