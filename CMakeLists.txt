cmake_minimum_required(VERSION 3.20)
project(poetools)

add_compile_options(-g -Wall -Og)
add_link_options(-g -Wall -Og)

add_subdirectory(fast_float)
add_executable(pt_map_demo_ff pt_map_demo_ff.cpp)
target_link_libraries(pt_map_demo_ff PRIVATE fast_float)

add_executable(pt_map_demo pt_map_demo.c)
