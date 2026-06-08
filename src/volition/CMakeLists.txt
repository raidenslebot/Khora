add_library(khora_volition STATIC
    volition.cpp
)

target_include_directories(khora_volition
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_volition PUBLIC khora::soma)

add_library(khora::volition ALIAS khora_volition)
