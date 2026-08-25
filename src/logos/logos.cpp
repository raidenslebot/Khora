#include "khora/logos/logos.hpp"

#include <algorithm>
#include <sstream>

namespace khora::logos {

std::string Atom::str() const {
    return relation + "(" + subject.name + ", " + object.name + ")";
}

Term resolve(const Term& t, const Binding& bind) {
    // Chase the chain: ?x may be bound to ?y which is bound to an atom. A single
    // lookup would return ?y and quietly treat it as an answer.
    Term cur = t;
    for (int guard = 0; guard < 64 && cur.var; ++guard) {
        const auto it = bind.find(cur.name);
        if (it == bind.end()) break;
        if (it->second == cur) break;
        cur = it->second;
    }
    return cur;
}

namespace {

bool unify_term(const Term& a, const Term& b, Binding& bind) {
    const Term x = resolve(a, bind);
    const Term y = resolve(b, bind);
    if (!x.var && !y.var) return x.name == y.name;
    if (x.var) { if (!(x == y)) bind[x.name] = y; return true; }
    bind[y.name] = x;
    return true;
}

// Every use of a rule needs its own variables, or two uses in one proof share
// bindings and the second silently constrains the first. This is the classic
// defect in a hand-written resolver and it presents as "the rule only works
// once".
Rule freshen(const Rule& r, std::size_t nonce) {
    const std::string tag = "#" + std::to_string(nonce);
    auto ren = [&tag](Term t) {
        if (t.var) t.name += tag;
        return t;
    };
    Rule out;
    out.name = r.name;
    out.head = Atom{r.head.relation, ren(r.head.subject), ren(r.head.object)};
    out.body.reserve(r.body.size());
    for (const Atom& a : r.body) {
        out.body.push_back(Atom{a.relation, ren(a.subject), ren(a.object)});
    }
    return out;
}

std::string key_of(const Atom& g, const Binding& b) {
    const Term s = resolve(g.subject, b);
    const Term o = resolve(g.object, b);
    return g.relation + "|" + (s.var ? "?" : s.name) + "|" + (o.var ? "?" : o.name);
}

} // namespace

bool unify(const Atom& a, const Atom& b, Binding& bind) {
    if (a.relation != b.relation) return false;
    Binding trial = bind;
    if (!unify_term(a.subject, b.subject, trial)) return false;
    if (!unify_term(a.object,  b.object,  trial)) return false;
    bind = std::move(trial);
    return true;
}

void Engine::fact(const std::string& relation, const std::string& subj,
                  const std::string& obj) {
    facts_.push_back(Atom{relation, Term::parse(subj), Term::parse(obj)});
}

void Engine::rule(Rule r) { rules_.push_back(std::move(r)); }

void Engine::solve(const std::vector<Atom>& goals, Binding bind, int depth,
                   std::vector<std::string> trail,
                   std::vector<Answer>& out, std::size_t max_answers) const {
    if (out.size() >= max_answers) return;
    if (goals.empty()) {
        out.push_back(Answer{std::move(bind), std::move(trail)});
        return;
    }
    if (depth <= 0) return;

    const Atom goal = goals.front();
    const std::vector<Atom> rest(goals.begin() + 1, goals.end());

    // NO REPEATED-GOAL CHECK, and the test is why.
    //
    // The obvious optimisation is to refuse a goal already on the stack, and this
    // resolver had one. It is WRONG for exactly the case it was written for.
    // Under `ancestor(?x,?y) :- ancestor(?x,?z), parent(?z,?y)` the subgoal
    // `ancestor(abe,?z)` keys identically to the parent goal `ancestor(abe,?y)`,
    // because both have an unbound second argument -- so the check refused the
    // recursion it was meant to permit and left recursion stopped at depth two.
    // Two tests caught it: three-step ancestors failed and only two of three
    // descendants were found, while the right-recursive form passed throughout,
    // which is the signature of this bug rather than of a broken unifier.
    //
    // Distinguishing "the same goal" from "a more general goal" properly is
    // tabling -- SLG resolution -- and that is a substantially larger thing than
    // this needs. The depth bound already guarantees termination, including on
    // left recursion; dropping the check costs redundant exploration inside that
    // bound and buys completeness, which is the right way round.

    for (const Atom& f : facts_) {
        Binding b = bind;
        if (unify(goal, f, b)) {
            solve(rest, std::move(b), depth - 1, trail, out, max_answers);
            if (out.size() >= max_answers) break;
        }
    }
    if (out.size() < max_answers) {
        std::size_t nonce = 0;
        for (const Rule& r0 : rules_) {
            ++nonce;
            const Rule r = freshen(r0, static_cast<std::size_t>(depth) * 1000 + nonce);
            Binding b = bind;
            if (!unify(goal, r.head, b)) continue;
            std::vector<Atom> next = r.body;
            next.insert(next.end(), rest.begin(), rest.end());
            std::vector<std::string> t2 = trail;
            t2.push_back(r.name.empty() ? r.head.relation : r.name);
            solve(next, std::move(b), depth - 1, std::move(t2), out, max_answers);
            if (out.size() >= max_answers) break;
        }
    }

}

std::vector<Answer> Engine::ask(const Atom& goal, int max_depth,
                                std::size_t max_answers) const {
    std::vector<Answer> out;
    solve({goal}, Binding{}, max_depth, {}, out, max_answers);

    // Two derivations of the same bindings are one answer as far as a caller is
    // concerned; the first proof found is kept.
    std::vector<Answer> uniq;
    for (const Answer& a : out) {
        bool dup = false;
        for (const Answer& u : uniq) {
            bool same = true;
            for (const auto& kv : a.bind) {
                if (!kv.first.empty() && kv.first.find('#') != std::string::npos) continue;
                const auto it = u.bind.find(kv.first);
                if (it == u.bind.end() || !(resolve(it->second, u.bind) ==
                                            resolve(kv.second, a.bind))) {
                    same = false; break;
                }
            }
            if (same) { dup = true; break; }
        }
        if (!dup) uniq.push_back(a);
    }
    return uniq;
}

bool Engine::holds(const Atom& goal, int max_depth) const {
    return !ask(goal, max_depth, 1).empty();
}

std::string Engine::explain(const Atom& goal, int max_depth) const {
    const auto answers = ask(goal, max_depth, 1);
    if (answers.empty()) return "";
    std::ostringstream os;
    os << goal.str() << " holds";
    if (!answers.front().used.empty()) {
        os << " by ";
        for (std::size_t i = 0; i < answers.front().used.size(); ++i) {
            if (i) os << " then ";
            os << answers.front().used[i];
        }
    } else {
        os << " as a stated fact";
    }
    // Only the caller's own variables are worth reporting; the freshened ones
    // are bookkeeping.
    std::vector<std::string> shown;
    for (const auto& kv : answers.front().bind) {
        if (kv.first.find('#') != std::string::npos) continue;
        shown.push_back(kv.first + " = " + resolve(kv.second, answers.front().bind).name);
    }
    std::sort(shown.begin(), shown.end());
    if (!shown.empty()) {
        os << "  [";
        for (std::size_t i = 0; i < shown.size(); ++i) { if (i) os << ", "; os << shown[i]; }
        os << "]";
    }
    return os.str();
}

} // namespace khora::logos
