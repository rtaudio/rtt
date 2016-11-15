cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 14)
add_library (rtt rtt.cpp)
target_include_directories (rtt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})