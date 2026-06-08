# Khora build options and shared compile flags.

option(KHORA_BUILD_TESTS "Build tests"   ON)
option(KHORA_BUILD_BENCH "Build benches" ON)
option(KHORA_USE_AVX2    "Use AVX2 instruction set" ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    if(KHORA_USE_AVX2)
        add_compile_options(/arch:AVX2)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options($<$<CONFIG:Release>:-O3>)
    if(KHORA_USE_AVX2)
        add_compile_options(-mavx2 -mpopcnt -mbmi2)
    endif()
endif()
