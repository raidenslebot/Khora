#include "khora/ribosome/ribosome.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <unordered_map>

namespace khora::ribosome {
namespace {

inline std::uint64_t splitmix(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

inline double unit(std::uint64_t& s) noexcept {
    return static_cast<double>(splitmix(s) >> 11) / 9007199254740992.0;
}

const char* op_name(Op o) {
    switch (o) {
        case Op::Nop:    return "nop";
        case Op::Copy:   return "copy";
        case Op::Bind:   return "bind";
        case Op::Bundle: return "bundle";
        case Op::Perm:   return "perm";
        case Op::Role:   return "role";
        case Op::And:    return "and";
        case Op::Or:     return "or";
        case Op::Clean:  return "clean";
        case Op::Assoc:  return "assoc";
        case Op::Neigh:  return "neigh";
        case Op::Common: return "common";
        case Op::Kin:    return "kin";
        default:         return "?";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Genome
// ---------------------------------------------------------------------------

Genome Genome::random(std::size_t codons, std::uint64_t seed) {
    std::vector<std::uint8_t> t(codons * 4);
    std::uint64_t s = seed | 1ULL;
    for (auto& b : t) b = static_cast<std::uint8_t>(splitmix(s) & 0xFF);
    return Genome(std::move(t));
}

std::vector<Codon> Genome::decode() const {
    // TOTAL decode. Opcodes are taken modulo the instruction count and register
    // fields modulo the register count, so there is no byte string that fails to
    // be a program. That is the property that makes mutation closed.
    std::vector<Codon> out;
    out.reserve(tape_.size() / 4);
    for (std::size_t i = 0; i + 3 < tape_.size(); i += 4) {
        Codon c;
        // Rejection-free debias. 256 % 13 == 9, so a plain modulo gives opcodes
        // 0-8 a 20/256 share and 9-12 only 19/256 — and 9-12 are exactly
        // {Assoc, Neigh, Common, Kin}, the only four opcodes with a gradient.
        // Scaling instead of wrapping spreads the remainder evenly rather than
        // dropping it all on the tail of the enum.
        c.op  = static_cast<Op>((static_cast<std::uint32_t>(tape_[i]) *
                                 static_cast<std::uint32_t>(Op::kCount)) >> 8);
        c.dst = tape_[i + 1] % kRegisters;
        c.a   = tape_[i + 2] % kRegisters;
        // `b` stays a full byte: Perm reads it as a signed shift and Role as a
        // role index, and both want the whole range.
        c.b   = tape_[i + 3];
        out.push_back(c);
    }
    return out;
}

Genome Genome::replicate(std::uint64_t seed, double rate) const {
    std::uint64_t s = seed | 1ULL;
    std::vector<std::uint8_t> t;
    t.reserve(tape_.size() + 4);
    for (const std::uint8_t b : tape_) {
        if (unit(s) < rate) {
            t.push_back(static_cast<std::uint8_t>(splitmix(s) & 0xFF));
        } else {
            t.push_back(b);
        }
    }
    // Indels, at a tenth of the substitution rate and always a whole codon, so
    // the reading frame is preserved. A substitution-only operator can never
    // change program LENGTH, which would fix the shape of every solution the
    // search is able to reach.
    if (unit(s) < rate * 0.1 && t.size() >= 8) {
        const std::size_t at = (splitmix(s) % (t.size() / 4)) * 4;
        t.erase(t.begin() + static_cast<std::ptrdiff_t>(at),
                t.begin() + static_cast<std::ptrdiff_t>(at + 4));
    } else if (unit(s) < rate * 0.1 && t.size() < 256) {
        const std::size_t at = (splitmix(s) % (t.size() / 4 + 1)) * 4;
        std::array<std::uint8_t, 4> c{};
        for (auto& x : c) x = static_cast<std::uint8_t>(splitmix(s) & 0xFF);
        t.insert(t.begin() + static_cast<std::ptrdiff_t>(at), c.begin(), c.end());
    }
    return Genome(std::move(t));
}

Genome Genome::cross(const Genome& x, const Genome& y, std::uint64_t seed) {
    std::uint64_t s = seed | 1ULL;
    const std::size_t cx = x.codons(), cy = y.codons();
    if (cx == 0) return y;
    if (cy == 0) return x;
    // A separate cut in each parent, so offspring length is not pinned to
    // either parent's -- single-point crossover with a shared index silently
    // conserves length across the whole population.
    const std::size_t a = (splitmix(s) % cx) * 4;
    const std::size_t b = (splitmix(s) % cy) * 4;
    std::vector<std::uint8_t> t;
    t.insert(t.end(), x.tape().begin(), x.tape().begin() + static_cast<std::ptrdiff_t>(a));
    t.insert(t.end(), y.tape().begin() + static_cast<std::ptrdiff_t>(b), y.tape().end());
    if (t.empty()) t = x.tape();
    return Genome(std::move(t));
}

std::size_t Genome::output_register() const {
    std::size_t out = 0;
    for (const Codon& c : decode()) {
        if (c.op != Op::Nop) out = c.dst;
    }
    return out;
}

// BACKWARD LIVENESS. Start with the output register live at the end and walk
// backwards: an instruction is live only if its destination is live at that
// point, and if it is, its sources become live before it. Everything else is
// non-coding.
//
// This is not cosmetic. Reading a champion without the marking is how a program
// that ignores its own input gets mistaken for an operator, which is exactly
// what happened on the first hypernym run here.
std::vector<bool> Genome::live_mask() const {
    const auto code = decode();
    std::vector<bool> live(code.size(), false);
    std::array<bool, kRegisters> reg{};
    reg[output_register()] = true;     // the last register written is the answer
    for (std::size_t k = code.size(); k-- > 0;) {
        const Codon& c = code[k];
        if (c.op == Op::Nop) continue;
        if (!reg[c.dst]) continue;     // result is never read: non-coding
        live[k] = true;
        // The destination is redefined here, so anything earlier writing it is
        // only needed if this instruction also reads it.
        const bool reads_dst = (c.a == c.dst) ||
            (c.b % kRegisters == c.dst &&
             (c.op == Op::Bind || c.op == Op::Bundle || c.op == Op::And ||
              c.op == Op::Or || c.op == Op::Common));
        if (!reads_dst) reg[c.dst] = false;
        reg[c.a] = true;
        if (c.op == Op::Bind || c.op == Op::Bundle || c.op == Op::And ||
            c.op == Op::Or || c.op == Op::Common) {
            reg[c.b % kRegisters] = true;
        }
    }
    return live;
}

std::size_t Genome::effective_length() const {
    const auto m = live_mask();
    std::size_t n = 0;
    for (const bool b : m) if (b) ++n;
    return n;
}

std::string Genome::disassemble() const {
    std::string out;
    char line[112];
    const auto live = live_mask();
    std::size_t i = 0;
    for (const Codon& c : decode()) {
        switch (c.op) {
            case Op::Perm:
                std::snprintf(line, sizeof line, "%2zu  perm   r%u <- r%u >> %d\n",
                              i, c.dst, c.a, static_cast<int>(c.b) - 128);
                break;
            case Op::Role:
                std::snprintf(line, sizeof line, "%2zu  role   r%u <- r%u * ROLE[%u]\n",
                              i, c.dst, c.a, c.b);
                break;
            case Op::Assoc:
                std::snprintf(line, sizeof line, "%2zu  assoc  r%u <- associate[%u] of r%u\n",
                              i, c.dst, static_cast<unsigned>(c.b % 8), c.a);
                break;
            case Op::Neigh:
                std::snprintf(line, sizeof line, "%2zu  neigh  r%u <- bundle top %u of r%u\n",
                              i, c.dst, static_cast<unsigned>((c.b % 8) + 1), c.a);
                break;
            case Op::Copy: case Op::Clean: case Op::Kin:
                std::snprintf(line, sizeof line, "%2zu  %-6s r%u <- r%u\n",
                              i, op_name(c.op), c.dst, c.a);
                break;
            case Op::Nop:
                std::snprintf(line, sizeof line, "%2zu  nop\n", i);
                break;
            default:
                std::snprintf(line, sizeof line, "%2zu  %-6s r%u <- r%u, r%u\n",
                              i, op_name(c.op), c.dst, c.a, c.b % kRegisters);
                break;
        }
        // Strip the trailing newline so the liveness marker can be appended.
        std::string l(line);
        while (!l.empty() && (l.back() == 0x0A || l.back() == 0x0D)) l.pop_back();
        while (l.size() < 44) l += ' ';
        out += l;
        out += (i < live.size() && live[i]) ? "  <- LIVE" : "  (dead)";
        out.push_back(0x0A);
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Codebook
// ---------------------------------------------------------------------------

namespace {
// A 64-bit digest of a Glyph, for exact-match lookup only. Collisions are
// checked against the stored vector before being trusted, so a collision costs
// a comparison and never a wrong answer.
std::uint64_t glyph_digest(const lattice::Glyph& g) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto w : g.words()) { h ^= w; h *= 1099511628211ULL; }
    return h;
}
} // namespace

void Codebook::add(std::string name, const lattice::Glyph& g) {
    exact_.emplace(glyph_digest(g), static_cast<std::uint32_t>(items_.size()));
    items_.emplace_back(std::move(name), g);
}

void Codebook::set_class(std::size_t i, int c) {
    if (class_.size() < items_.size()) class_.resize(items_.size(), -1);
    if (i < class_.size()) class_[i] = c;
}

int Codebook::class_of(std::size_t i) const {
    return (i < class_.size()) ? class_[i] : -1;
}

void Codebook::link(std::size_t from, std::size_t to) {
    if (adj_.size() < items_.size()) adj_.resize(items_.size());
    if (from < adj_.size()) adj_[from].push_back(static_cast<std::uint32_t>(to));
}

const std::vector<std::uint32_t>& Codebook::links(std::size_t i) const {
    static const std::vector<std::uint32_t> kNone;
    return (i < adj_.size()) ? adj_[i] : kNone;
}

void Codebook::precompute_kin() {
    kin_.assign(items_.size(), static_cast<std::uint32_t>(-1));
    if (adj_.size() < items_.size()) adj_.resize(items_.size());

    // Reverse index: who points at each item. Walking it turns the O(N^2)
    // all-pairs neighbourhood comparison into a pass over edges.
    std::vector<std::vector<std::uint32_t>> rev(items_.size());
    for (std::size_t i = 0; i < adj_.size(); ++i) {
        for (const std::uint32_t t : adj_[i]) {
            if (t < rev.size()) rev[t].push_back(static_cast<std::uint32_t>(i));
        }
    }

    std::vector<std::uint32_t> shared(items_.size(), 0);
    std::vector<std::uint32_t> touched;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        touched.clear();
        for (const std::uint32_t n : adj_[i]) {
            for (const std::uint32_t j : rev[n]) {
                if (j == i) continue;
                if (shared[j] == 0) touched.push_back(j);
                ++shared[j];
            }
        }
        std::uint32_t best = static_cast<std::uint32_t>(-1), bc = 0;
        for (const std::uint32_t j : touched) {
            if (shared[j] > bc) { bc = shared[j]; best = j; }
        }
        kin_[i] = best;
        for (const std::uint32_t j : touched) shared[j] = 0;
    }
}

std::size_t Codebook::kin(std::size_t i) const {
    if (i >= kin_.size() || kin_[i] == static_cast<std::uint32_t>(-1)) {
        return static_cast<std::size_t>(-1);
    }
    return kin_[i];
}

// The item in BOTH neighbourhoods with the best combined rank.
//
// The first version returned the first element of a's list found anywhere in
// b's, which is not an intersection operator at all -- it is a's ordering with a
// membership filter, and it made common(i, i) identical to assoc(i, 0). An
// exhaustive scan confirmed that: common r0<-r0,r0 and assoc[0] r0<-r0 produced
// bit-identical results, so a quarter of all Common codons were a redundant
// re-spelling of an opcode that already existed.
std::size_t Codebook::common(std::size_t i, std::size_t j) const {
    const auto& a = links(i);
    const auto& b = links(j);
    if (a.empty() || b.empty()) return static_cast<std::size_t>(-1);
    std::size_t best = static_cast<std::size_t>(-1), best_rank = static_cast<std::size_t>(-1);
    for (std::size_t x = 0; x < a.size(); ++x) {
        for (std::size_t y = 0; y < b.size(); ++y) {
            if (a[x] != b[y]) continue;
            if (x + y < best_rank) { best_rank = x + y; best = a[x]; }
            break;
        }
    }
    return best;
}

std::size_t Codebook::nearest_index(const lattice::Glyph& q) const {
    if (items_.empty()) return static_cast<std::size_t>(-1);
    const std::uint64_t d = glyph_digest(q);
    const auto hit = exact_.find(d);
    if (hit != exact_.end() && items_[hit->second].second == q) return hit->second;
    const auto m = memo_.find(d);
    if (m != memo_.end()) return m->second;

    std::size_t best = 0;
    double bs = -2.0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const double s = q.similarity(items_[i].second);
        if (s > bs) { bs = s; best = i; }
    }
    if (memo_.size() >= (1u << 18)) memo_.clear();
    memo_.emplace(d, static_cast<std::uint32_t>(best));
    return best;
}

const lattice::Glyph& Codebook::nearest(const lattice::Glyph& q) const {
    if (items_.empty()) return q;
    const std::size_t i = nearest_index(q);
    return items_[i].second;
}

std::string_view Codebook::nearest_name(const lattice::Glyph& q) const {
    if (items_.empty()) return {};
    return items_[nearest_index(q)].first;
}

// ---------------------------------------------------------------------------
// Vm
// ---------------------------------------------------------------------------

const lattice::Glyph& Vm::role(std::uint8_t i) {
    // Fixed for the life of the process and identical in every organism, so a
    // role index carries the same meaning across lineages. Without that,
    // crossover could not transfer a discovered role from one genome to
    // another -- it would be splicing in a symbol with a different referent.
    static const std::array<lattice::Glyph, 256> kRoles = [] {
        std::array<lattice::Glyph, 256> r;
        for (std::size_t k = 0; k < r.size(); ++k) {
            r[k] = lattice::Glyph::random(0x5EEDULL * (k + 1) + 0x9E3779B9ULL);
        }
        return r;
    }();
    return kRoles[i];
}

lattice::Glyph Vm::run(const Genome& g, const lattice::Glyph& input) const {
    // The non-input registers start as distinct fixed constants rather than
    // zero: a zero Glyph is an identity under XOR and absorbing under AND, so a
    // register file of zeros hands the search a cliff of degenerate programs
    // that all compute the same thing.
    //
    // Hoisted to a static because regenerating three 10,000-bit vectors per call
    // was measured at 88% of this function's runtime, alongside re-decoding the
    // tape on every single input.
    static const std::array<lattice::Glyph, kRegisters> kInit = [] {
        std::array<lattice::Glyph, kRegisters> a;
        for (std::size_t i = 1; i < kRegisters; ++i) {
            a[i] = lattice::Glyph::random(0xC0FFEEULL * (i + 1));
        }
        return a;
    }();

    std::array<lattice::Glyph, kRegisters> r = kInit;
    r[0] = input;

    const auto code = g.decode();
    for (const Codon& c : code) {
        switch (c.op) {
            case Op::Nop:                                    break;
            case Op::Copy:   r[c.dst] = r[c.a];               break;
            case Op::Bind:   r[c.dst] = lattice::bind(r[c.a], r[c.b % kRegisters]); break;
            case Op::Bundle: r[c.dst] = lattice::bundle({r[c.a], r[c.b % kRegisters]}); break;
            case Op::Perm:   r[c.dst] = lattice::permute(r[c.a], static_cast<int>(c.b) - 128); break;
            case Op::Role:   r[c.dst] = lattice::bind(r[c.a], role(c.b)); break;
            case Op::And:  { lattice::Glyph t = r[c.a]; t.and_with(r[c.b % kRegisters]); r[c.dst] = t; break; }
            case Op::Or:   { lattice::Glyph t = r[c.a]; t.or_with(r[c.b % kRegisters]); r[c.dst] = t; break; }
            case Op::Clean:  r[c.dst] = cleanup_ ? cleanup_->nearest(r[c.a]) : r[c.a]; break;

            // THE SENSES. Both resolve the register to an item first, which is
            // the only way a hypervector can address anything in the world: a
            // register holds a point in a 10,000-bit space, and cleanup is what
            // turns a point into a referent.
            case Op::Assoc: {
                if (!cleanup_) break;
                const std::size_t i = cleanup_->nearest_index(r[c.a]);
                if (i == static_cast<std::size_t>(-1)) break;
                const auto& l = cleanup_->links(i);
                if (l.empty()) break;
                // A STABLE RANK, not b modulo the item's degree.
                //
                // Degree varies from 0 to 32 across the codebook, so `b % degree`
                // meant a different associate rank for every input word: the same
                // codon had no consistent semantics and therefore nothing to
                // generalise. Worse, only b == 0 denoted "top associate" for all
                // words, probability 1/256 per codon -- expected copies in a
                // 300-organism, 5-codon starting population: 0.027. The single
                // best-performing primitive in the machine was effectively
                // unreachable by mutation.
                r[c.dst] = cleanup_->at(l[std::min<std::size_t>(c.b % 8, l.size() - 1)]);
                break;
            }
            case Op::Neigh: {
                if (!cleanup_) break;
                const std::size_t i = cleanup_->nearest_index(r[c.a]);
                if (i == static_cast<std::size_t>(-1)) break;
                const auto& l = cleanup_->links(i);
                if (l.empty()) break;
                const std::size_t k = std::min<std::size_t>(l.size(), (c.b % 8) + 1);
                std::vector<lattice::Glyph> xs;
                xs.reserve(k);
                for (std::size_t j = 0; j < k; ++j) xs.push_back(cleanup_->at(l[j]));
                r[c.dst] = lattice::bundle(std::span<const lattice::Glyph>(xs));
                break;
            }
            case Op::Common: {
                if (!cleanup_) break;
                const std::size_t i = cleanup_->nearest_index(r[c.a]);
                const std::size_t j = cleanup_->nearest_index(r[c.b % kRegisters]);
                if (i == static_cast<std::size_t>(-1) || j == static_cast<std::size_t>(-1)) break;
                const std::size_t k = cleanup_->common(i, j);
                if (k != static_cast<std::size_t>(-1)) r[c.dst] = cleanup_->at(k);
                break;
            }
            case Op::Kin: {
                if (!cleanup_) break;
                const std::size_t i = cleanup_->nearest_index(r[c.a]);
                if (i == static_cast<std::size_t>(-1)) break;
                const std::size_t k = cleanup_->kin(i);
                if (k != static_cast<std::size_t>(-1)) r[c.dst] = cleanup_->at(k);
                break;
            }
            default: break;
        }
    }
    return r[g.output_register()];
}

// ---------------------------------------------------------------------------
// Chamber
// ---------------------------------------------------------------------------

Chamber::Chamber(ChamberConfig cfg, const Codebook* cleanup, std::uint64_t seed)
    : cfg_(cfg), cleanup_(cleanup), rng_(seed | 1ULL) {
    pop_.reserve(cfg_.population);
    for (std::size_t i = 0; i < cfg_.population; ++i) {
        Organism o;
        o.genome = Genome::random(cfg_.genome_codons, next_rand());
        pop_.push_back(std::move(o));
        ++births_;
    }
}

std::uint64_t Chamber::next_rand() { return splitmix(rng_); }

double Chamber::evaluate(const Genome& g, const std::vector<Assay>& pairs) const {
    return evaluate_(g, pairs, 0, 0);
}

// FITNESS IS BALANCED ACCURACY, AND THE REASON IS A DEGENERATE OPTIMUM.
//
// Per-pair accuracy rewards ignoring the input. On WordNet hypernymy over 150
// categories, "always answer person" scores 5.65% because that category is the
// largest, and any honest operator scores less than that in its first
// generations. Selection climbed the constant hill and stayed: the champion
// produced ONE distinct answer across 1,133 held-out inputs -- the majority-
// class classifier in an eight-instruction costume, and its output register was
// never written from its input at all.
//
// A penalty term would be the wrong fix, because a constant would still be a
// local optimum with a moat around it. Averaging accuracy over TARGET CLASSES
// instead of over pairs removes the optimum entirely: a constant answering
// "person" is right for one class out of every class it is scored against, so
// it scores 1/k rather than the size of the largest class. The deceptive peak
// is not penalised, it is levelled.
//
// This is standard practice for imbalanced classification and it should have
// been the measure from the start.
double Chamber::evaluate_(const Genome& g, const std::vector<Assay>& pairs,
                          std::size_t sample, std::uint64_t seed) const {
    if (pairs.empty()) return 0.0;
    const Vm vm(cleanup_);
    double sim = 0.0;
    const std::size_t n_eval = (sample == 0 || sample >= pairs.size()) ? pairs.size() : sample;
    std::uint64_t s = seed | 1ULL;

    // Grouped by CLASS when the target is set-valued, otherwise by target item.
    // With a set-valued target this also makes the balancing bite: the class is
    // a real category with many members, so a constant answer is right for one
    // class out of every class it faces and scores 1/k. Grouping by target ITEM
    // made balancing a no-op on co-hyponymy — 3,307 of 3,718 target classes were
    // singletons, so the per-class rate was just the per-pair rate wearing a
    // different name, and the long justification for it only ever bit on
    // hypernymy.
    std::unordered_map<int, std::pair<std::uint32_t, std::uint32_t>> per_class;
    for (std::size_t k = 0; k < n_eval; ++k) {
        const Assay& a = (n_eval == pairs.size()) ? pairs[k]
                                                  : pairs[splitmix(s) % pairs.size()];
        const lattice::Glyph out = vm.run(g, a.from);
        sim += out.similarity(a.to);

        bool hit;
        if (cleanup_ && a.to_class >= 0) {
            // SET-VALUED: any member of the class is correct, except echoing the
            // input back, which is never a discovery.
            const std::size_t oi = cleanup_->nearest_index(out);
            hit = (oi != static_cast<std::size_t>(-1)) && oi != a.from_index &&
                  cleanup_->class_of(oi) == a.to_class;
        } else if (cleanup_ && a.to_index != static_cast<std::size_t>(-1)) {
            hit = (cleanup_->nearest_index(out) == a.to_index);
        } else {
            hit = (out == a.to);
        }

        const int key = (a.to_class >= 0) ? a.to_class : static_cast<int>(a.to_index);
        auto& c = per_class[key];
        ++c.second;
        if (hit) ++c.first;
    }

    double acc = 0.0;
    for (const auto& kv : per_class) {
        acc += static_cast<double>(kv.second.first) / static_cast<double>(kv.second.second);
    }
    acc /= static_cast<double>(per_class.size());
    // Similarity stays as a thousandth-weight tiebreak so a population that has
    // not yet got one pair right is not perfectly flat.
    return acc + 0.001 * (sim / static_cast<double>(n_eval));
}

std::size_t Chamber::select_parent() {
    // Tournament selection. Cheap, and the pressure is one parameter rather
    // than a fitness-proportional scheme that has to be rescaled whenever the
    // fitness range moves -- and here it moves a lot, because similarity is
    // signed.
    std::size_t best = next_rand() % pop_.size();
    for (std::size_t k = 1; k < cfg_.tournament; ++k) {
        const std::size_t c = next_rand() % pop_.size();
        if (pop_[c].fitness > pop_[best].fitness) best = c;
    }
    return best;
}

double Chamber::step(const std::vector<Assay>& train) {
    // One sample per ROUND, shared by every organism scored in it -- the whole
    // chamber faces the same environment at the same moment, which is what
    // makes the comparison between them meaningful. It changes between rounds.
    const std::uint64_t env = next_rand();
    for (Organism& o : pop_) {
        o.fitness = evaluate_(o.genome, train, cfg_.sample, env);
        ++o.age;
    }

    // The champion is re-scored on the FULL training set before it is allowed to
    // replace the incumbent, because keeping the luckiest sample rather than the
    // best organism is how a sampled fitness quietly becomes a random search.
    //
    // ONLY the round's best organism, not everyone who beat the incumbent. That
    // earlier test compared a SAMPLED score against the incumbent's FULL-SET
    // score, and measured on the real round-1 population 98 of 300 organisms
    // passed it — each triggering a full 4,194-pair re-evaluation, 411,012 extra
    // VM runs against the 28,800 it took to score the entire population. A 14x
    // front-loaded cost, and it is why runs stopped at 60 generations.
    if (!pop_.empty()) {
        const auto top = std::max_element(pop_.begin(), pop_.end(),
            [](const Organism& x, const Organism& y) { return x.fitness < y.fitness; });
        Organism c = *top;
        c.fitness = evaluate(c.genome, train);
        if (c.fitness > best_full_) { best_full_ = c.fitness; best_ = c; }
    }

    // CONTINUOUS replacement, in the PACE sense: the chamber has a fixed volume,
    // and each round a fraction of it is displaced by offspring of the fit
    // rather than the whole population turning over at a generation boundary.
    // Nothing is culled on age -- an organism leaves because something better
    // took its place.
    const std::size_t replace = std::max<std::size_t>(1, pop_.size() / 4);
    for (std::size_t k = 0; k < replace; ++k) {
        // Displace by inverse tournament: pick a few, evict the worst.
        std::size_t worst = next_rand() % pop_.size();
        for (std::size_t j = 1; j < cfg_.tournament; ++j) {
            const std::size_t c = next_rand() % pop_.size();
            if (pop_[c].fitness < pop_[worst].fitness) worst = c;
        }
        const std::size_t p = select_parent();
        Genome child = (unit(rng_) < cfg_.crossover)
            ? Genome::cross(pop_[p].genome, pop_[select_parent()].genome, next_rand())
            : pop_[p].genome;
        child = child.replicate(next_rand(), cfg_.mutation_rate);
        pop_[worst].genome = std::move(child);
        pop_[worst].fitness = evaluate_(pop_[worst].genome, train, cfg_.sample, env);
        pop_[worst].age = 1;
        ++births_;
    }

    // ELITISM. The best genome found is put back into the breeding population.
    //
    // Without this the champion survives only as a reporting artefact: it is
    // never re-inserted, its lineage sits in the evictable mass whenever a noisy
    // round scores it zero, and 75 evictions per round can breed a discovered
    // operator straight out of existence.
    if (!pop_.empty() && best_full_ > -1e8) {
        auto worst = std::min_element(pop_.begin(), pop_.end(),
            [](const Organism& x, const Organism& y) { return x.fitness < y.fitness; });
        *worst = best_;
    }

    ++generations_;
    return best_.fitness;
}

} // namespace khora::ribosome
