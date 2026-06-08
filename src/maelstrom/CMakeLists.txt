add_library(khora_maelstrom STATIC
    maelstrom.cpp
)

target_include_directories(khora_maelstrom
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(khora_maelstrom PUBLIC khora::lattice)

if(WIN32)
    # DirectCompute backend. These ship with the Windows SDK / Windows itself
    # (d3dcompiler_47.dll is present on every Win10/11 box) — no toolkit, no
    # added runtime dependency for khora.exe.
    target_link_libraries(khora_maelstrom PUBLIC d3d11 d3dcompiler dxgi)
endif()

add_library(khora::maelstrom ALIAS khora_maelstrom)
