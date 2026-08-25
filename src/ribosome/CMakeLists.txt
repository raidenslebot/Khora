add_library(khora_ribosome STATIC
    ribosome.cpp
)

target_include_directories(khora_ribosome
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_ribosome PUBLIC khora::lattice)

add_library(khora::ribosome ALIAS khora_ribosome)
