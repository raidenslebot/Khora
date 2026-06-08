add_executable(lattice_test lattice_test.cpp)
target_link_libraries(lattice_test PRIVATE khora::lattice)
add_test(NAME LatticeTest COMMAND lattice_test)
