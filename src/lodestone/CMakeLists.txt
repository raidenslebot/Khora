add_library(khora_lodestone STATIC
    lodestone.cpp
)

target_include_directories(khora_lodestone
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_lodestone PUBLIC khora::lattice)

add_library(khora::lodestone ALIAS khora_lodestone)
