cmake_minimum_required(VERSION 3.14)

project(test)

message("project name: ${PROJECT_NAME}")
message("project source dir: ${PROJECT_SOURCE_DIR}")
message("project binary dir: ${PROJECT_BINARY_DIR}")
message("cmake source dir: ${CMAKE_SOURCE_DIR}")
message("cmake binary dir: ${CMAKE_BINARY_DIR}")
message("project current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
message("project current binary dir: ${CMAKE_CURRENT_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

find_package(OpenSSL REQUIRED)

