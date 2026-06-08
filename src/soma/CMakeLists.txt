add_library(khora_soma STATIC
    soma_nexus.cpp
)

target_include_directories(khora_soma
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::soma ALIAS khora_soma)
