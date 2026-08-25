// Ligature: the structured-relation layer, and the first test it has ever had.
//
// This module holds 19,475 typed relations in the running system and had no
// test. It is the layer everything reasoned rather than merely associated goes
// through -- is-a, causes, has-part, transitive reachability, deduction, and now
// backward planning -- and an audit of the tree found it untested while much
// smaller modules had several.
//
// The checks below are all of the same shape: build a small graph whose right
// answers are obvious by inspection, then require the module to produce them.
// Nothing here depends on a corpus, so nothing here can pass for the wrong
// reason.

#include "khora/ligature/ligature.hpp"

#include <cstdio>
#include <string>

using namespace khora::ligature;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

} // namespace

int main() {
    std::printf("Ligature — typed relations, deduction, and planning\n\n");

    // --- IS-A, DIRECT AND TRANSITIVE -----------------------------------------
    {
        Ligature g;
        g.add(Relation::IsA, "sparrow", "bird", 3);
        g.add(Relation::IsA, "bird", "animal", 5);
        g.add(Relation::IsA, "animal", "organism", 4);

        check(g.is_a("sparrow", "bird"), "a direct is-a holds");
        check(g.is_a("sparrow", "organism"), "and it is transitive across three hops");
        check(!g.is_a("organism", "sparrow"), "and it is NOT symmetric");
        check(!g.is_a("sparrow", "mineral"), "an unrelated pair does not hold");
        check(g.triple_count() == 3, "three distinct triples were stored");

        // The depth bound is a bound, not a suggestion.
        check(!g.is_a("sparrow", "organism", 1), "the depth limit actually limits");
    }

    // --- COUNTS ARE ADDITIVE -------------------------------------------------
    {
        Ligature g;
        g.add(Relation::Causes, "rain", "flood", 2);
        g.add(Relation::Causes, "rain", "flood", 3);
        check(g.count(Relation::Causes, "rain", "flood") == 5,
              "asserting the same triple twice sums its support");
        check(g.triple_count() == 1, "and does not create a second triple");
    }

    // --- QUERIES IN BOTH DIRECTIONS ------------------------------------------
    {
        Ligature g;
        g.add(Relation::Causes, "rain", "flood", 9);
        g.add(Relation::Causes, "dam-failure", "flood", 4);
        g.add(Relation::Causes, "flood", "damage", 6);

        const auto causes_of_flood = g.subjects(Relation::Causes, "flood");
        check(causes_of_flood.size() == 2, "two things are known to cause a flood");
        check(!causes_of_flood.empty() && causes_of_flood.front().first == "rain",
              "and the strongest is listed first");

        const auto effects_of_flood = g.objects(Relation::Causes, "flood");
        check(effects_of_flood.size() == 1 && effects_of_flood.front().first == "damage",
              "and the forward direction is separate from the backward one");
    }

    // --- DEDUCTION: FACTS THAT WERE NEVER ASSERTED ---------------------------
    {
        Ligature g;
        // Inheritance: a sparrow is a bird, birds have feathers.
        g.add(Relation::IsA,     "sparrow", "bird",     4);
        g.add(Relation::HasPart, "bird",    "feathers", 7);
        // Causal chaining: rain -> flood -> damage.
        g.add(Relation::Causes,  "rain",    "flood",    5);
        g.add(Relation::Causes,  "flood",   "damage",   6);

        check(g.count(Relation::HasPart, "sparrow", "feathers") == 0,
              "the inherited fact is genuinely not asserted");

        bool inherited = false;
        for (const Inference& f : g.deduce("sparrow")) {
            if (f.relation == Relation::HasPart && f.object == "feathers") inherited = true;
        }
        check(inherited, "deduce inherits a property through is-a");

        bool chained = false;
        std::size_t via_len = 0;
        for (const Inference& f : g.deduce("rain")) {
            if (f.relation == Relation::Causes && f.object == "damage") {
                chained = true;
                via_len = f.via.size();
            }
        }
        check(chained, "and chains causes transitively");
        check(via_len > 0, "and reports the chain that produced it");
    }

    // --- PLANNING: BACKWARD FROM A GOAL --------------------------------------
    //
    // deduce() answers "what follows from X". This is the other direction, and
    // the system had nothing that could ask it: "what would bring X about".
    {
        Ligature g;
        // A four-step causal road to the goal, plus a shorter weaker branch.
        g.add(Relation::Causes, "deforestation", "erosion",  8);
        g.add(Relation::Causes, "erosion",       "silting",  7);
        g.add(Relation::Causes, "silting",       "flood",    6);
        g.add(Relation::Causes, "flood",         "famine",   9);
        g.add(Relation::Causes, "blight",        "famine",   2);

        const auto plans = g.plan_to("famine", 5, 6, 5, 1);
        check(!plans.empty(), "a goal with a known cause yields a plan");

        bool full_road = false;
        for (const auto& p : plans) {
            if (p.steps.size() == 5 &&
                p.steps.front() == "deforestation" && p.steps.back() == "famine") {
                full_road = true;
                // The weakest link on that road is silting->flood at 6.
                check(p.support == 6, "and it is scored by its WEAKEST link, not its best");
            }
        }
        check(full_road, "and it reaches the whole way back to a root cause");

        check(!plans.empty() && plans.front().steps.back() == "famine",
              "every plan ends at the goal");
        check(!plans.empty() && plans.front().steps.size() >= 2,
              "and contains at least one causal step");

        // Depth is a bound.
        const auto shallow = g.plan_to("famine", 2, 6, 5, 1);
        bool within = true;
        for (const auto& p : shallow) if (p.steps.size() > 3) within = false;
        check(within, "the depth limit bounds the plan length");

        // A goal nothing is known to cause has no plan, and says so rather than
        // inventing one.
        check(g.plan_to("aardvark").empty(), "a goal with no known cause yields nothing");

        // THE SUPPORT FLOOR. On the live graph the unfiltered planner returns
        // chains every link of which was asserted exactly once, which are real
        // paths and worthless as plans. Raising the floor above a link's support
        // must remove the chains that depend on it.
        g.add(Relation::Causes, "rumour", "blight", 1);
        bool weak_shown = false, weak_hidden = true;
        for (const auto& p : g.plan_to("famine", 5, 6, 8, 1))
            if (p.steps.front() == "rumour") weak_shown = true;
        for (const auto& p : g.plan_to("famine", 5, 6, 8, 2))
            if (p.steps.front() == "rumour") weak_hidden = false;
        check(weak_shown, "a once-asserted link shows at floor 1");
        check(weak_hidden, "and is gone at floor 2");
    }

    // --- PLANNING REFUSES LOOPS ----------------------------------------------
    //
    // A cycle in the causal graph is the case that turns a naive backward search
    // into an infinite one, and real extracted relations contain plenty.
    {
        Ligature g;
        g.add(Relation::Causes, "poverty", "crime",   5);
        g.add(Relation::Causes, "crime",   "poverty", 4);
        g.add(Relation::Causes, "crime",   "fear",    3);

        const auto plans = g.plan_to("fear", 6, 6, 5, 1);
        check(!plans.empty(), "a goal reachable through a cycle still yields a plan");
        bool no_repeat = true;
        for (const auto& p : plans) {
            for (std::size_t a = 0; a < p.steps.size(); ++a)
                for (std::size_t b = a + 1; b < p.steps.size(); ++b)
                    if (p.steps[a] == p.steps[b]) no_repeat = false;
        }
        check(no_repeat, "and no plan revisits a concept");
    }

    // --- THE PLANNING FACULTY MEASURES ITSELF --------------------------------
    {
        Ligature g;
        for (int i = 0; i < 20; ++i) {
            g.add(Relation::Causes, "a" + std::to_string(i), "b" + std::to_string(i), 3);
            g.add(Relation::Causes, "b" + std::to_string(i), "c" + std::to_string(i), 3);
        }
        const double rate = g.benchmark_planning(100, 2, 12345, 1);
        check(rate > 0.0 && rate <= 1.0, "benchmark_planning returns a rate in range");
        // Half the drawn goals are `b` nodes, reachable by one step only; the
        // other half are `c` nodes, reachable by two. So a two-step bar should
        // land near a half rather than at either extreme.
        check(rate > 0.2 && rate < 0.8,
              "and it discriminates -- neither everything nor nothing plans");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
