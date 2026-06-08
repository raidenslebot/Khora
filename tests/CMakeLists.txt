add_executable(lattice_test lattice_test.cpp)
target_link_libraries(lattice_test PRIVATE khora::lattice)
add_test(NAME LatticeTest COMMAND lattice_test)

add_executable(persistence_test persistence_test.cpp)
target_link_libraries(persistence_test PRIVATE khora::lattice)
add_test(NAME PersistenceTest COMMAND persistence_test)
