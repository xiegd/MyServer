cmake_minimum_required(VERSION 3.10)

project(MyServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# include_directories(./sources)

# aux_source_directory(./sources SRC_SYLAR)

# set(SRC_SYLAR)
# list(APPEND SRC_SYLAR ../sources/log.cc)
aux_source_directory(./src MY_CODE)

add_executable(MyServer ${MY_CODE})