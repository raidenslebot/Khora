add_library(khora_curator STATIC
    curator.cpp
    curator_scheduler.cpp
)

target_include_directories(khora_curator
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_curator
    PUBLIC khora::reservoir khora::lexicon khora::cortex
)

add_library(khora::curator ALIAS khora_curator)
