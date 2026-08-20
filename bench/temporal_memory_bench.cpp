// Does the temporal memory earn its place?
//
// Khora already does sequences, in the dense VSA: a chain is a bundle of
// transition bindings and traversal is unbind-plus-cleanup. That encoding is
// exact, fast, and measured -- 100% recall to chains of ~100, 99.6% at 200,
// falling away past 300 as superposition crosstalk builds.
//
// So the sparse temporal memory has to buy something that cannot be had by
// making the dense chain longer. Two things are measured here.
//
//   1. CAPACITY. How many sequences can be held at once, and how long, before
//      recall breaks. Head to head, same task, same symbols.
//
//   2. HIGH-ORDER CONTEXT. Whether the two encodings can hold sequences that
//      SHARE a subsequence and still keep their futures apart. The dense chain
//      structurally cannot: a transition is a pair, so B->C is one edge no
//      matter which sequence it belongs to, and after A B C the chain offers
//      every successor C ever had.
//
// Throughput is reported too, because a capability that costs 1000x is a
// different proposition from one that costs 2x.

#include "khora/cortex/temporal_memory.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lattice/sdr.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using clock_t_ = std::chrono::high_resolution_clock;
using namespace khora::lattice;
using khora::cortex::TemporalMemory;

namespace {

std::uint64_t rs = 0xC0FFEE123ULL;
std::uint64_t rnd() {
    std::uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// --- the dense VSA chain, as whetstone encodes it ---------------------------
struct DenseChain {
    Lattice codebook;
    Glyph   chain;

    void build(const std::vector<std::vector<int>>& seqs, int n_symbols) {
        std::vector<Glyph> items;
        for (int i = 0; i < n_symbols; ++i) {
            const Glyph g = Glyph::from_hash("sym" + std::to_string(i));
            items.push_back(g);
            codebook.store("s" + std::to_string(i), g);
        }
        std::vector<Glyph> tr;
        for (const auto& s : seqs) {
            for (std::size_t i = 0; i + 1 < s.size(); ++i) {
                tr.push_back(bind(permute(items[s[i]], 1), items[s[i + 1]]));
            }
        }
        chain = bundle(std::span<const Glyph>{tr.data(), tr.size()});
    }
    int follow(int from) const {
        const Glyph g = Glyph::from_hash("sym" + std::to_string(from));
        const auto m = codebook.query(bind(chain, permute(g, 1)), 1);
        if (m.empty()) return -1;
        return std::stoi(m.front().label.substr(1));
    }
};

Sdr sym_sdr(int i) { return Sdr::from_hash("sym" + std::to_string(i)); }

} // namespace

int main() {
    std::printf("Temporal memory vs dense VSA chain\n\n");

    // --- 1. CAPACITY: many independent sequences --------------------------
    std::printf("CAPACITY -- S sequences of length %d, exact next-step recall\n", 8);
    std::printf("    S  | dense chain | temporal memory\n");
    std::printf("  -----+-------------+----------------\n");
    for (const int S : {2, 4, 8, 16, 32, 64}) {
        const int L = 8;
        std::vector<std::vector<int>> seqs;
        int next_id = 0;
        for (int s = 0; s < S; ++s) {
            std::vector<int> seq;
            for (int i = 0; i < L; ++i) seq.push_back(next_id++);
            seqs.push_back(seq);
        }

        DenseChain dc;
        dc.build(seqs, next_id);
        int dense_ok = 0, total = 0;
        for (const auto& s : seqs) {
            for (std::size_t i = 0; i + 1 < s.size(); ++i) {
                ++total;
                if (dc.follow(s[i]) == s[i + 1]) ++dense_ok;
            }
        }

        TemporalMemory tm;
        for (int e = 0; e < 40; ++e) {
            for (const auto& s : seqs) {
                tm.reset();
                for (const int id : s) tm.compute(sym_sdr(id), true);
            }
        }
        int tm_ok = 0, tm_total = 0;
        for (const auto& s : seqs) {
            tm.reset();
            for (std::size_t i = 0; i + 1 < s.size(); ++i) {
                tm.compute(sym_sdr(s[i]), false);
                if (i == 0) continue;              // the first step cannot predict
                ++tm_total;
                const Sdr want = sym_sdr(s[i + 1]);
                std::size_t hit = 0;
                for (std::size_t b = 0; b < kSdrBlocks; ++b)
                    if (tm.predicted_columns().contains(b, want.index(b))) ++hit;
                if (hit > kSdrBlocks * 3 / 4) ++tm_ok;
            }
        }
        std::printf("   %3d  |   %6.1f%%    |   %6.1f%%\n", S,
                    100.0 * dense_ok / total, 100.0 * tm_ok / tm_total);
    }

    // --- 2. HIGH-ORDER CONTEXT -------------------------------------------
    //
    // N sequences that all share the middle subsequence. Only a representation
    // that distinguishes "this symbol in THIS context" can keep the endings
    // apart.
    std::printf("\nHIGH-ORDER CONTEXT -- N sequences sharing a common middle\n");
    std::printf("    N  | dense chain | temporal memory\n");
    std::printf("  -----+-------------+----------------\n");
    for (const int N : {2, 3, 4, 6}) {
        // prefix_i, M0, M1, M2, ending_i
        const int MID0 = 900, MID1 = 901, MID2 = 902;
        std::vector<std::vector<int>> seqs;
        for (int i = 0; i < N; ++i) {
            seqs.push_back({i, MID0, MID1, MID2, 500 + i});
        }
        DenseChain dc;
        dc.build(seqs, 1000);
        int dense_ok = 0;
        for (int i = 0; i < N; ++i) {
            if (dc.follow(MID2) == 500 + i) ++dense_ok;
        }

        TemporalMemory tm;
        for (int e = 0; e < 80; ++e) {
            for (const auto& s : seqs) {
                tm.reset();
                for (const int id : s) tm.compute(sym_sdr(id), true);
            }
        }
        int tm_ok = 0;
        for (int i = 0; i < N; ++i) {
            tm.reset();
            for (int k = 0; k < 4; ++k) tm.compute(sym_sdr(seqs[i][k]), false);
            const Sdr want = sym_sdr(500 + i);
            std::size_t hit = 0, wrong = 0;
            for (std::size_t b = 0; b < kSdrBlocks; ++b)
                if (tm.predicted_columns().contains(b, want.index(b))) ++hit;
            for (int j = 0; j < N; ++j) {
                if (j == i) continue;
                const Sdr other = sym_sdr(500 + j);
                std::size_t h = 0;
                for (std::size_t b = 0; b < kSdrBlocks; ++b)
                    if (tm.predicted_columns().contains(b, other.index(b))) ++h;
                if (h > kSdrBlocks * 3 / 4) ++wrong;
            }
            if (hit > kSdrBlocks * 3 / 4 && wrong == 0) ++tm_ok;
        }
        std::printf("   %3d  |   %6.1f%%    |   %6.1f%%\n", N,
                    100.0 * dense_ok / N, 100.0 * tm_ok / N);
    }

    // --- 3. THROUGHPUT ----------------------------------------------------
    {
        TemporalMemory tm;
        std::vector<Sdr> stream;
        for (int i = 0; i < 64; ++i) stream.push_back(sym_sdr(i));
        const int n = 4000;
        const auto t0 = clock_t_::now();
        for (int i = 0; i < n; ++i) tm.compute(stream[i % stream.size()], true);
        const double ms =
            std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();
        std::printf("\nTHROUGHPUT: %.0f steps/s learning (%.3f ms/step), %zu segments\n",
                    n / ms * 1000.0, ms / n, tm.segment_count());
    }
    return 0;
}
