#include "khora/ligature/ligature.hpp"

#include <algorithm>
#include <fstream>
#include <unordered_set>

namespace khora::ligature {

const char* relation_name(Relation r) noexcept {
    switch (r) {
        case Relation::IsA:     return "is-a";
        case Relation::Causes:  return "causes";
        case Relation::HasPart: return "has";
        default:                return "?";
    }
}

namespace {

// Words that pass the length>=3 test but are not content concepts. A small
// closed list is appropriate here — we are parsing surface syntax, not meaning.
const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> s = {
        "the","and","that","this","with","for","are","was","were","been","has","have",
        "had","not","but","you","your","its","they","them","their","there","then","than",
        "which","who","whom","whose","what","when","where","into","onto","upon","from",
        "out","off","over","under","very","much","more","most","some","such","like","also",
        "only","even","well","both","each","many","other","being","does","did","will","would",
        "could","should","shall","may","might","must","one","two","any","all","own","same",
        "thus","therefore","hence","because","since","while","though","although","however",
        "yet","still","here","now","ever","never","always","often","again","once","upon"
    };
    return s;
}

inline bool is_content(const std::string& w) {
    return w.size() >= 3 && !stopwords().count(w);
}

inline bool is_det(const std::string& w) {
    return w == "a" || w == "an" || w == "the";
}

// The OBJECT of a relation is the head noun of the noun phrase that follows the
// trigger. In English the head is (usually) the LAST content word of the phrase:
// "a social animal" -> animal, "the great inundation" -> inundation. So skip
// determiners, then take the last word of the run of content words. This drops
// the adjective noise of a naive "first word after 'a'" rule.
inline std::string head_after(const std::vector<std::string>& t, std::size_t j) {
    while (j < t.size() && is_det(t[j])) ++j;
    std::string last;
    std::size_t span = 0;
    while (j < t.size() && span < 5 && is_content(t[j])) { last = t[j]; ++j; ++span; }
    return last;
}

} // namespace

void Ligature::add(Relation r, const std::string& subj, const std::string& obj,
                   std::uint32_t n) {
    if (n == 0 || subj == obj || subj.empty() || obj.empty()) return;
    const std::size_t ri = static_cast<std::size_t>(r);
    auto& f = fwd_[ri][subj];
    const bool fresh = (f.find(obj) == f.end());
    f[obj] += n;
    rev_[ri][obj][subj] += n;
    if (fresh) ++triples_;
    assertions_ += n;
}

