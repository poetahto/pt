cmake_minimum_required(VERSION 3.20)
project(poetools)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(libs/SDL)
add_subdirectory(libs/glad)

add_executable(pt_map_demo pt_map_demo.c)

add_executable(pt_clip_demo pt_clip_demo.c)
target_link_libraries(pt_clip_demo PRIVATE SDL3::SDL3 glad)