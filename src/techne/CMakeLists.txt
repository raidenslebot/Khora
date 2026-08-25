add_library(khora_techne STATIC
    techne.cpp
    emit.cpp
    solve.cpp
    bidir.cpp
)

target_include_directories(khora_techne
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_techne PUBLIC khora_governor)

add_library(khora::techne ALIAS khora_techne)
