add_library(khora_cogitator STATIC
    cogitator.cpp
)

target_include_directories(khora_cogitator
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_cogitator
    PUBLIC khora::lattice khora::lexicon khora::cortex khora::soma khora::maelstrom
)

add_library(khora::cogitator ALIAS khora_cogitator)
