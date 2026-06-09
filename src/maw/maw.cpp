#include "khora/maw/maw.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace khora::maw {

namespace {

std::uint64_t hash_norm(const std::string& s) {
    // FNV-1a over a lowercased, whitespace-collapsed form.
    std::uint64_t h = 1469598103934665603ull;
    bool prev_space = true;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        char n;
        if (std::isspace(u)) { if (prev_space) continue; n = ' '; prev_space = true; }
        else { n = static_cast<char>(std::tolower(u)); prev_space = false; }
        h ^= static_cast<unsigned char>(n);
        h *= 1099511628211ull;
    }
    return h;
}

bool looks_like_path(const std::string& t) {
    return t.size() >= 3 && (t.find(":\\") != std::string::npos ||
                             (t[0] == '\\' && t[1] == '\\'));
}
bool looks_like_flag(const std::string& t) {
    return t.size() >= 2 && (t[0] == '/' || t[0] == '-') && std::isalpha((unsigned char)t[1]);
}

// A broad seed of real Windows verbs — informative queries AND destructive verbs
// alike. The destructive ones are INCLUDED on purpose: the Maw will reach for them,
// the Bulwark contains them, and Khora charts that they exist and are refused.
const char* kSeedVerbs[] = {
    "dir","type","where","whoami","hostname","ver","vol","tree","set","path","echo",
    "find","findstr","fc","comp","sort","more","tasklist","ipconfig","ping","nslookup",
    "systeminfo","driverquery","netstat","getmac","arp","route","net","nbtstat","wmic",
    "reg","sc","schtasks","attrib","assoc","ftype","cipher","fsutil","powercfg","chcp",
    "date","time","tzutil","clip","timeout","title","color","mode","cmd","powershell",
    "curl","tar","certutil","bcdedit","diskpart","mountvol","label","subst","makecab",
    "del","erase","rd","rmdir","md","copy","move","ren","xcopy","robocopy","mklink",
    "taskkill","start","shutdown","sfc","dism","chkdsk","gpresult","klist","query",
};

} // namespace

void Maw::seed() {
    if (verbs_.empty()) {
        for (const char* v : kSeedVerbs) verbs_.emplace_back(v);
    }
    // Discover real installed tools by scanning PATH for *.exe (capped).
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path);
        std::string dir;
        std::size_t added = 0;
        while (std::getline(ss, dir, ';') && added < 300) {
            if (dir.empty()) continue;
            std::error_code ec;
            std::filesystem::directory_iterator it(dir, ec), end;
            for (; it != end && added < 300; it.increment(ec)) {
                if (ec) break;
                const auto& p = it->path();
                if (p.extension() == ".exe" || p.extension() == ".EXE") {
                    add_capped_(verbs_, p.stem().string(), 500);
                    ++added;
                }
            }
        }
    }
    if (nouns_.empty()) {
        nouns_ = { ".", "..", "C:\\", "C:\\Windows", "C:\\Users", "%TEMP%",
                   "127.0.0.1", "localhost", "8.8.8.8", "example.com" };
    }
    st_.verbs = verbs_.size(); st_.nouns = nouns_.size(); st_.flags = flags_.size();
}

const std::string& Maw::pick_(const std::vector<std::string>& pool) {
    static const std::string empty;
    if (pool.empty()) return empty;
    std::uniform_int_distribution<std::size_t> d(0, pool.size() - 1);
    return pool[d(rng_)];
}

int Maw::pick_mode_() {
    double total = 0; for (double w : mode_w_) total += w;
    std::uniform_real_distribution<double> d(0.0, total);
    double r = d(rng_);
    for (int m = 0; m < 6; ++m) { r -= mode_w_[m]; if (r <= 0) return m; }
    return 0;
}

std::string Maw::generate() {
    if (verbs_.empty()) seed();
    const int mode = pick_mode_();
    last_mode_ = mode;
    std::ostringstream os;
    switch (mode) {
        case 0: {  // recombine: verb [flag] [noun]
            os << pick_(verbs_);
            std::uniform_real_distribution<double> coin(0.0, 1.0);
            if (!flags_.empty() && coin(rng_) < 0.5) os << ' ' << pick_(flags_);
            if (coin(rng_) < 0.5)                    os << ' ' << pick_(nouns_);
            break;
        }
        case 1: {  // mutate a recent novel command by swapping a token
            if (recent_.empty()) { os << pick_(verbs_); break; }
            std::istringstream in(pick_(recent_));
            std::vector<std::string> toks; std::string t;
            while (in >> t) toks.push_back(t);
            if (!toks.empty()) {
                std::uniform_int_distribution<std::size_t> di(0, toks.size() - 1);
                const std::size_t i = di(rng_);
                toks[i] = (i == 0) ? pick_(verbs_)
                                   : (looks_like_flag(toks[i]) && !flags_.empty() ? pick_(flags_)
                                                                                  : pick_(nouns_));
            }
            for (std::size_t i = 0; i < toks.size(); ++i) { if (i) os << ' '; os << toks[i]; }
            break;
        }
        case 2:  os << pick_(verbs_) << " /?";              break;  // harvest help text
        case 3:  os << pick_(verbs_) << ' ' << pick_(nouns_); break; // query a target
        case 4:  os << "dir " << pick_(nouns_);             break;  // expand a path
        case 5:  os << "ping -n 1 " << pick_(nouns_);       break;  // probe network
        default: os << pick_(verbs_);                        break;
    }
    return os.str();
}

void Maw::add_capped_(std::vector<std::string>& pool, const std::string& tok, std::size_t cap) {
    if (tok.empty() || tok.size() > 64) return;
    if (std::find(pool.begin(), pool.end(), tok) != pool.end()) return;
    if (pool.size() < cap) pool.push_back(tok);
}

