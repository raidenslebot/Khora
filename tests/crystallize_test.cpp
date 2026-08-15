#include "khora/crystallize/crystallize.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
    else       { std::printf("  ok  : %s\n", what); }
}

std::vector<std::string> taxonomy_corpus() {
    std::vector<std::string> out;
    auto sentence = [&](std::initializer_list<const char*> ws) {
        for (const char* w : ws) out.emplace_back(w);
    };

    for (int i = 0; i < 40; ++i) {
        sentence({"sparrow", "robin", "finch", "jay", "bird", "aviary"});
        sentence({"bird", "sparrow", "robin", "finch", "jay", "flock"});
        sentence({"sparrow", "bird", "wing", "feather", "nest", "flight"});
        sentence({"robin", "bird", "wing", "feather", "nest", "song"});
        sentence({"finch", "bird", "wing", "feather", "nest", "seed"});
        sentence({"jay", "bird", "wing", "feather", "nest", "forest"});
    }
    return out;
}

} // namespace

int main() {
    std::printf("Crystallize test\n");

    khora::plexus::Plexus plex;
    plex.observe(taxonomy_corpus(), 5);

    khora::ligature::Ligature lig;
    lig.add(khora::ligature::Relation::IsA, "robin", "bird", 3);
    lig.add(khora::ligature::Relation::IsA, "finch", "bird", 3);
    lig.add(khora::ligature::Relation::IsA, "jay", "bird", 3);

    khora::crystallize::Options opt;
    opt.min_support = 3;
    const auto c = khora::crystallize::infer_isa(plex, lig, "sparrow", opt);

    check(static_cast<bool>(c), "association consensus crystallizes a candidate");
    check(c.subject == "sparrow", "candidate subject is preserved");
    check(c.parent == "bird", "candidate parent is the corroborated taxonomy");
    check(c.support >= 3, "candidate has multi-kin support");
    check(!c.witnesses.empty(), "candidate records witnesses");
    check(khora::crystallize::commit(lig, c) == 1, "commit writes one relation");
    check(lig.count(khora::ligature::Relation::IsA, "sparrow", "bird") == c.support,
          "commit writes support count");
    check(!khora::crystallize::infer_isa(plex, lig, "sparrow", opt),
          "already-known relation is not rediscovered");

    khora::ligature::Ligature cyclic;
    cyclic.add(khora::ligature::Relation::IsA, "bird", "sparrow", 3);
    const auto blocked = khora::crystallize::infer_isa(plex, cyclic, "sparrow", opt);
    check(!blocked, "cycle-forming relation is refused");

    if (failures == 0) std::printf("ALL PASS\n");
    else               std::printf("%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
