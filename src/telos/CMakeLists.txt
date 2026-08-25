add_library(khora_telos STATIC
    telos.cpp
)

target_include_directories(khora_telos
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::telos ALIAS khora_telos)
