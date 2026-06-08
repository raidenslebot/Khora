add_library(khora_ballast STATIC
    ballast.cpp
)

target_include_directories(khora_ballast
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(khora_ballast PUBLIC psapi)
endif()

add_library(khora::ballast ALIAS khora_ballast)
