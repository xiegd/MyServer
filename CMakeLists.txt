cmake_minimum_required(VERSION 3.10)

project(MyServer)

set(CMAKE_CXX_STANDARD 11)
set(VERSION 2.9)

option(USE_OP;"is use op";OFF)
configure_file(./sources/testheader.h ./sources/testheader.h)
# set(CMAKE_CXX_STANDARD_REQUIRED true)

message("extra libs: ${EXTRA_LIBS}")
message("extra includes: ${EXTRA_INCLUDES}")
message("project source dir: ${PROJECT_SOURCE_DIR}")
message("project binary dir: ${PROJECT_BINARY_DIR}")

# 定义一个列表
set(MyList "apple" "banana" "cherry")

# 获取列表的长度
list(LENGTH MyList ListLength)
message("Length of MyList: ${ListLength}")

# 获取列表中索引为 1 的元素
list(GET MyList 1 SecondElement)
message("Second element of MyList: ${SecondElement}")

# 在列表末尾添加一个元素
list(APPEND MyList "dates")

# 输出修改后的列表
message("MyList after appending: ${MyList}")

message("cmake module path: ${CMAKE_MODULE_PATH}")

message("cmake current source dir: ${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})
message("cmake module path: ${CMAKE_MODULE_PATH}")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
message("cmake module path: ${CMAKE_MODULE_PATH}")


add_executable(test test.cpp)

target_include_directories(test PUBLIC ./sources)
target_include_directories(test PUBLIC .)

# target_link_directories(test PUBLIC ${PROJECT_SOURCE_DIR})
