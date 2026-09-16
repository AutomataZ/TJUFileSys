# CMake的版本
cmake_minimum_required(VERSION 3.16)
# 项目名称
project(misakifs)

# 添加头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/
)

# 添加库
add_library(wirteDisk STATIC wirteDisk/wirteDisk.cpp wirteDisk/wirteDisk.h)

# 添加main目标
add_executable(
    main 
    main.cpp 
    shell.cpp
    filedir.cpp
    format.cpp
    inode.cpp
    superblock.cpp
    buffer.cpp
    openfile.cpp
    test.cpp
)

# 链接库
target_link_libraries(main wirteDisk)