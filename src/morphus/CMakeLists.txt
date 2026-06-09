# The user-facing Khora runtime. (The demo paradigm has been retired;
# serious exercise of the substrate lives in src/crucible.)
add_executable(khora khora_main.cpp)
target_link_libraries(khora PRIVATE
    khora::carapace
    khora::reverie
    khora::lexicon
    khora::plexus
    khora::ligature
    khora::hand
    khora::cogitator
    khora::whetstone
    khora::reservoir
    khora::curator
    khora::lodestone
    khora::ballast
    khora::maelstrom
    khora::volition
)

# plexus_forge — fast standalone (re)builder of the associative graph, decoupled
# from the cortex training. Separate target; building it never relinks khora.exe.
add_executable(plexus_forge plexus_forge.cpp)
target_link_libraries(plexus_forge PRIVATE
    khora::reservoir
    khora::lexicon
    khora::plexus
    khora::ligature
)
