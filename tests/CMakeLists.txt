add_executable(lattice_test lattice_test.cpp)
target_link_libraries(lattice_test PRIVATE khora::lattice)
add_test(NAME LatticeTest COMMAND lattice_test)

add_executable(sdr_test sdr_test.cpp)
target_link_libraries(sdr_test PRIVATE khora::lattice)
add_test(NAME SdrTest COMMAND sdr_test)

add_executable(temporal_memory_test temporal_memory_test.cpp)
target_link_libraries(temporal_memory_test PRIVATE khora::cortex)
add_test(NAME TemporalMemoryTest COMMAND temporal_memory_test)

add_executable(context_tree_test context_tree_test.cpp)
target_link_libraries(context_tree_test PRIVATE khora::cortex)
add_test(NAME ContextTreeTest COMMAND context_tree_test)

add_executable(novelty_test novelty_test.cpp)
target_link_libraries(novelty_test PRIVATE khora::cortex)
add_test(NAME NoveltyTest COMMAND novelty_test)

add_executable(provenance_test provenance_test.cpp)
target_link_libraries(provenance_test PRIVATE khora::cortex)
add_test(NAME ProvenanceTest COMMAND provenance_test)

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

add_executable(plexus_test plexus_test.cpp)
target_link_libraries(plexus_test PRIVATE khora::plexus)
add_test(NAME PlexusTest COMMAND plexus_test)

add_executable(crystallize_test crystallize_test.cpp)
target_link_libraries(crystallize_test PRIVATE khora::crystallize)
add_test(NAME CrystallizeTest COMMAND crystallize_test)

add_executable(bulwark_test bulwark_test.cpp)
target_link_libraries(bulwark_test PRIVATE khora::bulwark)
add_test(NAME BulwarkTest COMMAND bulwark_test)

add_executable(maw_test maw_test.cpp)
target_link_libraries(maw_test PRIVATE khora::maw)
add_test(NAME MawTest COMMAND maw_test)

add_executable(crucible_test crucible_test.cpp)
target_link_libraries(crucible_test PRIVATE khora::crucible)
add_test(NAME CrucibleTest COMMAND crucible_test)

add_executable(whetstone_test whetstone_test.cpp)
target_link_libraries(whetstone_test PRIVATE khora::whetstone)
add_test(NAME WhetstoneTest COMMAND whetstone_test)

add_executable(ribosome_test ribosome_test.cpp)
target_link_libraries(ribosome_test PRIVATE khora::ribosome)
add_test(NAME RibosomeTest COMMAND ribosome_test)

add_executable(descent_test descent_test.cpp)
target_link_libraries(descent_test PRIVATE khora::descent khora::retina)
add_test(NAME DescentTest COMMAND descent_test)

add_executable(logos_test logos_test.cpp)
target_link_libraries(logos_test PRIVATE khora::logos)
add_test(NAME LogosTest COMMAND logos_test)

add_executable(telos_test telos_test.cpp)
target_link_libraries(telos_test PRIVATE khora::telos)
add_test(NAME TelosTest COMMAND telos_test)

add_executable(retina_test retina_test.cpp)
target_link_libraries(retina_test PRIVATE khora::retina)
add_test(NAME RetinaTest COMMAND retina_test)

add_executable(volition_test volition_test.cpp)
target_link_libraries(volition_test PRIVATE khora::volition khora::telos)
add_test(NAME VolitionTest COMMAND volition_test)

add_executable(taxis_test taxis_test.cpp)
target_link_libraries(taxis_test PRIVATE khora::taxis khora::ligature)
add_test(NAME TaxisTest COMMAND taxis_test)

add_executable(ligature_test ligature_test.cpp)
target_link_libraries(ligature_test PRIVATE khora::ligature)
add_test(NAME LigatureTest COMMAND ligature_test)

add_executable(techne_test techne_test.cpp)
target_link_libraries(techne_test PRIVATE khora::techne)
add_test(NAME TechneTest COMMAND techne_test)

# Every test gets a wall-clock ceiling. Without one a hung test blocks the dev
# loop forever with no signal -- and several of these drive background threads,
# spawn contained processes, or wait on pipes, all of which can hang rather than
# fail. Bulwark is given more room: it deliberately runs a runaway to timeout.
get_property(_khora_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_khora_tests} PROPERTIES TIMEOUT 60)
set_tests_properties(BulwarkTest PROPERTIES TIMEOUT 120)
