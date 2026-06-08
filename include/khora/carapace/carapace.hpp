#pragma once

// Carapace — Khora's outer agentic shell.
//
// Owns a registry of named Tools. Each Tool has a description and a
// handler that takes a parsed Intent and returns a ToolResult. The
// shell parses raw command lines into Intents (whitespace-split with
// double-quote groups) and dispatches them to the registered handler.

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace khora::carapace {

struct Intent {
    std::string              verb;
    std::vector<std::string> args;
    std::string              raw;
};

struct ToolResult {
    bool        ok    = false;
    std::string output;
    std::string error;
};

using ToolHandler = std::function<ToolResult(const Intent&)>;

struct Tool {
    std::string  name;
    std::string  description;
    ToolHandler  handler;
};

class Carapace {
public:
    Carapace();

    void register_tool(Tool t);
    bool has_tool(const std::string& name) const;
    std::vector<std::string> list_tools() const;
    const Tool* find_tool(const std::string& name) const;

    // Parse one line into an Intent. Whitespace-split, but double-quoted
    // groups are kept as one argument with their interior whitespace.
    static Intent parse(std::string_view line);

    // Dispatch a parsed Intent. Unknown verbs return ok=false with a
    // helpful error.
    ToolResult dispatch(const Intent& intent) const;

    // Convenience: parse + dispatch.
    ToolResult invoke(std::string_view line) const;

private:
    std::unordered_map<std::string, Tool> tools_;
};

} // namespace khora::carapace
