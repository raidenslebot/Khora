// THE RUNNING SYSTEM CAN NOW REACH THE ORGANS THAT WERE DARK.
//
// Ribosome, Synapse and Governor were compiled, linked into tests and benches,
// and never mentioned by khora_main.cpp. This file is the wiring, and the
// choices in it are constrained by two facts about the live binary that a
// standalone bench never has to face.
//
// FIRST: every operator command runs under a global lock. khora_main dispatches
// through `locked_dispatch`, which takes `shared_mu` in unique mode, so a tool
// that blocks for a minute blocks reverie, the curator and volition for that
// minute too. A ribosome run is minutes. So `evolve` starts a worker thread and
// returns, and the status tools poll it.
//
// SECOND: Codebook::nearest_index memoises cleanup in a `mutable` unordered_map
// with no lock. Two threads cleaning up at once is a data race on a hash map,
// not merely a stale answer. Everything here that touches the codebook -- the
// worker's whole generation, `organism`, and `pulse`'s cleanup of a payload --
// takes one mutex, so a status command can wait up to one generation for its
// answer. That is the honest cost of a shared cleanup memo.
//
// WHAT THE BUS IS ACTUALLY CARRYING, stated plainly because the alternative
// would have been ceremony. SynapseBus had zero publishers in this process --
// no subsystem produced a Pulse, and a shell tool that publishes and then reads
// its own message back is a demo, not a wiring. The evolve worker is a real
// producer: it runs on its own thread, it emits one pulse per generation, and
// the payload type the bus carries (a Glyph, and only a Glyph) is exactly what
// it has to send -- the champion's raw output hypervector for a fixed probe
// word. The receiver cleans it up to a name. That is the VSA-native use of a
// glyph-typed bus, and it makes the bounded queue and the drop counter mean
// something: leave a long run unattended and `pulse` shows what was dropped.
//
// WHAT THE GOVERNOR CANNOT DO HERE, so nobody reads more into the tool than is
// there. Its control law regulates a POOL: `park_if_over(slot)` parks workers
// whose slot index is past the allowance, and `min_workers` is 1. The evolve
// worker is a single thread, so it is never over the allowance and the governor
// can never gate it. Wiring `park_if_over` into the generation loop would be a
// call that provably never parks. The governor's real consumer is a parallel
// chamber, which does not exist yet; until then the honest exposure is the
// sensor probe and the loop's own statistics.

#include "khora/carapace/organ_tools.hpp"

#include "khora/governor/governor.hpp"
#include "khora/ribosome/ribosome.hpp"
#include "khora/synapse/synapse_bus.hpp"

