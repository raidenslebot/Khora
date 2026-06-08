add_library(khora_reverie STATIC
    reverie_loom.cpp
)

target_include_directories(khora_reverie
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_reverie
    PUBLIC khora::lattice khora::cortex khora::soma
)

add_library(khora::reverie ALIAS khora_reverie)
