add_library(khora_techne STATIC
    techne.cpp
    emit.cpp
    solve.cpp
)

target_include_directories(khora_techne
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::techne ALIAS khora_techne)
