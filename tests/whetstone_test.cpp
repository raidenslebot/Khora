// Whetstone test — the self-sharpening faculties.
//
// The module had no test, which is how a coin flip sat inside the transitive
// reasoning faculty for its whole life. These pin the properties that matter:
// each faculty is competent at the easy end, and multi-hop chain traversal is
// DIRECTED rather than a fair guess between a node's successor and predecessor.

#include "khora/whetstone/whetstone.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else         std::printf("  ok  : %s\n", what);
}

} // namespace

int main() {
    using namespace khora::whetstone;

    std::printf("Whetstone test\n");

    // THE REGRESSION. A chain A->B->C is encoded as a bundle of transition
    // bindings and traversed by unbinding. bind is XOR, which is commutative,
    // so bind(a,b) == bind(b,a): with a plain binding the chain is a set of
    // UNDIRECTED edges, unbinding an item has two equally strong claimants --
    // the transition in and the transition out -- and cleanup chooses between
    // successor and predecessor by coin flip. Multi-hop then compounds it.
    //
    // Measured with the symmetric encoding, averaged over 16 seeds:
    //   L=5   1-hop 60.9%  3-hop 15.6%   walked backwards 39.1%
    //   L=12  1-hop 51.7%  3-hop  2.1%   walked backwards 48.3%
    //   L=35  1-hop 51.8%  3-hop  5.5%   walked backwards 48.2%
    // Permuting the source before binding breaks the symmetry, because permute
    // is not self-inverse. The whetstone run went from a ceiling of difficulty
    // 2 at 66.67% to difficulty 32 at 100%.
    //
    // Difficulty d means a chain of length d + 3, and the score mixes 1-, 2-
    // and 3-hop recovery, so anything near 0.5 here is the coin flip returning.
    {
        auto t = make_transitive_faculty(0x713A11ULL);
        check(t->name() == "transitive_reasoning", "transitive faculty constructed");

        for (const int d : {1, 5, 12, 32, 60, 97}) {
            const auto o = t->attempt(d);
            char msg[112];
            std::snprintf(msg, sizeof msg,
                          "chain of %d traverses exactly (1, 2 and 3 hops)", d + 3);
            std::printf("  d=%-3d chain=%-4d score=%6.2f%%   %s\n",
                        d, d + 3, 100.0 * o.score, o.detail.c_str());
            check(o.score >= 0.99, msg);
        }

        // The capacity ceiling is real and lies well beyond the old cap of 32.
        // Superposing this many transitions into one 10,000-bit glyph is what
        // finally degrades it -- crosstalk, not direction. Asserted loosely
        // because it is a measurement, not a contract.
        const auto far = t->attempt(197);          // chain of 200
        std::printf("  d=197 chain=200 score=%6.2f%%\n", 100.0 * far.score);
        check(far.score >= 0.95, "chains of 200 are still near-exact");

        check(t->max_difficulty() > 32,
              "difficulty ceiling is above the level the faculty already clears");
    }

    // The other two faculties, at the easy end only -- enough to catch a
    // wholesale break without pinning numbers that are meant to move.
    {
        auto r = make_relational_faculty(0xBEAC04ULL);
        const auto o = r->attempt(1);
        std::printf("  relational d=1 score=%.2f%%\n", 100.0 * o.score);
        check(o.score >= 0.99, "relational capacity is exact at difficulty 1");

        auto s = make_sequence_faculty(0x5EE5EEDULL);
        const auto o2 = s->attempt(1);
        std::printf("  sequence   d=1 score=%.2f%%\n", 100.0 * o2.score);
        check(o2.score >= 0.99, "sequence induction is exact at difficulty 1");
    }

    // The engine keeps a beneficial mutation and discards a harmful one, so a
    // faculty can never be left worse off than the engine found it.
    {
        Whetstone w;
        w.add_faculty(make_transitive_faculty(0x713A11ULL));
        const auto steps = w.run(6);
        check(steps.size() == 6, "engine runs the requested rounds");
        bool any_escalation = false;
        for (const auto& st : steps) if (st.escalated) any_escalation = true;
        check(any_escalation, "a mastered faculty escalates to harder problems");
    }

    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
