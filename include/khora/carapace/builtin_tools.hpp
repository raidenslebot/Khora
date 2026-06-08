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
#include "khora/soma/soma_nexus.hpp"

namespace khora::carapace {

// Help, echo, now, pwd, ls, cat, stat — pure builtins, no subsystem refs.
void register_core_tools(Carapace& c);

// memorize, recall, query — operate on a Lattice.
void register_memory_tools(Carapace& c, khora::lattice::Lattice& memory);

// learn, predict, cortex_stats — operate on a PredictiveColumn.
void register_cortex_tools(Carapace& c, khora::cortex::PredictiveColumn& cortex);

// mood, stimulate, drive_set — operate on a SomaNexus.
void register_soma_tools(Carapace& c, khora::soma::SomaNexus& soma);

} // namespace khora::carapace
