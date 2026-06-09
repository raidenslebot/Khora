// reforge_eval — the SUCCESSOR-EVALUATOR for Khora's self-rewriting loop.
//
// A running khora.exe holds its own binary open, so it cannot relink itself. The
// honest way around that is a SEPARATE, non-running target: Khora rewrites a
// constant in its own source, rebuilds THIS small harness (which links the changed
// cogitator library), and runs it to measure the candidate's inference yield. The
// winning value is then baked into khora.exe itself on its next build. This is the
// faculty that lets Khora edit, compile and judge its own code by measured result.
//
// It deliberately reproduces the live cogitator's configuration (same lexicon, same
// plexus, same tuned goal-pull) so the number it prints is the number that matters.

#include "khora/cogitator/cogitator.hpp"
#include "khora/cortex/predictive_column.hpp"
#include "khora/lattice/lattice.hpp"
#include "khora/lexicon/lexicon.hpp"
#include "khora/plexus/plexus.hpp"
#include "khora/soma/soma_nexus.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    using namespace khora;

    const std::size_t n = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 200;

    lattice::Lattice         memory;
    cortex::PredictiveColumn column(3);
    soma::SomaNexus          nexus;
    lexicon::Lexicon         lex;
    plexus::Plexus           plex;

    try { lex.load("data/lexicon_archive/main"); }  catch (...) {}
    try { plex.load("data/plexus_archive/main"); }  catch (...) {}

    cogitator::Cogitator mind(lex, memory, column, nexus);
    mind.set_plexus(&plex);

    {   // match the live, self-tuned inference goal-pull
        std::ifstream pf("data/ledger/params.txt");
        double gp = 0.0;
        if (pf >> gp && gp > 0.0 && gp < 20.0) mind.set_infer_goal_pull(gp);
    }

    mind.wandering_seed(0);   // forces the concept field to materialise from the lexicon

    // Combined mind-fitness: the mean of the faculties that have headroom (inference +
    // abstraction). A gene that moves only one faculty is still selected correctly — the
    // others stay flat — so one evaluator serves genes across the whole mind.
    const double yi = mind.benchmark_inference(n, 7);
    const double ya = mind.benchmark_abstraction(n, 7);
    double combined = yi;
    int parts = 1;
    if (ya >= 0.0) { combined += ya; ++parts; }
    combined /= parts;

    std::cout << "YIELD " << combined << "\n";
    return (yi >= 0.0) ? 0 : 2;
}
