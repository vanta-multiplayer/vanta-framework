cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(VantaMP CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

include_directories(${PROJECT_SOURCE_DIR}/code/framework/src/)

include(vendors/plugin-sdk.cmake)

add_subdirectory(code)
