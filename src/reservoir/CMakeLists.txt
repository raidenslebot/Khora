add_library(khora_reservoir STATIC
    codec.cpp
    distill.cpp
    reservoir.cpp
    aqueduct.cpp
)

target_include_directories(khora_reservoir
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(khora_reservoir PUBLIC winhttp)
endif()

add_library(khora::reservoir ALIAS khora_reservoir)
