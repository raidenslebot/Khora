#pragma once

// THREE ORGANS THAT WERE BUILT, TESTED, AND UNREACHABLE.
//
// ribosome (637 LOC, two tests), synapse (147 LOC, one test) and governor
// (317 LOC, one bench) all compile into this tree, and khora_main.cpp did not
// name one of them. That is the defect this repo already has on record for Sdr,
// TemporalMemory and techne: a capability the running system cannot invoke is
// not a capability the system has.
//
// FOUR TOOLS, and what each organ actually is:
//
//   evolve    ribosome. A population of byte tapes under a fixed budget,
//             selected on whether the program a tape decodes to carries a
//             subject to its object over one of the Ligature's relations, with
//             Plexus adjacency as the environment its senses reach into. It
//             runs on a BACKGROUND THREAD, because every tool dispatch in
//             khora_main takes the global shared_mutex, so a foreground run of
//             any useful length would stop reverie, curator and volition dead
//             for its whole duration.
//
//   organism  the champion, disassembled with its dead instructions marked,
//             beside its held-out accuracy and the dumb graph baseline. The
//             point of evolving a program rather than fitting weights is that
//             the answer can be read, so this is the tool that reads it.
//
//   pulse     synapse. The evolve thread publishes the champion's answer to one
//             fixed probe word on every generation; this drains the
//             subscription and cleans each hypervector back to a word. The bus
//             carries Glyphs and nothing else, so this is the only shape of
//             traffic it can carry -- see the note in the .cpp about the bus
//             having had no publisher at all before this.
//
//   governor  probe what thermal sensors this machine really exposes, and run
//             or stop the concurrency control loop.
//
// The Plexus is the environment the organism senses and the Ligature supplies
// the relations it is selected against; both are held by reference, so both
// must outlive the Carapace. In khora_main they are declared before the shell,
// which destroys in reverse order, so that holds.

#include "khora/carapace/carapace.hpp"
#include "khora/ligature/ligature.hpp"
#include "khora/plexus/plexus.hpp"

namespace khora::carapace {

// evolve, organism, pulse, governor.
void register_organ_tools(Carapace& c, const plexus::Plexus& px,
                          const ligature::Ligature& lig);

} // namespace khora::carapace
