#include "khora/bulwark/bulwark.hpp"

#include <chrono>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  pragma comment(lib, "advapi32.lib")
#endif

namespace khora::bulwark {

#ifdef _WIN32

namespace {

constexpr unsigned long long kReserveBytes  = 8ull << 30;   // always leave 8 GiB for the OS
constexpr unsigned long long kDiskFloorBytes = 5ull << 30;  // refuse to run if < 5 GiB free
constexpr std::size_t        kOutputCap     = 1u << 20;     // 1 MiB

// Grant the token's OWN user SID full access in its default DACL.
//
// Every kernel object a process creates without an explicit descriptor gets its
// token's default DACL. Ours grants only SYSTEM and BUILTIN\Administrators — and
// when Khora runs ELEVATED, Administrators is the token's group owner and the
// only non-SYSTEM grant there is. CreateRestrictedToken then marks that SID
// deny-only, so the child cannot open the objects it creates for itself: ntdll's
// loader fails and the process dies with STATUS_DLL_INIT_FAILED (0xC0000142)
// before reaching main. CreateProcessAsUser still returns TRUE, so the cage
// happily reported "ran, tier 2" for a process that executed nothing.
//
// Adding the user's own SID is what Chromium's sandbox does (AddUserToDefaultDacl)
// for exactly this reason. It does not weaken containment: the integrity level,
// not this DACL, is what governs access to anything outside the child itself.
bool grant_self_in_default_dacl(HANDLE tok) {
    DWORD n = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
    if (n == 0) return false;
    std::vector<BYTE> ub(n);
    if (!GetTokenInformation(tok, TokenUser, ub.data(), n, &n)) return false;
    PSID user = reinterpret_cast<TOKEN_USER*>(ub.data())->User.Sid;
    if (!user || !IsValidSid(user)) return false;

    n = 0;
    GetTokenInformation(tok, TokenDefaultDacl, nullptr, 0, &n);
    if (n == 0) return false;
    std::vector<BYTE> db(n);
    if (!GetTokenInformation(tok, TokenDefaultDacl, db.data(), n, &n)) return false;
    const ACL* old = reinterpret_cast<TOKEN_DEFAULT_DACL*>(db.data())->DefaultDacl;

    const DWORD sz = (old ? old->AclSize : static_cast<DWORD>(sizeof(ACL))) +
                     static_cast<DWORD>(sizeof(ACCESS_ALLOWED_ACE)) + GetLengthSid(user);
    std::vector<BYTE> nb(sz);
    ACL* acl = reinterpret_cast<ACL*>(nb.data());
    if (!InitializeAcl(acl, sz, ACL_REVISION)) return false;
    if (old) {
        for (DWORD i = 0; i < old->AceCount; ++i) {
            LPVOID ace = nullptr;
            if (GetAce(const_cast<ACL*>(old), i, &ace)) {
                if (!AddAce(acl, ACL_REVISION, MAXDWORD, ace,
                            reinterpret_cast<ACE_HEADER*>(ace)->AceSize))
                    return false;
            }
        }
    }
    if (!AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, user)) return false;

    TOKEN_DEFAULT_DACL tdd{};
    tdd.DefaultDacl = acl;
    return SetTokenInformation(tok, TokenDefaultDacl, &tdd, sizeof(tdd)) != FALSE;
}

// Build a LOW-INTEGRITY, NON-ADMIN restricted token derived from our own primary token.
// Returns nullptr if it cannot (caller then runs at the resource-only tier).
HANDLE make_low_il_nonadmin_token() {
    HANDLE self = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY |
                          TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_GROUPS,
                          &self))
        return nullptr;

    // Disable the BUILTIN\Administrators alias so the token is no longer admin.
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID adminSid = nullptr;
    AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                             DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid);
    SID_AND_ATTRIBUTES toDisable{};
    toDisable.Sid        = adminSid;
    toDisable.Attributes = 0;

    HANDLE restricted = nullptr;
    const BOOL ok = CreateRestrictedToken(
        self, DISABLE_MAX_PRIVILEGE,   // drop every privilege
        adminSid ? 1u : 0u, adminSid ? &toDisable : nullptr,
        0, nullptr, 0, nullptr, &restricted);

    if (adminSid) FreeSid(adminSid);
    CloseHandle(self);
    if (!ok || !restricted) return nullptr;

    // Without this the restricted token is worse than no token: children die in
    // the loader while the cage reports full containment. Degrade honestly to the
    // resource-only tier rather than hand back a token that silently kills.
    if (!grant_self_in_default_dacl(restricted)) {
        CloseHandle(restricted);
        return nullptr;
    }

    // Lower the token's integrity to LOW (S-1-16-4096) — blocks writes to medium+ objects.
    SID_IDENTIFIER_AUTHORITY mlAuth = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID lowSid = nullptr;
    if (AllocateAndInitializeSid(&mlAuth, 1, SECURITY_MANDATORY_LOW_RID,
                                 0, 0, 0, 0, 0, 0, 0, &lowSid)) {
        TOKEN_MANDATORY_LABEL tml{};
        tml.Label.Sid        = lowSid;
        tml.Label.Attributes = SE_GROUP_INTEGRITY;
        SetTokenInformation(restricted, TokenIntegrityLevel, &tml,
                            sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(lowSid));
        FreeSid(lowSid);
    }
    return restricted;
}

