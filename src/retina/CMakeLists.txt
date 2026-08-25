add_library(khora_retina STATIC
    retina.cpp
)

target_include_directories(khora_retina
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_retina
    PUBLIC khora::lattice
)

add_library(khora::retina ALIAS khora_retina)