void Maw::harvest_(const std::string& cmd, const std::string& output) {
    // The verb is the first token; track which verbs have actually run.
    std::istringstream cin(cmd);
    std::string verb; cin >> verb;
    if (!verb.empty()) verbs_run_.insert(verb);

    // Scrape new nouns (paths) and flags from the output so the pools GROW from
    // what the machine actually reveals — recombination compounds on real findings.
    std::istringstream oin(output);
    std::string tok; std::size_t scanned = 0;
    while (oin >> tok && scanned < 400) {
        ++scanned;
        if (looks_like_path(tok)) add_capped_(nouns_, tok, 600);
        else if (looks_like_flag(tok)) add_capped_(flags_, tok, 400);
    }
    st_.verbs = verbs_.size(); st_.nouns = nouns_.size(); st_.flags = flags_.size();
    st_.verbs_run = verbs_run_.size();
}

bool Maw::record(const std::string& cmd, int exit_code, bool killed,
                 const std::string& output) {
    last_relations_.clear();
    ++st_.attempts;
    if (killed)           ++st_.killed;
    else if (exit_code == 0) ++st_.succeeded;
    else                  ++st_.contained;

    const std::uint64_t h = hash_norm(cmd);
    const bool novel = seen_.insert(h).second;
    st_.distinct = seen_.size();

    // Novelty-weighted bandit: reward the generation mode that produced a never-seen
    // command, gently decay one that produced a duplicate — so the drive drifts toward
    // whatever keeps charting new ground, and away from whatever keeps repeating.
    if (last_mode_ >= 0 && last_mode_ < 6) {
        if (novel) mode_w_[last_mode_] = std::min(8.0,  mode_w_[last_mode_] + 0.5);
        else       mode_w_[last_mode_] = std::max(0.25, mode_w_[last_mode_] * 0.97);
    }
    if (novel) {
        ++st_.novel;
        recent_.push_back(cmd);
        if (recent_.size() > 64) recent_.erase(recent_.begin());

        // Distil CLEAN structured facts about the machine. A verb the SHELL RECOGNISED
        // (it ran — even if it was denied or mis-argued) genuinely IS-A command; only a
        // "not recognized" reply means it does not exist. From a /? help we also learn
        // each flag it HAS. Typed, true, no output noise — the bridge to understanding.
        if (!killed && (exit_code == 0 || !output.empty()) &&
            output.find("is not recognized") == std::string::npos) {
            std::istringstream cin(cmd);
            std::string verb; cin >> verb;
            if (verb.size() >= 2 && std::isalpha((unsigned char)verb[0]) &&
                verb != "echo" && verb != "cmd") {
                last_relations_.push_back({0, verb, "command"});
                if (cmd.find("/?") != std::string::npos && !output.empty()) {
                    std::istringstream oin(output);
                    std::string tok; int got = 0;
                    while (oin >> tok && got < 8) {
                        while (!tok.empty() &&
                               !std::isalnum((unsigned char)tok.back())) tok.pop_back();
                        if (looks_like_flag(tok)) { last_relations_.push_back({1, verb, tok}); ++got; }
                    }
                }
            }
        }
    }
    harvest_(cmd, output);
    return novel;
}

double Maw::coverage() const {
    if (verbs_.empty()) return 0.0;
    return static_cast<double>(verbs_run_.size()) / static_cast<double>(verbs_.size());
}

std::vector<std::string> Maw::recent_discoveries(std::size_t k) const {
    std::vector<std::string> out;
    for (auto it = recent_.rbegin(); it != recent_.rend() && out.size() < k; ++it)
        out.push_back(*it);
    return out;
}

void Maw::save(const std::filesystem::path& dir) {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    auto dump = [&](const std::string& name, const std::vector<std::string>& v) {
        std::ofstream o(dir / name, std::ios::trunc);
        for (const auto& s : v) o << s << '\n';
    };
    dump("verbs.txt", verbs_);
    dump("nouns.txt", nouns_);
    dump("flags.txt", flags_);
    { std::ofstream o(dir / "seen.idx", std::ios::trunc);
      for (auto h : seen_) o << h << '\n'; }
    { std::ofstream o(dir / "verbs_run.txt", std::ios::trunc);
      for (const auto& s : verbs_run_) o << s << '\n'; }
    { std::ofstream o(dir / "stats.txt", std::ios::trunc);
      o << st_.attempts << ' ' << st_.novel << ' ' << st_.succeeded << ' '
        << st_.contained << ' ' << st_.killed << '\n'; }
}

void Maw::load(const std::filesystem::path& dir) {
    auto slurp = [&](const std::string& name, std::vector<std::string>& v) {
        std::ifstream in(dir / name); std::string line;
        while (std::getline(in, line)) if (!line.empty()) v.push_back(line);
    };
    slurp("verbs.txt", verbs_);
    slurp("nouns.txt", nouns_);
    slurp("flags.txt", flags_);
    { std::ifstream in(dir / "seen.idx"); std::uint64_t h;
      while (in >> h) seen_.insert(h); }
    { std::ifstream in(dir / "verbs_run.txt"); std::string s;
      while (std::getline(in, s)) if (!s.empty()) verbs_run_.insert(s); }
    { std::ifstream in(dir / "stats.txt");
      in >> st_.attempts >> st_.novel >> st_.succeeded >> st_.contained >> st_.killed; }
    st_.verbs = verbs_.size(); st_.nouns = nouns_.size(); st_.flags = flags_.size();
    st_.distinct = seen_.size(); st_.verbs_run = verbs_run_.size();
}

} // namespace khora::maw
