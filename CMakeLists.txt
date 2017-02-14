cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 14)





add_library (rtt rtt.cpp stress.cpp)
target_include_directories (rtt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (UNIX)
	find_library(THREAD_LIB NAMES pthread )
	target_link_libraries (rtt ${THREAD_LIB})
endif (UNIX)	

#debug
#add_definitions("-g -ggdb")
