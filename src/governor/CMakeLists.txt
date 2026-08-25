add_library(khora_governor STATIC
    governor.cpp
)

target_include_directories(khora_governor
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(khora_governor PUBLIC pdh)
endif()

add_library(khora::governor ALIAS khora_governor)
