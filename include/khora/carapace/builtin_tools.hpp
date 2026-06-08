#pragma once

// Built-in tools for the Carapace.
//
// Each register_* function adds a coherent group of tools that target
// a specific subsystem. The tools capture references to their target
// objects via lambdas, so the caller must keep them alive for the life
// of the Carapace.

#include "khora/carapace/carapace.hpp"
#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/soma/soma_nexus.hpp"

namespace khora::carapace {

// Help, echo, now, pwd, ls, cat, stat — pure builtins, no subsystem refs.
void register_core_tools(Carapace& c);

// memorize, recall, query — operate on a Lattice. If a Lexicon is
// supplied, tokens are encoded through it so similar / typo'd words
// resolve to similar glyphs. If nullptr, falls back to Glyph::from_hash.
void register_memory_tools(Carapace& c,
                           khora::lattice::Lattice& memory,
                           khora::lexicon::Lexicon* lex = nullptr);

// learn, predict, cortex_stats, train — operate on a PredictiveColumn.
// If a Lexicon is supplied, training text exposes it for cooccurrence
// drift in parallel with feeding the cortex.
void register_cortex_tools(Carapace& c,
                           khora::cortex::PredictiveColumn& cortex,
                           khora::lexicon::Lexicon* lex = nullptr);

// mood, stimulate, drive_set — operate on a SomaNexus.
void register_soma_tools(Carapace& c, khora::soma::SomaNexus& soma);

// lex_stats, lex_sim, lex_expose — operate on a Lexicon.
void register_lexicon_tools(Carapace& c, khora::lexicon::Lexicon& lex);

} // namespace khora::carapace
