// Registered ahead of the work so bench/CMakeLists.txt is not a shared surface
// during a parallel fan-out. Concurrent edits to it already cost two broken
// configures today; this is the serial pre-step that prevents a third.
#include <cstdio>
int main() { std::printf("verify_bench: not yet implemented\n"); return 0; }
