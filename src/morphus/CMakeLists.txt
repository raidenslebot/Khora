# The user-facing Khora runtime. (The demo paradigm has been retired;
# serious exercise of the substrate lives in src/crucible.)
add_executable(khora khora_main.cpp)
target_link_libraries(khora PRIVATE
    khora::carapace
    khora::reverie
    khora::lexicon
    khora::plexus
    khora::ligature
    khora::crystallize
    khora::hand
    khora::bulwark
    khora::maw
    khora::cogitator
    khora::whetstone
    khora::reservoir
    khora::curator
    khora::lodestone
    khora::ballast
    khora::maelstrom
    khora::volition
)

# khora_next — the SUCCESSOR target for `ascend` (binary self-replacement). A running
# khora.exe holds its own image locked, so it cannot relink itself; this is the SAME
# source built to a distinct binary that the running instance promotes over itself via
# a detached relauncher (with a known-good backup + rollback). Mirrors khora exactly.
add_executable(khora_next khora_main.cpp)
target_link_libraries(khora_next PRIVATE
    khora::carapace
    khora::reverie
    khora::lexicon
    khora::plexus
    khora::ligature
    khora::crystallize
    khora::hand
    khora::bulwark
    khora::maw
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

# bulwark_probe — proves the containment cage in isolation (red-team requirement:
# containment must be proven before any autonomous driver can use it).
add_executable(bulwark_probe bulwark_probe.cpp)
target_link_libraries(bulwark_probe PRIVATE khora::bulwark)

# reforge_eval — successor-evaluator for the self-rewriting loop. A SEPARATE binary
# (khora.exe locks itself while running, so it cannot relink itself): Khora rewrites
# a constant in its own source, rebuilds THIS, and measures the candidate's yield.
add_executable(reforge_eval reforge_eval.cpp)
target_link_libraries(reforge_eval PRIVATE
    khora::cogitator
    khora::lexicon
    khora::plexus
)