std::size_t Ligature::extract(const std::vector<std::string>& t) {
    std::size_t added = 0;
    if (t.size() < 3) return 0;

    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
        const std::string& w = t[i];
        const std::string& X = t[i - 1];   // subject = head word before the verb
        if (!is_content(X)) {
            // still allow Hearst (Y such as X) where the order differs
            if (!(w == "such" && i + 2 < t.size() && t[i + 1] == "as")) continue;
        }

        // IS-A:  X is/are/was/were  a/an/the  [kind/sort/type/form of]  ... Y(head)
        // The determiner is REQUIRED — it marks a noun phrase ("is a Y"), excluding
        // passive/predicate forms ("is reflected", "is sufficient") that aren't taxonomy.
        if ((w == "is" || w == "are" || w == "was" || w == "were") &&
            i + 1 < t.size() && is_det(t[i + 1])) {
            std::size_t ys = i + 2;
            // "a KIND of Y" / "a sort/type/form/species/class/branch of Y" -> skip the meta-noun.
            if (ys + 1 < t.size() &&
                (t[ys] == "kind" || t[ys] == "sort" || t[ys] == "type" || t[ys] == "form" ||
                 t[ys] == "species" || t[ys] == "class" || t[ys] == "branch") &&
                t[ys + 1] == "of") {
                ys += 2;
            }
            const std::string Y = head_after(t, ys);
            if (is_content(X) && !Y.empty()) { add(Relation::IsA, X, Y); ++added; }
        }
        // CAUSES:  X <causal verb>  [det] ... Y
        else if (w == "causes" || w == "cause" || w == "produces" || w == "produce" ||
                 w == "creates" || w == "create" || w == "generates" || w == "generate" ||
                 w == "yields"  || w == "yield"  || w == "induce"  || w == "induces" ||
                 w == "makes"   || w == "make"   || w == "brings"  || w == "bring"   ||
                 w == "drives"  || w == "drive"  || w == "forces"  || w == "enables" ||
                 w == "enable"  || w == "excites" || w == "excite") {
            const std::string Y = head_after(t, i + 1);
            if (is_content(X) && !Y.empty()) { add(Relation::Causes, X, Y); ++added; }
        }
        // CAUSES:  X leads/led/lead/results/give  to/in  ... Y   ("leads to", "results in")
        else if ((w == "leads" || w == "led" || w == "lead" || w == "results" ||
                  w == "result" || w == "gives" || w == "give") &&
                 i + 2 < t.size() && (t[i + 1] == "to" || t[i + 1] == "in" || t[i + 1] == "rise")) {
            std::size_t ys = i + 2;
            if (ys < t.size() && (t[ys] == "to" || t[ys] == "in")) ++ys;   // "rise to"
            const std::string Y = head_after(t, ys);
            if (is_content(X) && !Y.empty()) { add(Relation::Causes, X, Y); ++added; }
        }
        // HAS-PART:  X has/have  DET ... Y      |  X contains/includes/... ... Y
        //
        // has/have/had REQUIRE a determiner, exactly as is/are do above. Without
        // that, English's perfect tense is read as possession: "the time has
        // come" becomes HAS-PART(time, come), "man has got" becomes
        // HAS-PART(man, got). Those are not edge cases -- they were the single
        // largest source of nonsense in the learned relations, with "time has
        // come" asserted ten times and "mind has life/eyes/arms" alongside it.
        // "the cell has a nucleus" still passes; "the time has come" no longer
        // does. The unambiguous verbs need no determiner, since "water contains
        // oxygen" is a real part-whole claim.
        else if (w == "has" || w == "have" || w == "had") {
            if (i + 1 < t.size() && is_det(t[i + 1])) {
                const std::string Y = head_after(t, i + 2);
                if (is_content(X) && !Y.empty()) { add(Relation::HasPart, X, Y); ++added; }
            }
        }
        else if (w == "contains" || w == "contain" ||
                 w == "comprises" || w == "comprise" || w == "includes" ||
                 w == "possesses" || w == "possess" || w == "carries" || w == "carry") {
            const std::string Y = head_after(t, i + 1);
            if (is_content(X) && !Y.empty()) { add(Relation::HasPart, X, Y); ++added; }
        }
        // HAS-PART (material/composition):  X consists/composed/made/formed  of  ... Y
        else if ((w == "consists" || w == "consist" || w == "composed" || w == "made" ||
                  w == "formed" || w == "built" || w == "comprised") &&
                 i + 2 < t.size() && t[i + 1] == "of") {
            const std::string Y = head_after(t, i + 2);
            if (is_content(X) && !Y.empty()) { add(Relation::HasPart, X, Y); ++added; }
        }

        // Hearst:  Y such as X   ->  IS-A(X, Y)
        if (w == "such" && i + 2 < t.size() && t[i + 1] == "as") {
            const std::string& Y = t[i - 1];
            const std::string& Xh = t[i + 2];
            if (is_content(Xh) && is_content(Y)) { add(Relation::IsA, Xh, Y); ++added; }
        }
    }
    return added;
}

void Ligature::absorb(const Ligature& other) {
    for (std::size_t ri = 0; ri < fwd_.size(); ++ri) {
        const Relation r = static_cast<Relation>(ri);
        for (const auto& [subj, objs] : other.fwd_[ri])
            for (const auto& [obj, c] : objs)
                add(r, subj, obj, c);
    }
}

std::vector<std::pair<std::string, std::uint32_t>>
Ligature::objects(Relation r, const std::string& subj, std::size_t k) const {
    std::vector<std::pair<std::string, std::uint32_t>> out;
    const auto& m = fwd_[static_cast<std::size_t>(r)];
    auto it = m.find(subj);
    if (it == m.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > k) out.resize(k);
    return out;
}