#include <atomic>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace khora::carapace {
namespace {

// Local, matching builtin_tools.cpp, techne_tools.cpp and reason_tools.cpp.
ToolResult make_ok(std::string output) { return {true, std::move(output), ""}; }
ToolResult make_err(std::string error) { return {false, "", std::move(error)}; }

constexpr const char* kTopic = "ribosome/champion";
constexpr std::size_t kNone  = static_cast<std::size_t>(-1);

bool relation_from(const std::string& n, ligature::Relation& out) {
    using R = ligature::Relation;
    if (n == "is-a" || n == "isa")     { out = R::IsA;     return true; }
    if (n == "causes" || n == "cause") { out = R::Causes;  return true; }
    if (n == "has" || n == "has-part") { out = R::HasPart; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Process-wide organ state. One of each, held by shared_ptr in the handlers.
// ---------------------------------------------------------------------------
struct Organs {
    // Guards the codebook, the chamber, and therefore every cleanup call.
    std::mutex mu;

    ribosome::Codebook           cb;
    std::vector<ribosome::Assay> train, held;
    std::unique_ptr<ribosome::Chamber> chamber;
    lattice::Glyph               probe;
    std::string                  probe_word;
    std::string                  relation;
    std::string                  setup;       // the setup report, reprinted by `organism`

    std::atomic<std::size_t> done{0};
    std::size_t              target = 0;

    synapse::SynapseBus bus;
    synapse::Handle     sub = 0;

    governor::Governor gov;
    std::atomic<bool>  gov_on{false};

    std::thread       worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};

    Organs() { sub = bus.subscribe(kTopic); }
    ~Organs() { halt(); gov.stop(); }

    // The worker holds a raw pointer, not a shared_ptr: a shared_ptr would keep
    // this object alive and the destructor that joins the thread would never
    // run, which is a thread outliving the shell rather than a lifetime fix.
    void halt() {
        stop.store(true);
        if (worker.joinable()) worker.join();
        stop.store(false);
        running.store(false);
    }
};

void work(Organs* o) {
    const ribosome::Vm vm(&o->cb);
    while (!o->stop.load() && o->done.load() < o->target) {
        std::lock_guard<std::mutex> lk(o->mu);
        o->chamber->step(o->train);
        o->done.fetch_add(1);
        // One pulse per generation: what the champion currently answers for the
        // probe word, as the raw hypervector. Cleanup happens at the receiver.
        o->bus.publish(kTopic, vm.run(o->chamber->best().genome, o->probe));
    }
    o->running.store(false);
}

// ---------------------------------------------------------------------------
// Build the environment and the assays. Runs on the shell thread, which is the
// only thread allowed to touch Plexus and the Ligature -- khora_main holds the
// global lock for the length of a dispatch, and nothing else here reads them.
// ---------------------------------------------------------------------------
std::string build(Organs& o, const plexus::Plexus& px, const ligature::Ligature& lig,
                  ligature::Relation rel, std::uint32_t floor, std::size_t generations,
                  std::string& err) {
    o.cb = ribosome::Codebook{};
    o.train.clear();
    o.held.clear();
    o.chamber.reset();
    o.done.store(0);

    std::unordered_map<std::string, std::size_t> slot;
    auto intern = [&](const std::string& w) -> std::size_t {
        const auto it = slot.find(w);
        if (it != slot.end()) return it->second;
        const std::size_t i = o.cb.size();
        // HASHED glyphs, every word orthogonal to every other. The bench runs a
        // distributional arm off the Lexicon as well; that is a second knob and
        // a second subsystem reference, and the graph opcodes are what carry the
        // signal either way.
        o.cb.add(w, lattice::Glyph::from_hash(w));
        slot.emplace(w, i);
        return i;
    };

    // A relation can only be searched for between words the environment knows:
    // an organism reaches the world through Plexus adjacency, so a word absent
    // from Plexus has nothing to sense and cannot be an answer either.
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::size_t skipped = 0;
    for (const auto& t : lig.all(floor)) {
        if (t.rel != rel) continue;
        if (t.subject == t.object) continue;
        if (!px.has(t.subject) || !px.has(t.object)) { ++skipped; continue; }
        pairs.emplace_back(intern(t.subject), intern(t.object));
    }
    if (o.cb.size() < 8 || pairs.size() < 8) {
        err = "only " + std::to_string(pairs.size()) + " usable pairs for " +
              ligature::relation_name(rel) + " at support >= " + std::to_string(floor) +
              " (" + std::to_string(skipped) + " more had an end the Plexus has never "
              "seen) -- lower the floor or study more";
        return {};
    }

    // The environment graph: Plexus associates, restricted to the codebook.
    // Searched 400 deep and capped at 32 kept, which is what the bench measured
    // -- cutting the associate list before intersecting it with the codebook
    // leaves most words with no edges at all, and then the senses are not under
    // test.
    std::size_t edges = 0;
    for (std::size_t i = 0; i < o.cb.size(); ++i) {
        std::size_t kept = 0;
        // BROAD, and asking for 400 was never going to be enough on its own.
        // associates() applies three filters before k is even consulted -- pairs
        // seen fewer than three times, neighbours above 0.6% of tokens, and
        // non-positive PMI -- which cut the mean list from 42 words to 8.5
        // whatever k says. The comment above wanted coverage and the readout was
        // quietly refusing it; retrieve_bench measured that list at recall@100 of
        // 1.56%, below random ranking. broad() is the setting that gives three
        // times the recall, which is what an environment graph wants.
        for (const auto& a : px.associates(std::string(o.cb.name_at(i)), 400,
                                           khora::plexus::Plexus::Readout::broad())) {
            if (kept >= 32) break;
            const auto it = slot.find(a.first);
            if (it == slot.end()) continue;
            o.cb.link(i, it->second);
            ++kept; ++edges;
        }
    }
    o.cb.precompute_kin();
    std::size_t with_edges = 0;
    for (std::size_t i = 0; i < o.cb.size(); ++i) {
        if (!o.cb.links(i).empty()) ++with_edges;
    }

    // Held out one pair in five. The split is by PAIR rather than by subject,
    // which is weaker than the bench's split by member: a subject with two
    // objects can have one of each side. Stated rather than hidden.
    for (std::size_t k = 0; k < pairs.size(); ++k) {
        ribosome::Assay a;
        a.from       = o.cb.at(pairs[k].first);
        a.to         = o.cb.at(pairs[k].second);
        a.to_index   = pairs[k].second;
        a.from_index = pairs[k].first;
        ((k % 5 == 0) ? o.held : o.train).push_back(a);
    }
    if (o.train.empty() || o.held.empty()) {
        err = "not enough pairs to split into train and held-out";
        return {};
    }

    // THE DUMB GRAPH BASELINE: answer with the strongest Plexus associate that
    // is in the codebook. Thirty lines, no search, no evolution. An evolved
    // operator that does not beat this has discovered nothing.
    std::size_t top_hits = 0;
    for (const auto& a : o.held) {
        // The same readout the environment was built with, or the baseline is
        // answering a different question from the organism it is judging.
        for (const auto& n : px.associates(std::string(o.cb.name_at(a.from_index)), 400,
                                           khora::plexus::Plexus::Readout::broad())) {
            const auto it = slot.find(n.first);
            if (it == slot.end() || it->second == a.from_index) continue;
            if (it->second == a.to_index) ++top_hits;
            break;
        }
    }

    // Same settings the bench measured with: 5 codons because the output is the
    // last register written and short tapes were what made that pay, 300
    // organisms, 3% per-byte replication error.
    ribosome::ChamberConfig cfg;
    cfg.population    = 300;
    cfg.genome_codons = 5;
    cfg.mutation_rate = 0.03;
    o.chamber   = std::make_unique<ribosome::Chamber>(cfg, &o.cb, 20260825ULL);
    o.relation  = ligature::relation_name(rel);
    o.target    = generations;
    o.probe     = o.held.front().from;
    o.probe_word = std::string(o.cb.name_at(o.held.front().from_index));

    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "  relation    : " << o.relation << ", support >= " << floor << "\n";
    os << "  codebook    : " << o.cb.size() << " words, " << edges
       << " environment edges from Plexus, " << with_edges << " words with at least one\n";
    os << "  pairs       : " << o.train.size() << " train / " << o.held.size()
       << " held out (" << skipped << " dropped, an end the Plexus never saw)\n";
    os << "  chance      : " << (100.0 / static_cast<double>(o.cb.size())) << "%  (1 / "
       << o.cb.size() << ")\n";
    os << "  identity    : 0.00%, by construction -- subject == object is excluded\n";
    os << "  top associate: " << (100.0 * static_cast<double>(top_hits) /
                                  static_cast<double>(o.held.size()))
       << "%  (" << top_hits << "/" << o.held.size() << ") -- the baseline to beat\n";
    return os.str();
}

std::string reading_report(const governor::Reading& r) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << "  die sensor  : " << (r.die_temp_available ? "present" : "NOT AVAILABLE")
       << " -- no ring-0 thermal driver on this machine, so the 85 C rule is\n"
          "                not being enforced against the die\n";
    os << "  temperature : ";
    if (r.celsius < 0.0) os << "none readable\n";
    else                 os << r.celsius << " C from " << r.source << "\n";
    os << "  firmware    : " << (r.firmware_throttling ? "throttle bit SET" : "not throttling")
       << "\n";
    os << "  frequency   : " << r.performance_pct << "% of nominal\n";
    os << "  machine cpu : ";
    if (r.machine_cpu_pct < 0.0) os << "not sampled (probe does not read % Processor Time)\n";
    else                         os << r.machine_cpu_pct << "%\n";
    os << "  allowed     : " << r.allowed_workers << " workers\n";
    return os.str();
}

} // namespace

