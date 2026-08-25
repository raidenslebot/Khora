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
target_link_libraries(extraction_bench PRIVATE khora::ligature khora::lexicon khora::reservoir)

add_executable(corpus_bench corpus_bench.cpp)
target_link_libraries(corpus_bench PRIVATE khora::ligature khora::lexicon khora::reservoir)

add_executable(hypernym_bench hypernym_bench.cpp)
target_link_libraries(hypernym_bench PRIVATE khora::plexus)

add_executable(category_bench category_bench.cpp)
target_link_libraries(category_bench PRIVATE khora::plexus khora::lattice)

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
