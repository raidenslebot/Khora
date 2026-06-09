#include "khora/hand/hand.hpp"

#include <chrono>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace khora::hand {

#ifdef _WIN32

ActionResult execute(const std::string& command, int timeout_ms) {
    ActionResult r;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        r.error = "CreatePipe failed";
        return r;
    }
    // The read end stays with us and must NOT be inherited by the child.
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;          // combine stderr into the same stream
    si.hStdInput  = nullptr;

    PROCESS_INFORMATION pi{};

    // Run through the shell so builtins, pipes and redirection all work — Khora
    // gets the machine's real command surface, not a crippled subset.
    std::string cmdline = "cmd.exe /c " + command;
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back('\0');

    BOOL launched = CreateProcessA(
        nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(wr);   // parent does not write; close so reads see EOF at child exit

    if (!launched) {
        CloseHandle(rd);
        r.error = "could not launch process (error " +
                  std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return r;
    }
    r.ran = true;

    const auto start = std::chrono::steady_clock::now();
    std::string out;
    constexpr std::size_t kCap = 1u << 20;   // 1 MiB output cap

    auto drain = [&]() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
                break;
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!ReadFile(rd, chunk.data(), avail, &got, nullptr) || got == 0)
                break;
            chunk.resize(got);
            out += chunk;
            if (out.size() > kCap) { out.resize(kCap); break; }
        }
    };

    for (;;) {
        drain();
        if (out.size() >= kCap) { TerminateProcess(pi.hProcess, 1); break; }

        const DWORD w = WaitForSingleObject(pi.hProcess, 15);
        if (w == WAIT_OBJECT_0) { drain(); break; }   // process finished

        if (timeout_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                TerminateProcess(pi.hProcess, 1);
                r.timed_out = true;
                drain();
                break;
            }
        }
    }

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = static_cast<int>(code);

    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    r.output = std::move(out);
    return r;
}

std::string own_executable_path() {
    char buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    return std::string(buf, n);
}

unsigned long current_process_id() {
    return static_cast<unsigned long>(GetCurrentProcessId());
}

bool launch_detached(const std::string& command, const std::string& working_dir) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmd(command.begin(), command.end()); cmd.push_back('\0');
    const BOOL ok = CreateProcessA(
        nullptr, cmd.data(), nullptr, nullptr, FALSE,
        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
        nullptr, working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

#else   // non-Windows fallback (Khora targets Windows; kept for portability)

ActionResult execute(const std::string&, int) {
    ActionResult r;
    r.error = "action only implemented on Win32";
    return r;
}
std::string   own_executable_path() { return {}; }
unsigned long current_process_id()  { return 0; }
bool          launch_detached(const std::string&, const std::string&) { return false; }

#endif

} // namespace khora::hand
