add_executable(morphus_demo morphus_main.cpp)
target_link_libraries(morphus_demo PRIVATE khora::lattice)

add_executable(persistence_demo persistence_demo.cpp)
target_link_libraries(persistence_demo PRIVATE khora::lattice)

add_executable(synapse_demo synapse_demo.cpp)
target_link_libraries(synapse_demo PRIVATE khora::synapse)
