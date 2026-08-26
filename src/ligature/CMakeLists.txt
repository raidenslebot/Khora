add_library(khora_ligature STATIC
    ligature.cpp
)

target_include_directories(khora_ligature
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_ligature PUBLIC khora::taxis)

# Pure standard C++ — the structured-relation layer over the corpus.
add_library(khora::ligature ALIAS khora_ligature)
