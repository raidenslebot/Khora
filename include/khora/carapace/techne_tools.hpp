#pragma once

// Program synthesis, reachable from the running system.
//
// Khora exposes ninety-four tools and none of them could write a program, while
// a synthesiser with fourteen language backends sat in the same repository
// unreferenced by khora_main.cpp. These three close that.

#include "khora/carapace/carapace.hpp"

namespace khora::carapace {

// synth, synth_library, synth_forget. No subsystem references: the learned
// library is process-wide and persisted under data/, so what Khora works out
// about programming in one session is there in the next.
void register_techne_tools(Carapace& c);

} // namespace khora::carapace