// Configure the Job Object with every hard limit (the red-team's full set).
bool harden_job(HANDLE job) {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    const unsigned long long jobMem =
        (ms.ullTotalPhys > kReserveBytes) ? (ms.ullTotalPhys - kReserveBytes)
                                          : (ms.ullTotalPhys / 2);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
    eli.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |          // closing the handle kills the whole tree
        JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS |             // fork-bomb cap
        JOB_OBJECT_LIMIT_PROCESS_MEMORY |
        JOB_OBJECT_LIMIT_JOB_MEMORY |
        JOB_OBJECT_LIMIT_PRIORITY_CLASS;
    // NOTE: BREAKAWAY flags are deliberately NOT set — children cannot escape the job.
    eli.BasicLimitInformation.ActiveProcessLimit = 64;
    eli.BasicLimitInformation.PriorityClass      = IDLE_PRIORITY_CLASS;
    eli.ProcessMemoryLimit = 4ull << 30;
    eli.JobMemoryLimit     = static_cast<SIZE_T>(jobMem);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &eli, sizeof(eli)))
        return false;

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpu.CpuRate      = 5000;   // 50.00% of TOTAL machine CPU (hundredths of a percent)
    SetInformationJobObject(job, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu));

    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{};
    ui.UIRestrictionsClass =
        JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_EXITWINDOWS |
        JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
    SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof(ui));
    return true;
}

unsigned long long system_volume_free() {
    char win[MAX_PATH]{};
    if (!GetWindowsDirectoryA(win, MAX_PATH)) return 0;
    const std::string vol = std::string(win).substr(0, 3);   // "C:\"
    ULARGE_INTEGER freeAvail{};
    if (!GetDiskFreeSpaceExA(vol.c_str(), &freeAvail, nullptr, nullptr)) return 0;
    return freeAvail.QuadPart;
}

} // namespace

bool ensure_cell(const std::filesystem::path& cell_root) {
    std::error_code ec;
    std::filesystem::create_directories(cell_root, ec);
    return std::filesystem::exists(cell_root, ec) &&
           std::filesystem::is_directory(cell_root, ec);
}

