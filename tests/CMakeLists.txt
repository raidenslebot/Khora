add_executable(lattice_test lattice_test.cpp)
target_link_libraries(lattice_test PRIVATE khora::lattice)
add_test(NAME LatticeTest COMMAND lattice_test)

add_executable(persistence_test persistence_test.cpp)
target_link_libraries(persistence_test PRIVATE khora::lattice)
add_test(NAME PersistenceTest COMMAND persistence_test)

add_executable(synapse_test synapse_test.cpp)
target_link_libraries(synapse_test PRIVATE khora::synapse)
add_test(NAME SynapseTest COMMAND synapse_test)

add_executable(cortex_test cortex_test.cpp)
target_link_libraries(cortex_test PRIVATE khora::cortex)
add_test(NAME CortexTest COMMAND cortex_test)
