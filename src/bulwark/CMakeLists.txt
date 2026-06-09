add_library(khora_bulwark STATIC
    bulwark.cpp
)

target_include_directories(khora_bulwark
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(khora_bulwark PUBLIC advapi32)
endif()

# The Bulwark — Khora's containment cage for autonomous action (Job Object + low-IL
# non-admin token + cell + disk floor, all fail-closed).
add_library(khora::bulwark ALIAS khora_bulwark)
