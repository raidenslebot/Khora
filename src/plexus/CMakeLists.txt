add_library(khora_plexus STATIC
    plexus.cpp
)

target_include_directories(khora_plexus
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

# The Plexus is pure standard C++ — no substrate, no GPU, no dependency.
add_library(khora::plexus ALIAS khora_plexus)
