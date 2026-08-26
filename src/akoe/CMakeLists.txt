add_library(khora_akoe STATIC
    akoe.cpp
)

target_include_directories(khora_akoe
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_akoe PUBLIC khora::lattice)

add_library(khora::akoe ALIAS khora_akoe)