ContainedResult execute_contained(const std::string& command, int timeout_ms) {
    ContainedResult r;

    // FAIL-CLOSED gate 1: the cell must exist on a present volume.
    const std::filesystem::path cell = std::filesystem::path("data") / "bulwark" / "cell";
    if (!ensure_cell(cell)) {
        r.error = "containment: cannot create cell — refusing to run uncontained";
        return r;
    }
    // FAIL-CLOSED gate 2: never let Khora finish filling the boot volume.
    if (system_volume_free() < kDiskFloorBytes) {
        r.error = "containment: system volume below safe free-space floor — refusing";
        return r;
    }

    // FAIL-CLOSED gate 3: the Job cage must be created and hardened.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);   // unnamed: no collision
    if (!job) { r.error = "containment: CreateJobObject failed"; return r; }
    if (!harden_job(job)) {
        CloseHandle(job);
        r.error = "containment: could not apply job limits — refusing";
        return r;
    }

    // The pipe for captured output.
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        CloseHandle(job);
        r.error = "containment: CreatePipe failed";
        return r;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = nullptr;

    // FAIL-CLOSED gate 4: the shell is named by ABSOLUTE path. With a bare
    // "cmd.exe" and no lpApplicationName, CreateProcess searches PATH — so any
    // writable directory ahead of System32 could substitute the interpreter that
    // runs every contained command. A cage whose shell can be swapped from
    // outside is not a cage.
    PROCESS_INFORMATION pi{};
    char sysdir[MAX_PATH]{};
    const UINT sysdir_len = GetSystemDirectoryA(sysdir, MAX_PATH);
    if (sysdir_len == 0 || sysdir_len >= MAX_PATH) {
        CloseHandle(rd); CloseHandle(wr); CloseHandle(job);
        r.error = "containment: cannot resolve the system directory — refusing";
        return r;
    }
    std::string cmdline = std::string("\"") + sysdir + "\\cmd.exe\" /c " + command;
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back('\0');

    const std::string cellStr = cell.string();

    // Try the low-integrity, non-admin token first (tier 2). Launch SUSPENDED so the
    // child is assigned to the job BEFORE it runs a single instruction (closes the
    // fork-bomb / breakaway race).
    HANDLE tok = make_low_il_nonadmin_token();
    BOOL launched = FALSE;
    int  tier = 0;
    const DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;

    if (tok) {
        launched = CreateProcessAsUserA(tok, nullptr, mutable_cmd.data(), nullptr, nullptr,
                                        TRUE, flags, nullptr, cellStr.c_str(), &si, &pi);
        if (launched) tier = 2;
    }
    if (!launched) {
        // Fall back to the resource cage WITHOUT the low-IL token (tier 1). Still
        // contained for resources; callers that need namespace isolation check tier.
        launched = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                                  flags, nullptr, cellStr.c_str(), &si, &pi);
        if (launched) tier = 1;
    }

    CloseHandle(wr);
    if (tok) CloseHandle(tok);

    if (!launched) {
        CloseHandle(rd);
        CloseHandle(job);
        r.error = "containment: could not launch contained process (error " +
                  std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return r;
    }

    // FAIL-CLOSED gate 5: assign to the job BEFORE resuming, and only resume if
    // the assignment took. Ignoring this return value meant a failed assignment
    // still reported tier 2 while the process ran entirely outside the cage —
    // the one outcome containment exists to prevent. The child is still
    // suspended here, so it has executed nothing and can be killed cleanly.
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        const DWORD err = GetLastError();
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        CloseHandle(rd); CloseHandle(job);
        r.error = "containment: could not assign the process to the job (error " +
                  std::to_string(static_cast<unsigned long>(err)) + ") — refusing";
        return r;
    }
    ResumeThread(pi.hThread);

    r.ran  = true;
    r.tier = tier;

    const auto start = std::chrono::steady_clock::now();
    std::string out;
    auto drain = [&]() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!ReadFile(rd, chunk.data(), avail, &got, nullptr) || got == 0) break;
            chunk.resize(got);
            out += chunk;
            if (out.size() > kOutputCap) { out.resize(kOutputCap); break; }
        }
    };

    for (;;) {
        drain();
        if (out.size() >= kOutputCap) {
            TerminateJobObject(job, 1); r.killed_by_job = true; break;
        }
        if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) { drain(); break; }
        if (timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                TerminateJobObject(job, 1);   // kill the WHOLE tree, not just cmd.exe
                r.timed_out = true; r.killed_by_job = true;
                drain();
                break;
            }
        }
    }

    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);
    r.output    = std::move(out);

    // A loader failure means the process never reached its entry point, so its
    // silence is not a result. Say so, rather than letting a caller read "ran,
    // tier 2, no output" as a command that succeeded quietly.
    if (code == 0xC0000142u /* STATUS_DLL_INIT_FAILED */ && r.output.empty()) {
        r.error = "containment: the contained process died in the loader "
                  "(STATUS_DLL_INIT_FAILED) — it executed nothing";
    }

    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(job);   // KILL_ON_JOB_CLOSE: any straggler descendant dies here
    return r;
}

int self_check(std::string& report) {
    std::string rep;

    // Canary (a): a contained command runs and is captured.
    const auto echo = execute_contained("echo khora_contained_ok", 15000);
    const bool launches = echo.ran && echo.output.find("khora_contained_ok") != std::string::npos;
    rep += launches ? "[ok] contained command launches and is captured\n"
                    : "[FAIL] contained command did not launch/capture\n";
    if (!launches) { report = rep; return 0; }
    rep += "    achieved token tier: " + std::to_string(echo.tier) +
           (echo.tier >= 2 ? " (low-integrity, non-admin)\n" : " (resource cage only)\n");

    // Canary (b): a write to a protected host path must be DENIED at full tier.
    bool protected_denied = true;
    if (echo.tier >= 2) {
        const auto w = execute_contained(
            "echo x > C:\\Windows\\khora_canary_DELETEME.txt", 15000);
        // Low IL cannot write under C:\Windows -> nonzero exit / no file.
        protected_denied = (w.exit_code != 0);
        // Defensive: if it somehow landed, remove it via the uncontained nothing —
        // we cannot here; report loudly instead.
        rep += protected_denied
                 ? "[ok] write to C:\\Windows DENIED by containment\n"
                 : "[FAIL] write to C:\\Windows SUCCEEDED — containment NOT holding\n";
    } else {
        rep += "[warn] low-integrity token unavailable; namespace isolation NOT proven\n";
    }

    // Canary (c): a runaway is killed by the job on timeout.
    const auto runaway = execute_contained(kRunawayCanary, 2000);
    const bool killed = runaway.timed_out && runaway.killed_by_job;
    rep += killed ? "[ok] runaway process killed by the job on timeout\n"
                  : "[FAIL] runaway not killed by the job\n";

    report = rep;
    const bool full = launches && protected_denied && killed && echo.tier >= 2;
    if (full) return 2;
    if (launches && killed) return 1;   // resource cage works; integrity not proven
    return 0;
}

#else   // ---- non-Windows fallback ----

bool ensure_cell(const std::filesystem::path&) { return false; }
ContainedResult execute_contained(const std::string&, int) {
    ContainedResult r; r.error = "containment only implemented on Win32"; return r;
}
int self_check(std::string& report) { report = "containment only on Win32\n"; return 0; }

#endif

} // namespace khora::bulwark
