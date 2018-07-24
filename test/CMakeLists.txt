find_package(Threads)

add_executable(rx-test test.cpp)
target_link_libraries(rx-test rx-test-lib doctest RxCpp ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(rx-test PRIVATE cxx_std_17)

