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

        // IS-A:  X is/are/was/were  a/an/the  ... Y(head noun). The determiner is
        // REQUIRED — it marks a noun phrase ("is a Y"), excluding passive/predicate
        // forms ("is reflected", "is sufficient") that are not taxonomy.
        if ((w == "is" || w == "are" || w == "was" || w == "were") &&
            i + 1 < t.size() && is_det(t[i + 1])) {
            const std::string Y = head_after(t, i + 1);
            if (is_content(X) && !Y.empty()) { add(Relation::IsA, X, Y); ++added; }
        }
        // CAUSES:  X causes/produces/creates/generates  [det] ... Y
        else if (w == "causes" || w == "cause" || w == "produces" || w == "produce" ||
                 w == "creates" || w == "create" || w == "generates" || w == "generate" ||
                 w == "yields"  || w == "induce"  || w == "induces") {
            const std::string Y = head_after(t, i + 1);
            if (is_content(X) && !Y.empty()) { add(Relation::Causes, X, Y); ++added; }
        }
        // CAUSES:  X leads/led/lead  to  [det] ... Y
        else if ((w == "leads" || w == "led" || w == "lead") &&
                 i + 2 < t.size() && t[i + 1] == "to") {
            const std::string Y = head_after(t, i + 2);
            if (is_content(X) && !Y.empty()) { add(Relation::Causes, X, Y); ++added; }
        }
        // HAS-PART:  X has/have/contains/contain/comprises  [det] ... Y
        else if (w == "has" || w == "have" || w == "contains" || w == "contain" ||
                 w == "comprises" || w == "comprise" || w == "includes") {
            const std::string Y = head_after(t, i + 1);
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
