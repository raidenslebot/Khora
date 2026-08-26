add_executable(lattice_bench lattice_bench.cpp)
target_link_libraries(lattice_bench PRIVATE khora::lattice)

add_executable(temporal_memory_bench temporal_memory_bench.cpp)
target_link_libraries(temporal_memory_bench PRIVATE khora::cortex)

add_executable(scaling_bench scaling_bench.cpp)
target_link_libraries(scaling_bench PRIVATE khora::cortex)

add_executable(topology_bench topology_bench.cpp)
target_link_libraries(topology_bench PRIVATE khora::plexus)

add_executable(curiosity_bench curiosity_bench.cpp)
target_link_libraries(curiosity_bench PRIVATE khora::cortex)

add_executable(invivo_bench invivo_bench.cpp)
target_link_libraries(invivo_bench PRIVATE khora::cortex khora::reservoir)

add_executable(knowledge_bench knowledge_bench.cpp)
target_link_libraries(knowledge_bench PRIVATE khora::ligature)

add_executable(extraction_bench extraction_bench.cpp)
target_link_libraries(extraction_bench PRIVATE khora::ligature khora::lexicon khora::reservoir khora::taxis)

add_executable(corpus_bench corpus_bench.cpp)
target_link_libraries(corpus_bench PRIVATE khora::ligature khora::lexicon khora::reservoir)

add_executable(hypernym_bench hypernym_bench.cpp)
target_link_libraries(hypernym_bench PRIVATE khora::plexus)

add_executable(category_bench category_bench.cpp)
target_link_libraries(category_bench PRIVATE khora::plexus khora::lattice)

add_executable(spelling_bench spelling_bench.cpp)
target_link_libraries(spelling_bench PRIVATE khora::descent khora::taxis khora::lexicon khora::reservoir)

add_executable(category_eval category_eval.cpp)
target_link_libraries(category_eval PRIVATE khora::plexus khora::lattice)

add_executable(richclub_bench richclub_bench.cpp)
target_link_libraries(richclub_bench PRIVATE khora::plexus khora::lexicon khora::reservoir)

add_executable(growth_bench growth_bench.cpp)
target_link_libraries(growth_bench PRIVATE khora::cortex khora::plexus khora::reservoir)

add_executable(population_bench population_bench.cpp)
target_link_libraries(population_bench PRIVATE khora::cortex khora::reservoir)

add_executable(vigilance_bench vigilance_bench.cpp)
target_link_libraries(vigilance_bench PRIVATE khora::cortex khora::reservoir)

add_executable(recurrence_bench recurrence_bench.cpp)
target_link_libraries(recurrence_bench PRIVATE khora::reservoir)

add_executable(prediction_bench prediction_bench.cpp)
target_link_libraries(prediction_bench PRIVATE khora::cortex khora::reservoir)

add_executable(ribosome_bench ribosome_bench.cpp)
target_link_libraries(ribosome_bench PRIVATE khora::ribosome khora::plexus khora::lexicon)

add_executable(autopoiesis_bench autopoiesis_bench.cpp)
target_link_libraries(autopoiesis_bench PRIVATE khora::ribosome khora::plexus khora::reservoir)

add_executable(techne_bench techne_bench.cpp)
target_link_libraries(techne_bench PRIVATE khora::techne)

add_executable(throughput_bench throughput_bench.cpp)
target_link_libraries(throughput_bench PRIVATE khora::techne)

add_executable(selfhost_bench selfhost_bench.cpp)
target_link_libraries(selfhost_bench PRIVATE khora::techne)

add_executable(bidir_bench bidir_bench.cpp)
target_link_libraries(bidir_bench PRIVATE khora::techne)

add_executable(text_bench text_bench.cpp)
target_link_libraries(text_bench PRIVATE khora::techne)

add_executable(proof_bench proof_bench.cpp)
target_link_libraries(proof_bench PRIVATE khora::techne)

add_executable(diagnosis_bench diagnosis_bench.cpp)
target_link_libraries(diagnosis_bench PRIVATE khora::techne)

add_executable(differential_bench differential_bench.cpp)
target_link_libraries(differential_bench PRIVATE khora::techne)

add_executable(governor_bench governor_bench.cpp)
target_link_libraries(governor_bench PRIVATE khora::governor)

add_executable(ascent_bench ascent_bench.cpp)
target_link_libraries(ascent_bench PRIVATE khora::techne khora::governor)

add_executable(fixedbar_bench fixedbar_bench.cpp)
target_link_libraries(fixedbar_bench PRIVATE khora::techne)

add_executable(analogy_bench analogy_bench.cpp)
target_link_libraries(analogy_bench PRIVATE khora::lattice)

add_executable(chiasm_bench chiasm_bench.cpp)
target_link_libraries(chiasm_bench PRIVATE khora::chiasm khora::retina khora::akoe khora::lexicon)

add_executable(substrate_bench substrate_bench.cpp)
target_link_libraries(substrate_bench PRIVATE khora::lattice)

add_executable(capability_bench capability_bench.cpp)
target_link_libraries(capability_bench PRIVATE khora::chiasm khora::techne)

add_executable(compound_bench compound_bench.cpp)
target_link_libraries(compound_bench PRIVATE khora::chiasm khora::techne)

add_executable(forgetting_bench forgetting_bench.cpp)
target_link_libraries(forgetting_bench PRIVATE khora::descent khora::lattice)

add_executable(structure_bench structure_bench.cpp)
target_link_libraries(structure_bench PRIVATE khora::lattice)