void register_organ_tools(Carapace& c, const plexus::Plexus& px,
                          const ligature::Ligature& lig) {
    auto o = std::make_shared<Organs>();

    c.register_tool({
        "evolve",
        "evolve a program that computes one of the extracted relations, on a "
        "background thread (usage: evolve <is-a|causes|has> [generations] "
        "[min-support] | evolve stop | evolve)",
        [o, &px, &lig](const Intent& i) -> ToolResult {
            if (i.args.empty()) {
                if (!o->chamber) return make_ok("  nothing has been evolved yet -- try "
                                                "`evolve is-a 40`");
                std::ostringstream os;
                os << "  " << o->relation << ": generation " << o->done.load() << " of "
                   << o->target << (o->running.load() ? " (running)" : " (stopped)")
                   << "\n  `organism` for the champion, `pulse` for what it is answering";
                return make_ok(os.str());
            }
            if (i.args[0] == "stop") {
                if (!o->running.load()) return make_ok("  nothing is running");
                o->halt();
                return make_ok("  stopped at generation " + std::to_string(o->done.load()));
            }

            ligature::Relation rel{};
            if (!relation_from(i.args[0], rel)) {
                return make_err("relation must be is-a, causes or has");
            }
            std::size_t gens = 40;
            std::uint32_t floor = 2;
            try {
                if (i.args.size() > 1) gens  = std::stoul(i.args[1]);
                if (i.args.size() > 2) floor = static_cast<std::uint32_t>(std::stoul(i.args[2]));
            } catch (...) {
                return make_err("generations and min-support must be numbers");
            }
            if (gens == 0) return make_err("zero generations evolves nothing");

            o->halt();   // a second run replaces the first rather than racing it
            std::string err;
            std::string report;
            {
                std::lock_guard<std::mutex> lk(o->mu);
                report = build(*o, px, lig, rel, floor, gens, err);
            }
            if (!err.empty()) return make_err(err);
            o->setup = report;

            o->running.store(true);
            o->worker = std::thread(work, o.get());

            std::ostringstream os;
            os << report;
            os << "  probe       : '" << o->probe_word
               << "' -- its answer is published on every generation, read it with `pulse`\n";
            os << "  running " << gens << " generations on a background thread; "
                  "`evolve` for progress, `evolve stop` to halt.";
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "organism",
        "the evolved champion, disassembled with dead instructions marked, and "
        "its held-out accuracy",
        [o](const Intent&) -> ToolResult {
            std::lock_guard<std::mutex> lk(o->mu);
            if (!o->chamber) return make_err("nothing has been evolved yet -- try "
                                             "`evolve is-a 40`");
            if (o->done.load() == 0) return make_ok("  no generation has completed yet");

            const auto& best = o->chamber->best();
            const ribosome::Vm vm(&o->cb);
            std::size_t hits = 0;
            for (const auto& a : o->held) {
                if (o->cb.nearest_index(vm.run(best.genome, a.from)) == a.to_index) ++hits;
            }

            std::ostringstream os;
            os << std::fixed << std::setprecision(2);
            os << o->setup;
            os << "  generation  : " << o->done.load() << " of " << o->target
               << (o->running.load() ? " (running)" : " (stopped)") << ", "
               << o->chamber->births() << " births\n";
            os << "  fitness     : " << best.fitness
               << " balanced accuracy on the training pairs\n";
            os << "  held out    : " << (100.0 * static_cast<double>(hits) /
                                         static_cast<double>(o->held.size()))
               << "%  (" << hits << "/" << o->held.size()
               << ") -- compare against top associate above\n";
            os << "  " << best.genome.codons() << " codons, "
               << best.genome.effective_length() << " of them live\n";
            os << best.genome.disassemble();
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "pulse",
        "drain the synapse bus: what the evolving champion answered for the "
        "probe word, one pulse per generation",
        [o](const Intent&) -> ToolResult {
            std::lock_guard<std::mutex> lk(o->mu);
            std::ostringstream os;
            std::size_t shown = 0, more = 0;
            // The last twenty are the interesting ones, but the queue is FIFO
            // and drops from the front, so drain it all and print the tail.
            std::vector<std::string> lines;
            while (auto p = o->bus.try_pop(o->sub)) {
                std::string name = "(no codebook)";
                if (o->cb.size() > 0) {
                    const std::size_t k = o->cb.nearest_index(p->payload);
                    if (k != kNone) name = std::string(o->cb.name_at(k));
                }
                lines.push_back("    seq " + std::to_string(p->sequence) + "  -> " + name);
            }
            if (lines.size() > 20) { more = lines.size() - 20; }
            for (std::size_t k = more; k < lines.size(); ++k) { os << lines[k] << "\n"; ++shown; }
            if (more) os << "    (" << more << " earlier pulses read and not shown)\n";
            if (shown == 0) os << "  nothing queued on " << kTopic
                               << " -- start a run with `evolve is-a 40`\n";
            os << "  bus: " << o->bus.total_published() << " published, "
               << o->bus.total_dropped() << " dropped overall, "
               << o->bus.dropped_for(o->sub) << " dropped for this subscription, "
               << o->bus.subscriber_count(kTopic) << " subscriber(s) on " << kTopic;
            return make_ok(os.str());
        }
    });

    c.register_tool({
        "governor",
        "what thermal and load sensors this machine really exposes, and the "
        "concurrency control loop (usage: governor [start|stop])",
        [o](const Intent& i) -> ToolResult {
            if (!i.args.empty() && i.args[0] == "start") {
                o->gov.start();
                o->gov_on.store(true);
                return make_ok("  control loop sampling every 500 ms; ceiling "
                               + std::to_string(governor::Governor::cap_workers(0.90))
                               + " workers, starting at half of it (slow start).\n"
                                 "  nothing in this process consumes the allowance yet -- "
                                 "see the note in organ_tools.cpp.");
            }
            if (!i.args.empty() && i.args[0] == "stop") {
                if (!o->gov_on.load()) return make_ok("  the control loop is not running");
                std::ostringstream os;
                os << std::fixed << std::setprecision(1);
                // peak_celsius() is -1 until the first sample lands, and a run
                // reported as "peaked at -1.0 C" is exactly the kind of value
                // that says one thing and means another.
                if (o->gov.peak_celsius() < 0.0) os << "  no temperature was sampled";
                else os << "  peak " << o->gov.peak_celsius() << " C";
                os << ", allowance fell to "
                   << o->gov.min_allowed() << " workers, " << o->gov.throttle_events()
                   << " backoffs\n";
                o->gov.stop();
                o->gov_on.store(false);
                os << "  stopped.";
                return make_ok(os.str());
            }
            if (!i.args.empty()) return make_err("usage: governor [start|stop]");

            std::ostringstream os;
            os << std::fixed << std::setprecision(1);
            os << "  one-shot probe:\n" << reading_report(governor::probe());
            if (o->gov_on.load()) {
                os << "  control loop running: peak ";
                if (o->gov.peak_celsius() < 0.0) os << "(no sample yet)";
                else                             os << o->gov.peak_celsius() << " C";
                os << ", allowance now " << o->gov.allowed() << " of "
                   << governor::Governor::cap_workers(0.90) << ", "
                   << o->gov.throttle_events() << " backoffs\n";
            } else {
                os << "  control loop is not running (`governor start`)\n";
            }
            return make_ok(os.str());
        }
    });
}

} // namespace khora::carapace
