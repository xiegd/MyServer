# 设置允许CMakeLists.txt所需的最低CMake版本
cmake_minimum_required(VERSION 3.14)

# 设置项目名称
project(my_project)

# 设置GoogleTest所需的最低C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

# 包含CMake的FetchContent模块，用于在配置时下载和配置外部项目
include(FetchContent)
# 声明一个外部项目，名称为googletest，URL为GoogleTest的GitHub仓库
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# 实际的下载和配置FetchContent_Declare的内容，如果gtest没有下载则进行下载
FetchContent_MakeAvailable(googletest)

set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR}/../../src)
set(HEADER_DIR ${CMAKE_SOURCE_DIR})
set(INCLUDE_DIRS "")
list(APPEND INCLUDE_DIRS 
  ${SOURCE_DIR}/thread 
  ${SOURCE_DIR}/network 
  ${SOURCE_DIR}/util
  ${SOURCE_DIR}/poller
)

file(GLOB UTIL_SRCS ${SOURCE_DIR}/util/*.cc)
file(GLOB POLLE_SRCS ${SOURCE_DIR}/poller/*.cc)
file(GLOB THREAD_SRCS ${SOURCE_DIR}/thread/*.cc)
file(GLOB NETWORK_SRCS ${SOURCE_DIR}/network/*.cc)

file(GLOB UTIL_HEAD ${SOURCE_DIR}/util/*.h)
file(GLOB POLLE_HEAD ${SOURCE_DIR}/poller/*.h)
file(GLOB THREAD_HEAD ${SOURCE_DIR}/thread/*.h)
file(GLOB NETWORK_HEAD ${SOURCE_DIR}/network/*.h)

add_executable(eventpoller_test eventpoller_test.cc 
              ${UTIL_SRCS}
              ${POLLE_SRCS}
              ${THREAD_SRCS}
              ${NETWORK_SRCS}
              )

target_include_directories(eventpoller_test 
  PRIVATE
    ${INCLUDE_DIRS}
)

target_link_libraries(eventpoller_test gtest_main)

set_target_properties(eventpoller_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}) 