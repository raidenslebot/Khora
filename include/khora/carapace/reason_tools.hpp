#pragma once

// Rules with variables, reachable from the running system, over the relations
// Khora actually read.
//
// khora::logos exists because Ligature::deduce has exactly two inference rules
// and both are written into the C++:
//
//     subject is-a A, A has Z        =>  subject has Z
//     subject causes Y, Y causes Z   =>  subject causes Z
//
// Those two are useful and they are the only two the system will ever have. A
// resolver that fixes that has been built, tested with thirty checks, and never
// once run by the live binary -- khora_main.cpp did not mention logos, and the
// engine had no way to see the fourteen thousand relations sitting next to it
// either, because the Ligature could answer "what does X cause" and not "what is
// in here".
//
// These four tools close both halves. The engine is SEEDED from the Ligature,
// the two hardcoded patterns are installed as ordinary Horn clauses rather than
// as C++, and a user or an agent can add a third.

#include "khora/carapace/carapace.hpp"
#include "khora/ligature/ligature.hpp"

namespace khora::carapace {

// know, rule, ask, why. The engine is seeded lazily from `lig` on first use and
// holds a reference to it, so it must outlive the Carapace.
void register_reason_tools(Carapace& c, const ligature::Ligature& lig);

} // namespace khora::carapace
