// Placeholder registered ahead of the work so that bench/CMakeLists.txt is not a
// shared surface during a parallel fan-out. Two agents editing it concurrently
// already cost one broken configure today.
#include <cstdio>
int main() { std::printf("attention_bench: not yet implemented\n"); return 0; }
