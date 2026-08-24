// DOES THE PAWL ACTUALLY PREDICT? Head to head, on real books.
//
// The chain of measurements that led here:
//
//   TemporalMemory does not converge on prose -- burst 0.93 forever, 2.9M
//   segments for 24k tokens, unbounded.
//   A population of specialists lost to the monolith on every arm.
//   Similarity-preserving input codes bought 3%.
//   Threshold tuning bought nothing; the sweep is flat.
//   Then the reason: 8-word contexts recur 0.32% of the time even at 7.66M
//   tokens, while 1/2/3-word contexts recur 66.9 / 30.7 / 13.5%. There was
//   never anything to find at depth 8.
//
// ContextTree is the response: predict from the longest context actually seen,
// back off when it has not been, and hold a hard node budget by evicting
// contexts that never get used. It has to prove two things, and the second is
// the one this project keeps failing.
//
//   1. It BOUNDS. Already shown in the unit test: 40,000 purely novel symbols
//      settle at 18,471 nodes against a 20,000 budget.
//   2. It BEATS THE DUMB BASELINE. A thirty-line trigram table already beat
//      TemporalMemory 1.0000 to 0.9981 on these same books. If the pawl cannot
//      beat a trigram table it is not an architecture, it is ceremony.
//
// So four predictors, same stream, same held-out text, next-symbol accuracy:
//
//   most-frequent   always guess the commonest word. The floor.
//   bigram          the commonest successor of the previous word.
//   trigram         the commonest successor of the previous two.
//   ContextTree     variable order with backoff, under a hard budget.
//
// The trigram is the one to beat, and it is a fair fight: it gets the same
// training text and is allowed unlimited memory, which ContextTree is not.

#include "khora/cortex/context_tree.hpp"
#include "khora/reservoir/reservoir.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace khora::cortex;
using clock_t_ = std::chrono::high_resolution_clock;

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

// A plain n-gram table: for each context, the commonest successor. Unlimited
// memory, which is a real advantage over the budgeted ContextTree.
struct NGram {
    std::size_t n;
    std::unordered_map<std::uint64_t, std::unordered_map<std::uint32_t, std::uint32_t>> t;

    explicit NGram(std::size_t order) : n(order) {}

    std::uint64_t key(const std::vector<std::uint32_t>& s, std::size_t at) const {
        std::uint64_t h = 1469598103934665603ULL;
        for (std::size_t k = 0; k < n; ++k) {
            h ^= s[at - n + k];
            h *= 1099511628211ULL;
        }
        return h;
    }
    void learn(const std::vector<std::uint32_t>& s) {
        for (std::size_t i = n; i < s.size(); ++i) ++t[key(s, i)][s[i]];
    }
    bool predict(const std::vector<std::uint32_t>& s, std::size_t at,
                 std::uint32_t& out) const {
        if (at < n) return false;
        const auto it = t.find(key(s, at));
        if (it == t.end()) return false;
        std::uint32_t best = 0, bc = 0;
        for (const auto& [sym, c] : it->second) if (c > bc) { bc = c; best = sym; }
        if (bc == 0) return false;
        out = best;
        return true;
    }
    std::size_t contexts() const { return t.size(); }
    // Counted entries, not allocator overhead: 8 bytes for the context key,
    // 8 for each (symbol, count) it stores. The bigram keeps EVERY successor,
    // which is the advantage it has to be charged for.
    std::size_t bytes() const {
        std::size_t e = 0;
        for (const auto& kv : t) e += kv.second.size();
        return t.size() * 8 + e * 8;
    }
};

} // namespace

