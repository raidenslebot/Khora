add_executable(lattice_bench lattice_bench.cpp)
target_link_libraries(lattice_bench PRIVATE khora::lattice)

add_executable(temporal_memory_bench temporal_memory_bench.cpp)
target_link_libraries(temporal_memory_bench PRIVATE khora::cortex)

add_executable(scaling_bench scaling_bench.cpp)
target_link_libraries(scaling_bench PRIVATE khora::cortex)

add_executable(topology_bench topology_bench.cpp)
target_link_libraries(topology_bench PRIVATE khora::plexus)
