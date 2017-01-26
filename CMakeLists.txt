cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 14)

find_library(THREAD_LIB NAMES pthread )


add_library (rtt rtt.cpp)
target_include_directories (rtt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (rtt ${THREAD_LIB})

#debug
#add_definitions("-g -ggdb")
