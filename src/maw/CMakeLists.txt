add_library(khora_maw STATIC
    maw.cpp
)

target_include_directories(khora_maw
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

# The Maw — Khora's chaos exploration drive. Pure standard C++; it runs commands
# only through khora::bulwark (wired at the morphus layer), never the raw Hand.
add_library(khora::maw ALIAS khora_maw)
