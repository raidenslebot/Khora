add_library(khora_whetstone STATIC
    whetstone.cpp
    whetstone_scheduler.cpp
)

target_include_directories(khora_whetstone
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_whetstone
    PUBLIC khora::lattice khora::cortex khora::crucible
)

add_library(khora::whetstone ALIAS khora_whetstone)

# The autonomous self-training runner.
add_executable(whetstone whetstone_main.cpp)
target_link_libraries(whetstone PRIVATE khora::whetstone)
