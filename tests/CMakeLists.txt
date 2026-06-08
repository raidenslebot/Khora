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

add_executable(soma_test soma_test.cpp)
target_link_libraries(soma_test PRIVATE khora::soma)
add_test(NAME SomaTest COMMAND soma_test)

add_executable(reverie_test reverie_test.cpp)
target_link_libraries(reverie_test PRIVATE khora::reverie)
add_test(NAME ReverieTest COMMAND reverie_test)

add_executable(carapace_test carapace_test.cpp)
target_link_libraries(carapace_test PRIVATE khora::carapace)
add_test(NAME CarapaceTest COMMAND carapace_test)

add_executable(lexicon_test lexicon_test.cpp)
target_link_libraries(lexicon_test PRIVATE khora::lexicon)
add_test(NAME LexiconTest COMMAND lexicon_test)

add_executable(reservoir_test reservoir_test.cpp)
target_link_libraries(reservoir_test PRIVATE khora::reservoir)
add_test(NAME ReservoirTest COMMAND reservoir_test)
