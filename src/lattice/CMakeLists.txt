add_library(khora_lattice STATIC
    glyph.cpp
    sdr.cpp
    lattice.cpp
    persistence.cpp
)

target_include_directories(khora_lattice
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

add_library(khora::lattice ALIAS khora_lattice)
