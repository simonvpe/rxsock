cmake_minimum_required(VERSION 3.0)
project(rx-test)

find_package(CURL REQUIRED)

add_subdirectory(doctest)
add_subdirectory(RxCpp)
add_subdirectory(src)
add_subdirectory(test)
enable_testing()
