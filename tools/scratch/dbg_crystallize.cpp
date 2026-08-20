#include "khora/crystallize/crystallize.hpp"
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> corpus() {
    std::vector<std::string> out;
    auto s = [&](std::initializer_list<const char*> ws) { for (const char* w : ws) out.emplace_back(w); };
    for (int i = 0; i < 40; ++i) {
        s({"sparrow","robin","finch","jay","bird","aviary"});
        s({"bird","sparrow","robin","finch","jay","flock"});
        s({"sparrow","bird","wing","feather","nest","flight"});
        s({"robin","bird","wing","feather","nest","song"});
        s({"finch","bird","wing","feather","nest","seed"});
        s({"jay","bird","wing","feather","nest","forest"});
    }
    return out;
}

int main() {
    khora::plexus::Plexus plex;
    plex.observe(corpus(), 5);
    std::printf("vocab=%zu total_tokens=%llu\n", plex.vocabulary_size(), (unsigned long long)plex.total_tokens());

    khora::ligature::Ligature lig;
    lig.add(khora::ligature::Relation::IsA, "robin", "bird", 3);
    lig.add(khora::ligature::Relation::IsA, "finch", "bird", 3);
    lig.add(khora::ligature::Relation::IsA, "jay", "bird", 3);

    // print associates of sparrow
    auto kin = plex.associates("sparrow", 32);
    std::printf("associates of sparrow (k=%zu):\n", kin.size());
    for (auto& [k,w] : kin) std::printf("  %-10s %.4f\n", k.c_str(), w);

    // print IsA objects for each kin
    for (auto& [k,w] : kin) {
        auto objs = lig.objects(khora::ligature::Relation::IsA, k, 6);
        std::printf("  IsA objects of %s:\n", k.c_str());
        for (auto& [o,n] : objs) std::printf("      %-10s n=%u\n", o.c_str(), n);
    }

    std::printf("affinity(sparrow,bird) = %.4f\n", plex.affinity("sparrow","bird"));

    khora::crystallize::Options opt; opt.min_support = 3;
    auto c = khora::crystallize::infer_isa(plex, lig, "sparrow", opt);
    std::printf("infer_isa bool=%d subject='%s' parent='%s' support=%u witnesses=%zu\n",
        (bool)c, c.subject.c_str(), c.parent.c_str(), c.support, c.witnesses.size());
    for (auto& w : c.witnesses) std::printf("    witness: %s\n", w.c_str());
    return 0;
}
