// THE RUNNING SYSTEM CAN NOW BE TOLD A RULE.
//
// Every act of reasoning Khora performed was compiled in. `Ligature::deduce`
// carries two inference patterns, written into the C++, and that was the whole
// of it: the system could not be told "a thing is fragile if it is made of glass
// and is thin", could not use a rule it read in a book, and could not derive a
// rule and then apply it.
//
// A resolver that fixes that -- Horn clauses, unification, SLD resolution, with
// the derivation returned alongside every answer -- has been in this tree,
// tested with thirty checks, and never run by the live binary. It also had
// nothing to reason OVER: the Ligature could answer "what does X cause" but not
// "what is in here", so handing it the fourteen thousand relations meant already
// knowing every subject to ask about. `Ligature::all` closes that half.
//
// FOUR TOOLS.
//
//   know  -- what the engine is holding, and reseed it at a different floor
//   rule  -- add a Horn clause, in the shell, at runtime
//   ask   -- every binding that satisfies a goal
//   why   -- the derivation behind the first one
//
// THE SUPPORT FLOOR IS NOT A TUNING KNOB. 14,544 extracted triples become 377 at
// a floor of two, and the ones it drops are the single sightings that make a
// chain a coincidence rather than a proof -- the same measurement that made the
// planner refuse a step below two. Seeding all of them would give the resolver
// fourteen thousand facts, most of them noise, and it would prove things out of
// them very convincingly.
//
// AND THE TWO HARDCODED RULES ARE NOW DATA. They are installed at seed time as
// ordinary clauses named `inherit` and `chain`, so they appear in `know`, they
// show up in a derivation by name, and they can be deleted. That is the actual
// point: not that Khora has two more inference rules, but that they stopped
// being C++.

#include "khora/carapace/reason_tools.hpp"

#include "khora/logos/logos.hpp"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace khora::carapace {
namespace {

// Local, matching builtin_tools.cpp and techne_tools.cpp.
ToolResult make_ok(std::string output) { return {true, std::move(output), ""}; }
ToolResult make_err(std::string error) { return {false, "", std::move(error)}; }

// HOW DEEP TO SEARCH, EXPRESSED IN SOMETHING A CALLER CAN REASON ABOUT.
//
// logos::Engine counts RESOLUTION STEPS, not links, and a transitive chain of k
// links costs 2k-1 of them. These tools were passing 4, which reaches chains of
// TWO -- so the `transitive` and `chain` rules installed below could barely
// chain at all, and said nothing about it: an over-budget query returns an empty
// result that is indistinguishable from "no such fact".
//
// A bounded model check of the resolver is what turned that up. The tools now
// name the number of links and convert.
constexpr int kMaxLinks = 4;
constexpr int depth_for_links(int links) { return 2 * links - 1; }
struct Reasoner {
    logos::Engine engine;
    std::uint32_t floor = 2;
    std::size_t   seeded_facts = 0;
    bool          ready = false;
};

// Rebuild from scratch: the Engine appends and never forgets, so reseeding at a
// different floor has to start over or the old facts stay.
void seed(Reasoner& r, const ligature::Ligature& lig, std::uint32_t floor) {
    r.engine = logos::Engine{};
    r.floor = floor;
    for (const auto& t : lig.all(floor)) {
        r.engine.fact(ligature::relation_name(t.rel), t.subject, t.object);
    }
    r.seeded_facts = r.engine.fact_count();

    // The two patterns that used to be C++, as clauses.
    r.engine.rule(logos::Rule{
        logos::Atom{"has", logos::Term::variable("?x"), logos::Term::variable("?z")},
        { logos::Atom{"is-a", logos::Term::variable("?x"), logos::Term::variable("?a")},
          logos::Atom{"has",  logos::Term::variable("?a"), logos::Term::variable("?z")} },
        "inherit"});
    r.engine.rule(logos::Rule{
        logos::Atom{"causes", logos::Term::variable("?x"), logos::Term::variable("?z")},
        { logos::Atom{"causes", logos::Term::variable("?x"), logos::Term::variable("?y")},
          logos::Atom{"causes", logos::Term::variable("?y"), logos::Term::variable("?z")} },
        "chain"});
    // And one the system could never have had: is-a is transitive. deduce()
    // walks the taxonomy in a hand-written loop and cannot state this.
    r.engine.rule(logos::Rule{
        logos::Atom{"is-a", logos::Term::variable("?x"), logos::Term::variable("?z")},
        { logos::Atom{"is-a", logos::Term::variable("?x"), logos::Term::variable("?y")},
          logos::Atom{"is-a", logos::Term::variable("?y"), logos::Term::variable("?z")} },
        "transitive"});
    r.ready = true;
}

void ensure(Reasoner& r, const ligature::Ligature& lig) {
    if (!r.ready) seed(r, lig, r.floor);
}

} // namespace

