add_library(khora_crucible STATIC
    crucible.cpp
)

target_include_directories(khora_crucible
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_crucible PUBLIC khora::lattice)

add_library(khora::crucible ALIAS khora_crucible)

# The forge runner — a serious training harness, not a demo.
add_executable(crucible crucible_main.cpp)
target_link_libraries(crucible PRIVATE khora::crucible)