std::vector<std::pair<std::string, std::uint32_t>>
Ligature::subjects(Relation r, const std::string& obj, std::size_t k) const {
    std::vector<std::pair<std::string, std::uint32_t>> out;
    const auto& m = rev_[static_cast<std::size_t>(r)];
    auto it = m.find(obj);
    if (it == m.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (out.size() > k) out.resize(k);
    return out;
}

std::uint32_t Ligature::count(Relation r, const std::string& subj, const std::string& obj) const {
    const auto& m = fwd_[static_cast<std::size_t>(r)];
    auto it = m.find(subj);
    if (it == m.end()) return 0;
    auto jt = it->second.find(obj);
    return (jt == it->second.end()) ? 0u : jt->second;
}

bool Ligature::is_a(const std::string& x, const std::string& y, int max_depth) const {
    // Breadth-first over the is-a forward edges (only well-attested ones, count>=2,
    // to avoid chasing parse noise).
    if (x == y) return true;
    const auto& m = fwd_[static_cast<std::size_t>(Relation::IsA)];
    std::unordered_set<std::string> seen{ x };
    std::vector<std::string> frontier{ x };
    for (int d = 0; d < max_depth && !frontier.empty(); ++d) {
        std::vector<std::string> next;
        for (const auto& node : frontier) {
            auto it = m.find(node);
            if (it == m.end()) continue;
            for (const auto& [parent, c] : it->second) {
                if (c < 2) continue;
                if (parent == y) return true;
                if (seen.insert(parent).second) next.push_back(parent);
            }
        }
        frontier.swap(next);
    }
    return false;
}

std::vector<Inference> Ligature::deduce(const std::string& subject, int max_depth) const {
    std::vector<Inference> out;
    if (subject.empty()) return out;

    const auto& isaF = fwd_[static_cast<std::size_t>(Relation::IsA)];

    // Generic / pronominal "is-a" parents carry no inheritable structure — "man
    // is-a thing" should not let man inherit whatever "thing" happens to have.
    static const std::unordered_set<std::string> generic = {
        "thing","things","one","ones","way","ways","part","parts","sort","kind",
        "type","him","her","them","those","these","something","someone","somebody",
        "anyone","anything","everyone","nothing","person","matter","point","case",
        "form","name","word","term","number","piece","set","group","sort"
    };

    // (1) Gather the is-a ANCESTORS of the subject (BFS over well-attested is-a
    //     edges), remembering the path and the weakest link's support.
    struct Anc { std::string node; std::vector<std::string> path; std::uint32_t support; };
    std::vector<Anc> ancestors;
    std::unordered_set<std::string> seen{ subject };
    std::vector<Anc> frontier{ { subject, {}, 0xFFFFFFFFu } };
    for (int d = 0; d < max_depth && !frontier.empty(); ++d) {
        std::vector<Anc> next;
        for (const auto& a : frontier) {
            auto it = isaF.find(a.node);
            if (it == isaF.end()) continue;
            for (const auto& [parent, c] : it->second) {
                if (c < 2 || generic.count(parent)) continue;
                if (!seen.insert(parent).second) continue;
                Anc na;
                na.node = parent;
                na.path = a.path;
                na.path.push_back(parent);
                na.support = std::min(a.support, c);
                ancestors.push_back(na);
                next.push_back(std::move(na));
            }
        }
        frontier.swap(next);
    }

    auto known_direct = [&](Relation r, const std::string& o) { return count(r, subject, o) > 0; };

    // (2) Inherit HAS and CAUSES down the taxonomy: subject is-a A, A has/causes Z.
    for (const Relation r : { Relation::HasPart, Relation::Causes }) {
        for (const auto& anc : ancestors) {
            for (const auto& [obj, c] : objects(r, anc.node, 8)) {
                if (c < 2 || obj == subject || known_direct(r, obj)) continue;
                Inference inf;
                inf.relation = r;
                inf.object   = obj;
                inf.via      = anc.path;
                inf.support  = std::min(anc.support, c);
                out.push_back(std::move(inf));
            }
        }
    }

    // (3) Causal chaining: subject causes Y, Y causes Z  =>  subject causes Z.
    for (const auto& [y, c1] : objects(Relation::Causes, subject, 8)) {
        if (c1 < 2 || generic.count(y)) continue;
        for (const auto& [z, c2] : objects(Relation::Causes, y, 6)) {
            if (c2 < 2 || z == subject || generic.count(z) || known_direct(Relation::Causes, z)) continue;
            Inference inf;
            inf.relation = Relation::Causes;
            inf.object   = z;
            inf.via      = { y };
            inf.support  = std::min(c1, c2);
            out.push_back(std::move(inf));
        }
    }

    // Strongest first, dedup by (relation, object), cap.
    std::sort(out.begin(), out.end(),
              [](const Inference& a, const Inference& b) { return a.support > b.support; });
    std::vector<Inference> uniq;
    std::unordered_set<std::string> dk;
    for (auto& i : out) {
        const std::string k = std::to_string(static_cast<int>(i.relation)) + "|" + i.object;
        if (dk.insert(k).second) uniq.push_back(std::move(i));
    }
    if (uniq.size() > 16) uniq.resize(16);
    return uniq;
}

double Ligature::benchmark_deduction(std::size_t n, std::uint64_t seed) const {
    const auto& isa = fwd_[static_cast<std::size_t>(Relation::IsA)];
    if (isa.empty() || n == 0) return -1.0;

    // The benchmark must be FAIR: test only facts deduce() is CONTRACTED to derive —
    // support >= 2 on both links (one-offs are noise it rightly ignores) and a
    // non-generic is-a parent (generic parents carry no inheritable structure). A
    // score here is true recall over in-contract facts, not a strawman.
    static const std::unordered_set<std::string> generic = {
        "thing","things","one","ones","way","ways","part","parts","sort","kind",
        "type","him","her","them","those","these","something","someone","somebody",
        "anyone","anything","everyone","nothing","person","matter","point","case",
        "form","name","word","term","number","piece","set","group"
    };

    std::vector<const std::string*> subs;
    subs.reserve(isa.size());
    for (const auto& kv : isa) subs.push_back(&kv.first);
    const std::size_t N = subs.size();

    std::size_t total = 0, recovered = 0;
    for (std::size_t s = 0; s < n; ++s) {
        const std::string& X =
            *subs[(s * 2654435761ull + seed * 1099511628211ull + 1) % N];

        bool constructed = false;
        for (const auto& [A, ca] : objects(Relation::IsA, X, 4)) {
            if (A == X || ca < 2 || generic.count(A)) continue;   // deduce's preconditions
            for (const Relation rel : { Relation::Causes, Relation::HasPart }) {
                for (const auto& [Z, cz] : objects(rel, A, 4)) {
                    if (Z == X || Z == A || cz < 2) continue;
                    if (count(rel, X, Z) > 0) continue;   // already direct — not a derivation
                    // X --is-a--> A --rel--> Z, all well-attested: deduce SHOULD derive X rel Z.
                    ++total;
                    constructed = true;
                    bool found = false;
                    for (const auto& inf : deduce(X, 3)) {
                        if (inf.relation == rel && inf.object == Z) { found = true; break; }
                    }
                    if (found) ++recovered;
                    break;
                }
                if (constructed) break;
            }
            if (constructed) break;
        }
    }
    return total ? static_cast<double>(recovered) / static_cast<double>(total) : -1.0;
}

// ---- Persistence: a compact text file <prefix>.lig --------------------------
//   one line per triple:  relIndex<TAB>subject<TAB>object<TAB>count

void Ligature::save(const std::filesystem::path& prefix) const {
    namespace fs = std::filesystem;
    if (prefix.has_parent_path()) fs::create_directories(prefix.parent_path());
    auto path = prefix; path += ".lig";
    std::ofstream os(path, std::ios::trunc);
    if (!os) return;
    for (std::size_t ri = 0; ri < fwd_.size(); ++ri)
        for (const auto& [subj, objs] : fwd_[ri])
            for (const auto& [obj, c] : objs)
                os << ri << '\t' << subj << '\t' << obj << '\t' << c << '\n';
}

void Ligature::load(const std::filesystem::path& prefix) {
    namespace fs = std::filesystem;
    auto path = prefix; path += ".lig";
    if (!fs::exists(path)) return;
    for (auto& m : fwd_) m.clear();
    for (auto& m : rev_) m.clear();
    triples_ = 0; assertions_ = 0;
    std::ifstream is(path);
    std::string line;
    while (std::getline(is, line)) {
        const auto t1 = line.find('\t');
        const auto t2 = (t1 == std::string::npos) ? t1 : line.find('\t', t1 + 1);
        const auto t3 = (t2 == std::string::npos) ? t2 : line.find('\t', t2 + 1);
        if (t3 == std::string::npos) continue;
        try {
            const int ri = std::stoi(line.substr(0, t1));
            if (ri < 0 || ri >= static_cast<int>(Relation::_Count)) continue;
            const std::string subj = line.substr(t1 + 1, t2 - t1 - 1);
            const std::string obj  = line.substr(t2 + 1, t3 - t2 - 1);
            const auto c = static_cast<std::uint32_t>(std::stoul(line.substr(t3 + 1)));
            add(static_cast<Relation>(ri), subj, obj, c);
        } catch (...) {}
    }
}

} // namespace khora::ligature
