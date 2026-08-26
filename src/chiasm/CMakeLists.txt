add_library(khora_chiasm STATIC
    chiasm.cpp
)

target_include_directories(khora_chiasm
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_chiasm PUBLIC khora::lattice)

add_library(khora::chiasm ALIAS khora_chiasm)
