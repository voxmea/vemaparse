
cmake_minimum_required(VERSION 2.8)

include_directories(${CMAKE_SOURCE_DIR}/../include)
include_directories(c:/workspace/boost/1.54.0/include)

add_executable(vematest ${CMAKE_SOURCE_DIR}/vematest.cpp)
