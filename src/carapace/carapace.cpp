#include "khora/carapace/carapace.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cctype>
#include <utility>

namespace khora::carapace {

Carapace::Carapace() = default;

bool Carapace::register_tool(Tool t) {
    // First registration wins, and a collision is loud. Silently overwriting is
    // what made four tools unreachable; a name clash is a programming error, not
    // a runtime condition, so it fails the build's assertions rather than
    // degrading quietly at runtime.
    if (tools_.find(t.name) != tools_.end()) {
        assert(false && "carapace: duplicate tool name -- the earlier tool is kept");
        std::fprintf(stderr,
                     "carapace: DUPLICATE TOOL NAME \"%s\" ignored; the existing "
                     "tool is kept\n", t.name.c_str());
        return false;
    }
    tools_.emplace(t.name, std::move(t));
    return true;
}

bool Carapace::has_tool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

std::vector<std::string> Carapace::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [n, _t] : tools_) names.push_back(n);
    std::sort(names.begin(), names.end());
    return names;
}

const Tool* Carapace::find_tool(const std::string& name) const {
    auto it = tools_.find(name);
    return (it == tools_.end()) ? nullptr : &it->second;
}

Intent Carapace::parse(std::string_view line) {
    Intent intent;
    intent.raw.assign(line);

    std::vector<std::string> tokens;
    std::string              current;
    bool                     in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (!in_quotes && std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(std::move(current));

    if (tokens.empty()) return intent;
    intent.verb = std::move(tokens.front());
    intent.args.assign(tokens.begin() + 1, tokens.end());
    return intent;
}

ToolResult Carapace::dispatch(const Intent& intent) const {
    if (intent.verb.empty()) {
        return {false, "", "empty intent"};
    }
    auto it = tools_.find(intent.verb);
    if (it == tools_.end()) {
        return {false, "", "unknown tool: " + intent.verb +
                          " (type 'help' to list available tools)"};
    }
    try {
        return it->second.handler(intent);
    } catch (const std::exception& e) {
        return {false, "", std::string("tool threw: ") + e.what()};
    } catch (...) {
        return {false, "", "tool threw: unknown exception"};
    }
}

ToolResult Carapace::invoke(std::string_view line) const {
    return dispatch(parse(line));
}

} // namespace khora::carapace