void register_reason_tools(Carapace& c, const ligature::Ligature& lig) {
    auto state = std::make_shared<Reasoner>();

    c.register_tool({
        "know",
        "what the rule engine holds, and reseed it at a support floor "
        "(usage: know [floor])",
        [state, &lig](const Intent& i) -> ToolResult {
            if (!i.args.empty()) {
                std::uint32_t f = 2;
                try { f = static_cast<std::uint32_t>(std::stoul(i.args[0])); }
                catch (...) { return make_err("floor must be a number"); }
                if (f == 0) return make_err("a floor of zero is every single sighting");
                seed(*state, lig, f);
            } else {
                ensure(*state, lig);
            }
            std::ostringstream os;
            os << "  " << state->seeded_facts << " facts at support >= " << state->floor
               << ", out of " << lig.triple_count() << " extracted relations\n";
            os << "  " << state->engine.rule_count() << " rules:\n";
            os << "    inherit    : has(?x,?z)    :- is-a(?x,?a), has(?a,?z)\n";
            os << "    chain      : causes(?x,?z) :- causes(?x,?y), causes(?y,?z)\n";
            os << "    transitive : is-a(?x,?z)   :- is-a(?x,?y), is-a(?y,?z)\n";
            os << "  the first two were written into Ligature::deduce as C++ and are\n"
                  "  now ordinary clauses; add a fourth with `rule`.\n";
            os << "  chains are followed " << kMaxLinks << " links deep ("
               << depth_for_links(kMaxLinks) << " resolution steps -- the engine counts\n"
                  "  steps, and a k-link chain costs 2k-1 of them).";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "rule",
        "add a Horn clause: head then body, each as rel/subj/obj, ?x for a "
        "variable (usage: rule <name> <rel> <s> <o> [<rel> <s> <o>]...)",
        [state, &lig](const Intent& i) -> ToolResult {
            if (i.args.size() < 4 || (i.args.size() - 1) % 3 != 0) {
                return make_err("usage: rule <name> <rel> <s> <o> [<rel> <s> <o>]...  "
                                "-- e.g. rule fragile is-a ?x brittle has ?x glass");
            }
            ensure(*state, lig);
            logos::Rule r;
            r.name = i.args[0];
            for (std::size_t k = 1; k + 2 < i.args.size() + 1 && k + 2 <= i.args.size(); k += 3) {
                logos::Atom a{i.args[k], logos::Term::parse(i.args[k + 1]),
                              logos::Term::parse(i.args[k + 2])};
                if (k == 1) r.head = a; else r.body.push_back(a);
            }
            if (r.body.empty()) {
                // A clause with no body is a fact, and saying so is friendlier
                // than silently asserting one under a rule name.
                state->engine.fact(r.head.relation, r.head.subject.name, r.head.object.name);
                return make_ok("  no body, so that is a fact, and it was asserted as one:\n    "
                               + r.head.str());
            }
            state->engine.rule(r);
            std::ostringstream os;
            os << "  " << r.name << " : " << r.head.str() << " :-";
            for (std::size_t k = 0; k < r.body.size(); ++k) {
                os << (k ? ", " : " ") << r.body[k].str();
            }
            os << "\n  " << state->engine.rule_count() << " rules now.";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "ask",
        "every binding that satisfies a goal, over the extracted relations "
        "(usage: ask <rel> <subj> <obj>, ?x for a variable)",
        [state, &lig](const Intent& i) -> ToolResult {
            if (i.args.size() != 3)
                return make_err("usage: ask <rel> <subj> <obj>  -- e.g. ask is-a man ?what");
            ensure(*state, lig);
            const logos::Atom goal{i.args[0], logos::Term::parse(i.args[1]),
                                   logos::Term::parse(i.args[2])};
            // The budget is expressed in LINKS and converted, because the engine
            // counts resolution steps and the two differ by a factor of two. Four
            // links is 7 steps; passing 4 directly, which is what this did, buys
            // two links and looks identical to the relation not holding.
            const auto answers = state->engine.ask(goal, depth_for_links(kMaxLinks), 32);
            if (answers.empty()) return make_ok("  nothing satisfies " + goal.str());
            std::ostringstream os;
            os << "  " << answers.size() << " answer" << (answers.size() == 1 ? "" : "s")
               << " for " << goal.str() << ":\n";
            for (const auto& a : answers) {
                os << "   ";
                bool any = false;
                for (const auto& kv : a.bind) {
                    if (kv.first.find('#') != std::string::npos) continue;
                    os << " " << kv.first << " = " << logos::resolve(kv.second, a.bind).name;
                    any = true;
                }
                if (!any) os << " (holds, no variables)";
                if (!a.used.empty()) {
                    os << "   [by ";
                    for (std::size_t k = 0; k < a.used.size(); ++k)
                        os << (k ? " then " : "") << a.used[k];
                    os << "]";
                }
                os << "\n";
            }
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "why",
        "the derivation behind a goal -- which rules produced it "
        "(usage: why <rel> <subj> <obj>)",
        [state, &lig](const Intent& i) -> ToolResult {
            if (i.args.size() != 3)
                return make_err("usage: why <rel> <subj> <obj>");
            ensure(*state, lig);
            const logos::Atom goal{i.args[0], logos::Term::parse(i.args[1]),
                                   logos::Term::parse(i.args[2])};
            const std::string e = state->engine.explain(goal, depth_for_links(kMaxLinks));
            if (e.empty()) return make_ok("  " + goal.str() + " does not hold, so there is "
                                                              "nothing to explain");
            return make_ok("  " + e);
        }
    });
}

} // namespace khora::carapace
