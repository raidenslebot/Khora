add_executable(lattice_bench lattice_bench.cpp)
target_link_libraries(lattice_bench PRIVATE khora::lattice)

add_executable(temporal_memory_bench temporal_memory_bench.cpp)
target_link_libraries(temporal_memory_bench PRIVATE khora::cortex)
