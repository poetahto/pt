cmake_minimum_required(VERSION 3.20)
project(ptb)

add_compile_options(-g -Wall -Og)
add_link_options(-g -Wall -Og)

add_subdirectory(fast_float)
add_executable(ptb_demo_fast_float ptb_demo_fast_float.cpp)
target_link_libraries(ptb_demo_fast_float PRIVATE fast_float)

add_executable(ptb_demo ptb_demo.c)
