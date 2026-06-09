#pragma once

// The Bulwark — Khora's CONTAINMENT cage for autonomous action.
//
// The operator's intent: Khora must keep its WHOLE capability — every command, every
// installed tool, the internet, the dangerous verbs it WILL reach for through chaos —
// but the blast radius must be contained so the real single-PC machine can never be
// bricked or locked up. Contain the blast radius, NOT the capability.
//
// The Hand (khora::hand) is the UNcontained effector: it is for the OPERATOR, who is a
// human in control of their own machine. The Bulwark is the path for AUTONOMOUS action
// (the chaos-exploration drive, self-rewrite under autonomy): every command runs inside
// a Job-Object cage and under a low-integrity, non-admin token, with the OS access check
// — not a string blocklist — as the wall. A `del C:\Windows`, a `format`, a `shutdown`
// therefore EXECUTE (Khora observes the attempt and its refusal, and learns) but cannot
// touch the real machine.
//
// Every control is FAIL-CLOSED: if any containment primitive cannot be applied, the
// Bulwark launches NOTHING. "Uncontained by accident" must be impossible. This is the
// foundation; it is proven by self_check() BEFORE any autonomous driver is allowed to
// use it. (A VHDX-isolated cell, a SYSTEM Warden reaping persistence, and host UAC
// hardening are stronger layers above this — see install/harden_host.ps1.)

#include <filesystem>
#include <string>

namespace khora::bulwark {

struct ContainedResult {
    bool        ran           = false;   // a process actually launched inside the cage
    bool        timed_out     = false;   // exceeded timeout; the whole job tree was killed
    bool        killed_by_job = false;   // terminated via the Job Object (tree-kill)
    int         exit_code     = -1;
    int         tier          = 0;       // 0 = NOT contained (refused), 1 = resource cage,
                                         // 2 = resource cage + low-integrity non-admin token
    std::string output;                  // captured stdout + stderr (capped)
    std::string error;                   // why it refused / failed, if it did not run
};

// Create the disposable cell directory. FAIL-CLOSED: returns false if it cannot be
// created on a present, writable volume.
bool ensure_cell(const std::filesystem::path& cell_root);

// Run a command fully contained. The capability is whole (any command), the blast
// radius is the cell: Job cage (kill-on-close, NO breakaway, active-process cap, RAM
// cap relative to physical memory, CPU hard-cap, idle priority), a low-integrity
// non-admin restricted token, the cell as the working directory, and a system-volume
// free-space floor. If a required primitive fails, it returns {ran=false, tier=0}.
ContainedResult execute_contained(const std::string& command, int timeout_ms = 30000);

// Prove the cage holds, with canaries: (a) a contained command runs and is captured;
// (b) at full tier a write to a protected host path is DENIED; (c) a runaway is killed
// by the job on timeout. Returns the achieved tier (0 = containment FAILED — autonomous
// exploration must be WITHHELD; 2 = full). Human-readable findings go into `report`.
int self_check(std::string& report);

} // namespace khora::bulwark