int main(int argc, char** argv) {
    const std::string dir  = (argc > 1) ? argv[1] : "data/reservoir";
    const std::size_t cap  = (argc > 2) ? std::stoul(argv[2]) : 400000;
    const std::size_t budget = (argc > 3) ? std::stoul(argv[3]) : 300000;

    khora::reservoir::Reservoir res(dir);
    res.load_catalog();
    const auto cat = res.catalog();
    if (cat.empty()) { std::printf("no tomes at %s\n", dir.c_str()); return 1; }

    // Intern words to ids once; every predictor sees the same integer stream.
    std::unordered_map<std::string, std::uint32_t> ids;
    std::vector<std::uint32_t> stream;
    for (const auto& t : cat) {
        if (stream.size() >= cap) break;
        auto text = res.read(t.title);
        if (!text || text->size() < 20000) continue;
        for (const auto& w : tokenize(*text)) {
            if (stream.size() >= cap) break;
            const auto it = ids.find(w);
            if (it != ids.end()) stream.push_back(it->second);
            else {
                const std::uint32_t id = static_cast<std::uint32_t>(ids.size());
                ids.emplace(w, id);
                stream.push_back(id);
            }
        }
    }

    // 90/10 split, held-out taken from the END so it is genuinely later text.
    const std::size_t split = stream.size() * 9 / 10;
    std::vector<std::uint32_t> train(stream.begin(), stream.begin() + split);
    std::vector<std::uint32_t> test(stream.begin() + split, stream.end());

    std::printf("Does the pawl predict? Next-word accuracy on real books.\n\n");
    std::printf("  %zu tokens, %zu distinct words\n", stream.size(), ids.size());
    std::printf("  %zu train / %zu held out (the last 10%%, never seen)\n\n",
                train.size(), test.size());

    // ---- most-frequent word: the floor ------------------------------------
    std::uint32_t commonest = 0;
    {
        std::unordered_map<std::uint32_t, std::uint32_t> c;
        for (const std::uint32_t s : train) ++c[s];
        std::uint32_t bc = 0;
        for (const auto& [sym, n] : c) if (n > bc) { bc = n; commonest = sym; }
    }
    std::size_t hit_mf = 0;
    for (const std::uint32_t s : test) if (s == commonest) ++hit_mf;

    // ---- bigram and trigram, unlimited memory ------------------------------
    NGram bi(1), tri(2);
    bi.learn(train);
    tri.learn(train);
    std::size_t hit_bi = 0, hit_tri = 0, cov_tri = 0;
    for (std::size_t i = 2; i < test.size(); ++i) {
        std::uint32_t p = 0;
        if (bi.predict(test, i, p) && p == test[i]) ++hit_bi;
        if (tri.predict(test, i, p)) { ++cov_tri; if (p == test[i]) ++hit_tri; }
    }

    // ---- ContextTree, budgeted --------------------------------------------
    ContextTreeConfig cfg;
    cfg.max_nodes = budget;
    ContextTree ct(cfg);
    const auto t0 = clock_t_::now();
    for (const std::uint32_t s : train) ct.observe(s);
    const double train_ms =
        std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();

    const std::size_t nodes_after_train = ct.nodes();
    const std::size_t bytes_after_train = ct.bytes();
    const std::size_t evicted_after_train = ct.evicted();

    // FROZEN: history advances, nothing is learned from the held-out text.
    // This is the fair comparison -- the n-gram tables do not get to read the
    // test set either.
    ct.reset();
    ct.clear_depth_signal();
    std::size_t hit_ct = 0, cov_ct = 0;
    // Per-order accuracy. "Use the longest context you have seen" is an
    // ASSUMPTION -- that deeper is better. This is the measurement that decides
    // whether it holds.
    std::vector<std::size_t> ord_try(cfg.max_order + 1, 0), ord_hit(cfg.max_order + 1, 0);
    // THE COUNTERFACTUAL. Falling to order 0 is the model saying "no context I
    // have is worth trusting here". That is only the right call if the bigram
    // would also have been wrong on those same positions -- otherwise the
    // caution is costing more than it saves. Same positions, both predictors.
    std::size_t floor_n = 0, floor_bi_hit = 0;
    for (std::size_t i = 0; i < test.size(); ++i) {
        const auto p = ct.predict();
        if (p.known) {
            ++cov_ct;
            ++ord_try[p.order];
            if (p.symbol == test[i]) { ++hit_ct; ++ord_hit[p.order]; }
            if (p.order == 0 && i >= 1) {
                ++floor_n;
                std::uint32_t q = 0;
                if (bi.predict(test, i, q) && q == test[i]) ++floor_bi_hit;
            }
        }
        ct.advance(test[i]);
    }
    const auto depth = ct.depth_signal();

    // ONLINE: the same model allowed to keep learning as it reads. Reported
    // separately and NOT compared against the n-grams, because it is a
    // different question -- what an online reader does, not what a frozen
    // model knows.
    ct.reset();
    std::size_t hit_on = 0, cov_on = 0;
    for (std::size_t i = 0; i < test.size(); ++i) {
        const auto p = ct.predict();
        if (p.known) { ++cov_on; if (p.symbol == test[i]) ++hit_on; }
        ct.observe(test[i]);
    }

    const double n = static_cast<double>(test.size());
    std::printf("  predictor        | accuracy | coverage |  contexts | memory  | bounded?\n");
    std::printf("  -----------------+----------+----------+-----------+---------+---------\n");
    std::printf("  most-frequent    |  %5.2f%%  |  100.0%%  |         1 |   0.0 MB| trivially\n",
                100.0 * hit_mf / n);
    std::printf("  bigram           |  %5.2f%%  |  100.0%%  | %9zu | %5.1f MB| no\n",
                100.0 * hit_bi / n, bi.contexts(), bi.bytes() / 1048576.0);
    std::printf("  trigram          |  %5.2f%%  |  %5.1f%%  | %9zu | %5.1f MB| no\n",
                100.0 * hit_tri / n, 100.0 * cov_tri / n, tri.contexts(),
                tri.bytes() / 1048576.0);
    std::printf("  ContextTree      |  %5.2f%%  |  %5.1f%%  | %9zu | %5.1f MB| YES, %zu nodes\n",
                100.0 * hit_ct / n, 100.0 * cov_ct / n, nodes_after_train,
                bytes_after_train / 1048576.0, budget);

    std::printf("\n  not part of the comparison, different question:\n");
    std::printf("  ContextTree      |  %5.2f%%  |  %5.1f%%  | %9zu |         | still LEARNING\n",
                100.0 * hit_on / n, 100.0 * cov_on / n, ct.nodes());
    std::printf("   (online)        |          |          |           |         | as it reads\n");

    std::printf("\n  ContextTree: %.1f s to train, %zu contexts evicted,\n",
                train_ms / 1000.0, evicted_after_train);
    std::printf("  mean backoff order %.2f, fell to the floor %.1f%% of the time\n",
                depth.mean_order, 100.0 * depth.floor_fraction);

    std::printf("\n  order actually used:");
    const auto& usage = ct.order_usage();
    std::size_t tot = 0;
    for (const std::size_t u : usage) tot += u;
    for (std::size_t o = 0; o < usage.size(); ++o) {
        std::printf("  n=%zu %.1f%%", o, tot ? 100.0 * usage[o] / tot : 0.0);
    }
    std::printf("\n");

    // THE ASSUMPTION UNDER TEST: deeper context predicts better.
    std::printf("\n  ACCURACY BY THE ORDER ACTUALLY USED (frozen model)\n");
    std::printf("     order | used     | share  | accuracy\n");
    std::printf("  ---------+----------+--------+---------\n");
    for (std::size_t o = 0; o <= cfg.max_order; ++o) {
        if (ord_try[o] == 0) continue;
        std::printf("  %8zu | %8zu | %5.1f%% |  %5.2f%%\n", o, ord_try[o],
                    100.0 * ord_try[o] / static_cast<double>(cov_ct),
                    100.0 * ord_hit[o] / static_cast<double>(ord_try[o]));
    }
    std::printf("  If accuracy does not RISE with order, then \"predict from the\n");
    std::printf("  longest context you have seen\" is the wrong rule, and depth is\n");
    std::printf("  being mistaken for reliability.\n");

    std::printf("\n  WAS FALLING TO THE UNIGRAM THE RIGHT CALL? (same %zu positions)\n",
                floor_n);
    std::printf("    ContextTree chose order 0 and scored  %5.2f%%\n",
                floor_n ? 100.0 * ord_hit[0] / static_cast<double>(floor_n) : 0.0);
    std::printf("    the bigram, on those same positions,  %5.2f%%\n",
                floor_n ? 100.0 * floor_bi_hit / static_cast<double>(floor_n) : 0.0);
    std::printf("    If the bigram is higher here, the caution is costing more than\n");
    std::printf("    it saves and the shrinkage prior is set too strong.\n");

    // SWEEP. Two knobs decide how readily a deep context is trusted, and
    // guessing at them is how the last three ideas in this project died.
    //
    //   prior_weight    evidence a context needs before its own hit rate
    //                   outweighs the shorter context it sits inside
    //   max_successors  continuations kept per context -- the bigram baseline
    //                   keeps ALL of them, so this is where a bounded model
    //                   pays for its bound
    std::printf("\n  SWEEP: how readily should a deep context be trusted?\n");
    std::printf("  (accuracy, frozen; bigram = %.2f%% with unlimited memory)\n\n",
                100.0 * hit_bi / n);
    std::printf("   keep / rule    |");
    const double weights[] = {1.0, 3.0, 10.0, 20.0, 50.0};
    for (const double w : weights) std::printf("  w=%-4.0f |", w);
    std::printf("\n  ----------------+");
    for (std::size_t k = 0; k < 5; ++k) std::printf("---------+");
    std::printf("\n");
    for (const std::size_t ms : {4u, 8u, 16u, 32u, 64u}) {
      for (const bool deepest : {false, true}) {
        std::printf("   keep %2zu %-7s|", ms, deepest ? "floor=alt" : "floor=1st");
        for (const double w : weights) {
            ContextTreeConfig c;
            c.max_nodes = budget;
            c.max_successors = ms;
            c.prior_weight = w;
            c.floor_is_last_resort = deepest;
            ContextTree t(c);
            for (const std::uint32_t s : train) t.observe(s);
            t.reset();
            std::size_t h = 0;
            for (std::size_t i = 0; i < test.size(); ++i) {
                const auto p = t.predict();
                if (p.known && p.symbol == test[i]) ++h;
                t.advance(test[i]);
            }
            const double acc = 100.0 * h / n;
            std::printf(" %6.2f%s |", acc, acc > 100.0 * hit_bi / n ? "*" : " ");
        }
        std::printf("\n");
      }
    }
    std::printf("  * beats the bigram baseline. \"best\" picks the highest shrunk\n");
    std::printf("  hit rate; \"deepest\" picks the longest context that clears the\n");
    std::printf("  shorter one it sits inside.\n");

    // DOES THE BOUND MATTER? Half a point over a bigram is not the claim worth
    // making. The claim worth making is that the margin is held under a ceiling
    // the baselines never have to respect -- so the question is what happens to
    // each model's memory as the stream grows, not what it is at one size.
    std::printf("\n  SCALE: accuracy and memory as the corpus grows\n");
    std::printf("  (ContextTree budget fixed at %zu nodes throughout)\n\n", budget);
    std::printf("      tokens |  bigram  | bigram MB |  ContextTree | CTree MB | nodes\n");
    std::printf("  -----------+----------+-----------+--------------+----------+--------\n");
    for (std::size_t frac = 4; frac >= 1; --frac) {
        const std::size_t upto = stream.size() / frac;
        const std::size_t sp = upto * 9 / 10;
        std::vector<std::uint32_t> tr(stream.begin(), stream.begin() + sp);
        std::vector<std::uint32_t> te(stream.begin() + sp, stream.begin() + upto);
        const double m = static_cast<double>(te.size());

        NGram b(1);
        b.learn(tr);
        std::size_t hb = 0;
        for (std::size_t i = 1; i < te.size(); ++i) {
            std::uint32_t q = 0;
            if (b.predict(te, i, q) && q == te[i]) ++hb;
        }

        ContextTreeConfig c;
        c.max_nodes = budget;
        ContextTree t(c);
        for (const std::uint32_t s : tr) t.observe(s);
        const std::size_t tn = t.nodes(), tb = t.bytes();
        t.reset();
        std::size_t hc = 0;
        for (std::size_t i = 0; i < te.size(); ++i) {
            const auto p = t.predict();
            if (p.known && p.symbol == te[i]) ++hc;
            t.advance(te[i]);
        }
        std::printf("  %10zu |  %5.2f%%  |  %6.1f   |    %5.2f%%%s   |  %6.1f  | %7zu\n",
                    upto, 100.0 * hb / m, b.bytes() / 1048576.0,
                    100.0 * hc / m, (100.0 * hc / m > 100.0 * hb / m) ? "*" : " ",
                    tb / 1048576.0, tn);
        if (frac == 1) break;
    }
    std::printf("  * ContextTree ahead. Watch the two MB columns, not the margin:\n");
    std::printf("    an n-gram table has no ceiling, so on a stream that does not\n");
    std::printf("    end it does not have a memory figure at all.\n");

    std::printf("\n  The trigram is the one to beat: it already beat TemporalMemory\n");
    std::printf("  on these books, and it is running with UNLIMITED memory while\n");
    std::printf("  ContextTree holds a hard budget. Coverage matters as much as\n");
    std::printf("  accuracy -- a predictor that answers rarely and is right when it\n");
    std::printf("  does has not solved the problem, it has avoided most of it.\n");
    return 0;
}
