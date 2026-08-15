add_library(khora_crystallize STATIC
    crystallize.cpp
)

target_include_directories(khora_crystallize
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_crystallize
    PUBLIC khora::plexus khora::ligature
)

add_library(khora::crystallize ALIAS khora_crystallize)
