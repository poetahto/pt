cmake_minimum_required(VERSION 3.20)
project(poetools)

add_compile_options(-g -Wall -Og)
add_link_options(-g -Wall -Og)

add_executable(pt_map_demo pt_map_demo.c)
