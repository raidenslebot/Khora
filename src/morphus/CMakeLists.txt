# The user-facing Khora runtime. (The demo paradigm has been retired;
# serious exercise of the substrate lives in src/crucible.)
add_executable(khora khora_main.cpp)
target_link_libraries(khora PRIVATE
    khora::carapace
    khora::reverie
    khora::lexicon
    khora::plexus
    khora::cogitator
    khora::whetstone
    khora::reservoir
    khora::curator
    khora::lodestone
    khora::ballast
    khora::maelstrom
    khora::volition
)
