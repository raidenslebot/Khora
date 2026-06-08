add_executable(morphus_demo morphus_main.cpp)
target_link_libraries(morphus_demo PRIVATE khora::lattice)

add_executable(persistence_demo persistence_demo.cpp)
target_link_libraries(persistence_demo PRIVATE khora::lattice)

add_executable(synapse_demo synapse_demo.cpp)
target_link_libraries(synapse_demo PRIVATE khora::synapse)

add_executable(cortex_demo cortex_demo.cpp)
target_link_libraries(cortex_demo PRIVATE khora::cortex)

add_executable(soma_demo soma_demo.cpp)
target_link_libraries(soma_demo PRIVATE khora::soma)

add_executable(reverie_demo reverie_demo.cpp)
target_link_libraries(reverie_demo PRIVATE khora::reverie)
