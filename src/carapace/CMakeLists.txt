add_library(khora_carapace STATIC
    carapace.cpp
    builtin_tools.cpp
    techne_tools.cpp
    reason_tools.cpp
    organ_tools.cpp
)

target_include_directories(khora_carapace
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_carapace
    PUBLIC khora::lattice khora::cortex khora::soma khora::lexicon khora::cogitator
           khora::techne khora::logos khora::ligature khora::chiasm
           khora::ribosome khora::synapse khora::governor
)

add_library(khora::carapace ALIAS khora_carapace)
