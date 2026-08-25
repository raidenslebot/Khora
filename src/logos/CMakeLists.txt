add_library(khora_logos STATIC
    logos.cpp
)

target_include_directories(khora_logos
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::logos ALIAS khora_logos)
