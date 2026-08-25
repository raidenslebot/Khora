#pragma once

// LOGOS — rules with variables, and a resolver that follows them.
//
// A capability audit of this tree found formal reasoning absent: no unification,
// no resolution, no SAT, no SMT. The nearest thing was `Ligature::deduce`, which
// is bounded transitive closure over two relations with the inference rules
// WRITTEN INTO THE C++:
//
//     subject is-a A, A has Z   =>   subject has Z
//     subject causes Y, Y causes Z  =>  subject causes Z
//
// Those two rules are useful and they are the only two the system will ever
// have. Khora cannot be told "a thing is fragile if it is made of glass and is
// thin", cannot be told a rule it read in a book, and cannot derive a rule and
// then use it. Every act of reasoning it performs was compiled in by hand.
//
// This is the other kind of reasoning, and it is a genuinely different paradigm
// from everything else here: the rest of Khora is 10,000-bit hypervectors and
// statistics over what co-occurs, which is very good at "these things go
// together" and cannot do "for ALL x, if P(x) then Q(x)". Symbolic inference is
// what covers that, and the two are complementary rather than competing --
// association proposes, resolution proves.
//
// WHAT IT IS. Horn clauses over binary relations, solved by backward chaining
// with unification -- SLD resolution, the core of Prolog -- with a DEPTH BOUND
// and nothing else keeping it finite. A query returns every binding that
// satisfies it, and the proof that produced each one.
//
// It originally carried a repeated-goal check as well, on the reasoning that
// left-recursive rules loop without one. That was wrong in the precise case it
// was written for: under `ancestor(?x,?y) :- ancestor(?x,?z), parent(?z,?y)` the
// subgoal keys identically to the parent goal because both have an unbound
// second argument, so the check refused the recursion it was meant to permit and
// ancestors stopped at depth two. Doing it properly is tabling, which is a much
// larger thing; the depth bound already guarantees termination, so the check is
// gone and the cost is redundant exploration inside the bound.
//
// WHAT IT IS NOT. No negation, so no non-monotonic reasoning and none of the
// semantic difficulty that comes with it. No function symbols, so terms are
// atoms or variables and unification cannot build structure -- which also means
// no occurs check is needed and no term can grow without bound. It is decidable
// on a finite fact base for that reason, and the depth bound is belt and braces
// rather than the thing keeping it honest.
//
// A rule that cannot be checked is a rule that will be believed wrongly, so the
// resolver returns the derivation with every answer, and `explain` renders it.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace khora::logos {

// A term is an atom ("socrates") or a variable ("?x"). Variables are marked by a
// leading '?' rather than by convention on capitalisation, because the concepts
// coming out of Ligature are lower-case words and a capitalisation rule would
// silently turn one of them into a variable.
struct Term {
    std::string name;
    bool        var = false;

    static Term atom(std::string n)     { return Term{std::move(n), false}; }
    static Term variable(std::string n) { return Term{std::move(n), true}; }
    static Term parse(const std::string& s) {
        return (!s.empty() && s[0] == '?') ? variable(s) : atom(s);
    }
    bool operator==(const Term& o) const { return var == o.var && name == o.name; }
};

// relation(subject, object). Binary because that is what the knowledge layer
// holds; widening it later is a change to this struct and nothing else.
struct Atom {
    std::string relation;
    Term        subject;
    Term        object;

    bool ground() const { return !subject.var && !object.var; }
    std::string str() const;
};

// head :- body. An empty body is a fact.
struct Rule {
    Atom              head;
    std::vector<Atom> body;
    std::string       name;      // for the derivation trace
};

using Binding = std::unordered_map<std::string, Term>;

// One answer: the bindings that satisfy the query, and the rules used to get
// there, outermost first.
struct Answer {
    Binding                  bind;
    std::vector<std::string> used;
};

class Engine {
public:
    // A fact is a rule with no body. Kept separate in the API because that is
    // how callers think about it, and because facts vastly outnumber rules.
    void fact(const std::string& relation, const std::string& subj,
              const std::string& obj);
    void rule(Rule r);

    // Every binding that satisfies the goal. `max_depth` bounds the resolution
    // and `max_answers` bounds the result, because a rule base with a cycle in
    // it can otherwise enumerate for a very long time even when it terminates.
    std::vector<Answer> ask(const Atom& goal, int max_depth = 8,
                            std::size_t max_answers = 64) const;

    // Whether the goal holds at all. Cheaper than ask() when the bindings do not
    // matter, because it stops at the first answer.
    bool holds(const Atom& goal, int max_depth = 8) const;

    // A rendered derivation for the first answer, or "" if there is none.
    std::string explain(const Atom& goal, int max_depth = 8) const;

    std::size_t fact_count() const noexcept { return facts_.size(); }
    std::size_t rule_count() const noexcept { return rules_.size(); }

private:
    std::vector<Atom> facts_;
    std::vector<Rule> rules_;

    void solve(const std::vector<Atom>& goals, Binding bind, int depth,
               std::vector<std::string> trail,
               std::vector<Answer>& out, std::size_t max_answers) const;
};

// Unification of two atoms under existing bindings. Exposed because it is the
// one piece worth testing on its own -- almost every defect in a resolver is
// really a defect here.
bool unify(const Atom& a, const Atom& b, Binding& bind);

// Follow a term through the bindings to whatever it is ultimately bound to.
Term resolve(const Term& t, const Binding& bind);

} // namespace khora::logos
