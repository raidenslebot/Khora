add_library(khora_synapse STATIC
    synapse_bus.cpp
)

target_include_directories(khora_synapse
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_synapse PUBLIC khora::lattice)

add_library(khora::synapse ALIAS khora_synapse)
