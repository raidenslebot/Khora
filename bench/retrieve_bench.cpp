// Registered ahead of the fan-out so bench/CMakeLists.txt is not a shared
// surface. Information retrieval is the remaining unmeasured axis: the Plexus
// answers neighbour queries and has never been scored as a retrieval engine
// (precision@k, MRR, nDCG) against a lexical baseline.
#include <cstdio>

int main() {
    std::printf("retrieve_bench: not yet implemented\n");
    return 0;
}
