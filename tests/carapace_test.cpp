// Tests for the Carapace shell.

#include "khora/carapace/builtin_tools.hpp"
#include "khora/carapace/carapace.hpp"

#include <cstdio>
#include <string>

namespace {
int g_total = 0;
int g_failed = 0;
}

#define EXPECT(cond, msg) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); } \
} while (0)

using namespace khora::carapace;

int main() {
    // 1. Whitespace parse.
    {
        auto i = Carapace::parse("verb arg1 arg2 arg3");
        EXPECT(i.verb == "verb", "parse: verb");
        EXPECT(i.args.size() == 3, "parse: 3 args");
        EXPECT(i.args[0] == "arg1" && i.args[2] == "arg3", "parse: arg values");
    }

    // 2. Quoted-string parse.
    {
        auto i = Carapace::parse(R"(echo "hello world" how are you)");
        EXPECT(i.verb == "echo", "parse: quoted verb");
        EXPECT(i.args.size() == 4, "parse: quoted produces 4 args");
        EXPECT(i.args[0] == "hello world", "parse: quoted preserves spaces");
        EXPECT(i.args[1] == "how", "parse: rest after quoted");
    }

    // 3. Empty / whitespace-only input.
    {
        auto i = Carapace::parse("   ");
        EXPECT(i.verb.empty(), "parse: whitespace gives empty verb");
        EXPECT(i.args.empty(), "parse: no args");
    }

    // 4. Dispatch: unknown verb returns error.
    {
        Carapace c;
        register_core_tools(c);
        auto r = c.invoke("nonexistent_tool foo");
        EXPECT(!r.ok, "unknown verb yields error");
        EXPECT(!r.error.empty(), "error message populated");
    }

    // 5. echo round-trip.
    {
        Carapace c;
        register_core_tools(c);
        auto r = c.invoke("echo hello world");
        EXPECT(r.ok, "echo ok");
        EXPECT(r.output == "hello world", "echo output");
    }

    // 6. help lists registered tools alphabetically.
    {
        Carapace c;
        register_core_tools(c);
        auto r = c.invoke("help");
        EXPECT(r.ok, "help ok");
        EXPECT(r.output.find("echo") != std::string::npos, "help mentions echo");
        EXPECT(r.output.find("now") != std::string::npos, "help mentions now");
    }

    // 7. memory tools round-trip.
    {
        khora::lattice::Lattice mem;
        Carapace c;
        register_memory_tools(c, mem);
        auto r1 = c.invoke("memorize alpha");
        EXPECT(r1.ok, "memorize ok");
        EXPECT(mem.size() == 1, "lattice grew by 1");

        auto r2 = c.invoke("recall alpha");
        EXPECT(r2.ok, "recall ok");

        auto r3 = c.invoke("recall missing");
        EXPECT(!r3.ok, "recall on missing label fails");

        auto r4 = c.invoke("memorize bravo");
        EXPECT(r4.ok, "memorize bravo");

        auto r5 = c.invoke("query alpha 2");
        EXPECT(r5.ok, "query ok");
        EXPECT(r5.output.find("alpha") != std::string::npos, "query returns alpha as top");
    }

    // 8. Cortex tools wire correctly.
    {
        khora::cortex::PredictiveColumn col(2);
        Carapace c;
        register_cortex_tools(c, col);
        c.invoke("learn the");
        c.invoke("learn quick");
        c.invoke("learn brown");
        auto r = c.invoke("cortex_stats");
        EXPECT(r.ok, "cortex_stats ok");
        EXPECT(r.output.find("observations") != std::string::npos, "stats mentions observations");
    }

    // 9. Soma tools wire correctly.
    {
        khora::soma::SomaNexus s;
        Carapace c;
        register_soma_tools(c, s);
        auto r1 = c.invoke("mood");
        EXPECT(r1.ok, "mood ok");
        auto r2 = c.invoke("stimulate Curiosity 0.2");
        EXPECT(r2.ok, "stimulate ok");
        auto r3 = c.invoke("stimulate BogusDrive 0.1");
        EXPECT(!r3.ok, "unknown drive rejected");
    }

    // 10. Handler throwing is caught.
    {
        Carapace c;
        c.register_tool({
            "boom", "throws",
            [](const Intent&) -> ToolResult { throw std::runtime_error("kaboom"); }
        });
        auto r = c.invoke("boom");
        EXPECT(!r.ok, "throwing handler returns error");
        EXPECT(r.error.find("kaboom") != std::string::npos, "error includes exception what()");
    }

    std::printf("\nCarapace tests: %d/%d passed (%d failed).\n",
                g_total - g_failed, g_total, g_failed);
    return g_failed == 0 ? 0 : 1;
}
