add_library(khora_cortex STATIC
    predictive_column.cpp
    temporal_memory.cpp
    context_tree.cpp
)

target_include_directories(khora_cortex
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_cortex PUBLIC khora::lattice)

add_library(khora::cortex ALIAS khora_cortex)
