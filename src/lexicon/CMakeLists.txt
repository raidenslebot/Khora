add_library(khora_lexicon STATIC
    lexicon.cpp
)

target_include_directories(khora_lexicon
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_lexicon PUBLIC khora::lattice)

add_library(khora::lexicon ALIAS khora_lexicon)
