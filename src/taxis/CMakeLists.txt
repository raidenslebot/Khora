add_library(khora_taxis STATIC
    taxis.cpp
)

target_include_directories(khora_taxis
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::taxis ALIAS khora_taxis)
