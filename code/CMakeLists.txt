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

# 单元测试目标
# 自带 main(), 因此排除 main.cpp; test.cpp 是演示程序的手工调试块, 同样排除
add_executable(
    unittest
    unittest/main.cpp
    unittest/test_superblock.cpp
    unittest/test_inode.cpp
    unittest/test_buffer.cpp
    unittest/test_fileapi.cpp
    unittest/test_boundary.cpp
    shell.cpp
    filedir.cpp
    format.cpp
    inode.cpp
    superblock.cpp
    buffer.cpp
    openfile.cpp
)

target_link_libraries(unittest wirteDisk)