// Does it prove things, or does it return true?
//
// A resolver is easy to write and easy to write wrongly, and the wrong versions
// pass casual use. The three that matter are checked here explicitly: variables
// shared between two uses of the same rule, left recursion, and answers that are
// true by accident because unification bound something it should not have.
//
// The comparison at the end is the one that justifies the module existing:
// against `Ligature::deduce`, which does the same job for the two rules that
// were compiled into it and cannot be told a third.

#include "khora/logos/logos.hpp"

#include <cstdio>
#include <string>

using namespace khora::logos;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

Atom A(const std::string& r, const std::string& s, const std::string& o) {
    return Atom{r, Term::parse(s), Term::parse(o)};
}

// Did any answer bind ?v to `want`?
bool bound_to(const std::vector<Answer>& as, const std::string& v,
              const std::string& want) {
    for (const Answer& a : as) {
        const auto it = a.bind.find(v);
        if (it != a.bind.end() && resolve(it->second, a.bind).name == want) return true;
    }
    return false;
}

} // namespace

int main() {
    std::printf("Logos — Horn clauses, unification, resolution\n\n");

    // --- UNIFICATION, WHERE THE BUGS LIVE ------------------------------------
    {
        Binding b;
        check(unify(A("p", "a", "b"), A("p", "a", "b"), b), "identical ground atoms unify");
        Binding b2;
        check(!unify(A("p", "a", "b"), A("p", "a", "c"), b2), "differing ones do not");
        Binding b3;
        check(!unify(A("p", "a", "b"), A("q", "a", "b"), b3), "nor do different relations");

        Binding b4;
        check(unify(A("p", "?x", "b"), A("p", "a", "b"), b4), "a variable binds to an atom");
        check(resolve(Term::variable("?x"), b4).name == "a", "and resolves to it");

        // The one that catches a whole class of defect: the same variable twice
        // must take the same value.
        Binding b5;
        check(!unify(A("p", "?x", "?x"), A("p", "a", "b"), b5),
              "the same variable twice cannot take two values");
        Binding b6;
        check(unify(A("p", "?x", "?x"), A("p", "a", "a"), b6),
              "but does unify when the two agree");

        // Chained bindings must be followed all the way.
        Binding b7;
        b7["?x"] = Term::variable("?y");
        b7["?y"] = Term::atom("z");
        check(resolve(Term::variable("?x"), b7).name == "z",
              "a variable bound to a variable resolves through to the atom");
    }

    // --- FACTS ---------------------------------------------------------------
    {
        Engine e;
        e.fact("parent", "abe", "homer");
        e.fact("parent", "homer", "bart");
        check(e.holds(A("parent", "abe", "homer")), "a stated fact holds");
        check(!e.holds(A("parent", "bart", "abe")), "an unstated one does not");

        const auto kids = e.ask(A("parent", "homer", "?who"));
        check(kids.size() == 1 && bound_to(kids, "?who", "bart"),
              "a query with a variable returns the binding");
    }

    // --- A RULE THE SYSTEM WAS TOLD, NOT ONE COMPILED IN ---------------------
    {
        Engine e;
        e.fact("made_of", "vase", "glass");
        e.fact("thin",    "vase", "yes");
        e.fact("made_of", "bar",  "steel");
        e.fact("thin",    "bar",  "yes");

        // fragile(X) if X is made of glass and X is thin.
        e.rule(Rule{A("fragile", "?x", "yes"),
                    {A("made_of", "?x", "glass"), A("thin", "?x", "yes")},
                    "fragility"});

        check(e.holds(A("fragile", "vase", "yes")), "a told rule fires");
        check(!e.holds(A("fragile", "bar", "yes")),
              "and does not fire when a premise fails");

        const auto all = e.ask(A("fragile", "?what", "yes"));
        check(all.size() == 1 && bound_to(all, "?what", "vase"),
              "and the rule can be run backwards to find what satisfies it");
    }

    // --- LEFT RECURSION, WHICH NAIVE RESOLVERS HANG ON -----------------------
    {
        Engine e;
        e.fact("parent", "abe",  "homer");
        e.fact("parent", "homer", "bart");
        e.fact("parent", "bart", "maggie");

        e.rule(Rule{A("ancestor", "?x", "?y"), {A("parent", "?x", "?y")}, "anc-base"});
        // The dangerous shape: the recursive goal comes FIRST.
        e.rule(Rule{A("ancestor", "?x", "?y"),
                    {A("ancestor", "?x", "?z"), A("parent", "?z", "?y")},
                    "anc-step"});

        check(e.holds(A("ancestor", "abe", "homer"), 10), "a one-step ancestor holds");
        check(e.holds(A("ancestor", "abe", "bart"),   10), "and a two-step one");
        check(e.holds(A("ancestor", "abe", "maggie"), 10), "and a three-step one");
        check(!e.holds(A("ancestor", "maggie", "abe"), 10),
              "and the relation is not symmetric");

        const auto desc = e.ask(A("ancestor", "abe", "?who"), 10);
        check(desc.size() == 3, "all three descendants are found, and no duplicates");
    }

    // --- ONE RULE USED TWICE IN ONE PROOF ------------------------------------
    //
    // If variables are not renamed per use, the second application inherits the
    // first's bindings and the proof silently fails. This is the defect that
    // presents as "the rule only works once".
    {
        Engine e;
        e.fact("edge", "a", "b");
        e.fact("edge", "b", "c");
        e.fact("edge", "c", "d");
        e.rule(Rule{A("path", "?x", "?y"), {A("edge", "?x", "?y")}, "path-base"});
        e.rule(Rule{A("path", "?x", "?y"),
                    {A("edge", "?x", "?z"), A("path", "?z", "?y")}, "path-step"});

        check(e.holds(A("path", "a", "d"), 12),
              "a proof that applies one rule three times succeeds");
        const auto reach = e.ask(A("path", "a", "?to"), 12);
        check(reach.size() == 3, "and every reachable node is found");
    }

    // --- IT REPORTS THE DERIVATION -------------------------------------------
    {
        Engine e;
        e.fact("made_of", "vase", "glass");
        e.fact("thin",    "vase", "yes");
        e.rule(Rule{A("fragile", "?x", "yes"),
                    {A("made_of", "?x", "glass"), A("thin", "?x", "yes")}, "fragility"});
        const std::string why = e.explain(A("fragile", "?what", "yes"));
        std::printf("      %s\n", why.c_str());
        check(why.find("fragility") != std::string::npos,
              "the derivation names the rule that produced the answer");
        check(why.find("vase") != std::string::npos, "and the binding it found");
        check(e.explain(A("fragile", "anvil", "yes")).empty(),
              "and an unprovable goal explains nothing rather than something");
    }

    // --- WHAT THIS DOES THAT LIGATURE CANNOT ---------------------------------
    //
    // Ligature::deduce covers exactly two inference patterns, both written into
    // the C++: property inheritance down is-a, and causal chaining. Everything
    // below is outside both, and is the reason a rule engine is not redundant
    // with it.
    {
        Engine e;
        e.fact("is_a",   "socrates", "man");
        e.fact("mortal", "man",      "yes");
        // The syllogism as a TOLD rule rather than a compiled one.
        e.rule(Rule{A("mortal", "?x", "yes"),
                    {A("is_a", "?x", "?k"), A("mortal", "?k", "yes")}, "syllogism"});
        check(e.holds(A("mortal", "socrates", "yes")), "the syllogism, as a rule Khora was told");

        // A three-premise rule, which no hardcoded pattern covers.
        Engine f;
        f.fact("has",   "engine", "fuel");
        f.fact("has",   "engine", "spark");
        f.fact("has",   "engine", "air");
        f.rule(Rule{A("runs", "?m", "yes"),
                    {A("has", "?m", "fuel"), A("has", "?m", "spark"), A("has", "?m", "air")},
                    "combustion"});
        check(f.holds(A("runs", "engine", "yes")), "a three-premise rule");

        Engine g;
        g.fact("has", "engine", "fuel");
        g.fact("has", "engine", "spark");
        g.rule(Rule{A("runs", "?m", "yes"),
                    {A("has", "?m", "fuel"), A("has", "?m", "spark"), A("has", "?m", "air")},
                    "combustion"});
        check(!g.holds(A("runs", "engine", "yes")),
              "and it fails when one of the three is missing");
    }

    // --- IT DOES NOT PROVE THINGS THAT ARE FALSE -----------------------------
    //
    // The failure worth fearing is not "cannot prove X", it is "proves X anyway".
    {
        Engine e;
        e.fact("likes", "alice", "cake");
        e.fact("likes", "bob",   "tea");
        e.rule(Rule{A("shares", "?x", "?y"),
                    {A("likes", "?x", "?t"), A("likes", "?y", "?t")}, "shared-taste"});

        check(e.holds(A("shares", "alice", "alice")),
              "someone trivially shares a taste with themselves");
        check(!e.holds(A("shares", "alice", "bob")),
              "but two people who like different things do NOT share a taste");

        e.fact("likes", "bob", "cake");
        check(e.holds(A("shares", "alice", "bob")),
              "and they do once a common taste is stated");
    }

    std::printf("\n");
    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
