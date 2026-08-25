#include "khora/ribosome/ribosome.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>

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
        c.op  = static_cast<Op>(tape_[i] % static_cast<std::uint8_t>(Op::kCount));
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

std::string Genome::disassemble() const {
    std::string out;
    char line[96];
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
                              i, c.dst, c.b, c.a);
                break;
            case Op::Neigh:
                std::snprintf(line, sizeof line, "%2zu  neigh  r%u <- bundle top %u of r%u\n",
                              i, c.dst, static_cast<unsigned>((c.b % 8) + 1), c.a);
                break;
            case Op::Copy: case Op::Clean:
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
        out += line;
        ++i;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Codebook
// ---------------------------------------------------------------------------

void Codebook::add(std::string name, const lattice::Glyph& g) {
    items_.emplace_back(std::move(name), g);
}

void Codebook::link(std::size_t from, std::size_t to) {
    if (adj_.size() < items_.size()) adj_.resize(items_.size());
    if (from < adj_.size()) adj_[from].push_back(static_cast<std::uint32_t>(to));
}

const std::vector<std::uint32_t>& Codebook::links(std::size_t i) const {
    static const std::vector<std::uint32_t> kNone;
    return (i < adj_.size()) ? adj_[i] : kNone;
}

std::size_t Codebook::nearest_index(const lattice::Glyph& q) const {
    if (items_.empty()) return static_cast<std::size_t>(-1);
    std::size_t best = 0;
    double bs = -2.0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const double s = q.similarity(items_[i].second);
        if (s > bs) { bs = s; best = i; }
    }
    return best;
}

const lattice::Glyph& Codebook::nearest(const lattice::Glyph& q) const {
    if (items_.empty()) return q;
    std::size_t best = 0;
    double bs = -2.0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const double s = q.similarity(items_[i].second);
        if (s > bs) { bs = s; best = i; }
    }
    return items_[best].second;
}

std::string_view Codebook::nearest_name(const lattice::Glyph& q) const {
    if (items_.empty()) return {};
    std::size_t best = 0;
    double bs = -2.0;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const double s = q.similarity(items_[i].second);
        if (s > bs) { bs = s; best = i; }
    }
    return items_[best].first;
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
    std::array<lattice::Glyph, kRegisters> r;
    r[0] = input;
    // The other registers start as distinct fixed constants rather than zero.
    // A zero Glyph is an identity under XOR and an absorbing element under AND,
    // so a register file of zeros hands the search a cliff of degenerate
    // programs that all compute the same thing.
    for (std::size_t i = 1; i < kRegisters; ++i) {
        r[i] = lattice::Glyph::random(0xC0FFEEULL * (i + 1));
    }

    for (const Codon& c : g.decode()) {
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
                r[c.dst] = cleanup_->at(l[c.b % l.size()]);
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
            default: break;
        }
    }
    return r[0];
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
    if (pairs.empty()) return 0.0;
    const Vm vm(cleanup_);
    std::size_t right = 0;
    double sim = 0.0;
    for (const Assay& a : pairs) {
        const lattice::Glyph out = vm.run(g, a.from);
        sim += out.similarity(a.to);
        if (cleanup_ && a.to_index != static_cast<std::size_t>(-1)) {
            if (cleanup_->nearest_index(out) == a.to_index) ++right;
        } else if (out == a.to) {
            ++right;
        }
    }
    const double n = static_cast<double>(pairs.size());
    // Accuracy dominates; similarity is a thousandth-weight tiebreak so a
    // population that has not yet got one pair right still has SOME gradient.
    return static_cast<double>(right) / n + 0.001 * (sim / n);
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
    for (Organism& o : pop_) {
        if (o.fitness < -1.0 + 1e-12 || o.age == 0) o.fitness = evaluate(o.genome, train);
        ++o.age;
    }

    for (const Organism& o : pop_) {
        if (o.fitness > best_.fitness) best_ = o;
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
        pop_[worst].fitness = evaluate(pop_[worst].genome, train);
        pop_[worst].age = 1;
        ++births_;
    }

    ++generations_;
    return best_.fitness;
}

} // namespace khora::ribosome
