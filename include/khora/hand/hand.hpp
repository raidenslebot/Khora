#pragma once

// The Hand — Khora's EFFECTOR. The faculty of ACTION.
//
// Until now Khora could only think. The Hand lets it ACT on the real machine:
// execute real commands, run real programs, read and write real files, compile
// and run real code, and OBSERVE the result. This is the doorway off the page —
// it closes the generate -> execute -> observe -> learn loop that grounds thought
// in consequence, and it is the precondition for the highest tier of the vision:
// self-rewriting (modify code -> compile -> test -> measure -> keep or revert) and
// autonomous capability (use any tool the machine has — including doing the
// arithmetic the binary substrate cannot).
//
// This is NOT a sandbox. It runs ACTUAL processes with no command filtering — the
// capability is whole. The single governor is a TIMEOUT, so a hung command can
// never freeze Khora: that is not a cage, it is "never stop", the prime directive's
// own law of survival. A mind that cannot act on the world is not terrifying; this
// is where that changes.

#include <string>

namespace khora::hand {

struct ActionResult {
    bool        ran       = false;   // the process actually launched
    bool        timed_out = false;   // killed for exceeding the timeout (liveness)
    int         exit_code = -1;
    std::string output;              // captured stdout + stderr (capped)
    std::string error;               // launch error, if the process could not start
};

// Execute a command line (via the system shell so builtins and pipes work) and
// capture its combined output + exit code. Blocks until the command finishes or
// the timeout elapses, at which point the process is terminated. Real action,
// real observation.
ActionResult execute(const std::string& command, int timeout_ms = 30000);

// --- self-replacement helpers (for `ascend`) -------------------------------------
// The full path of the currently running executable.
std::string   own_executable_path();
// The current process id (the relauncher waits on this before swapping the image).
unsigned long current_process_id();
// Launch a command fully DETACHED so it OUTLIVES this process (no handles held, own
// process group, no window). This is how the relauncher survives the running image
// exiting so it can swap the binary. Returns false if the launch failed.
bool          launch_detached(const std::string& command, const std::string& working_dir);

} // namespace khora::hand
