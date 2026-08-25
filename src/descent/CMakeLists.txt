add_library(khora_descent STATIC
    descent.cpp
)

target_include_directories(khora_descent
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::descent ALIAS khora_descent)
