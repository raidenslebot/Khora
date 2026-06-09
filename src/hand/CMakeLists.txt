add_library(khora_hand STATIC
    hand.cpp
)

target_include_directories(khora_hand
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

# The Hand — Khora's effector. Win32 process execution; the faculty of ACTION.
add_library(khora::hand ALIAS khora_hand)
