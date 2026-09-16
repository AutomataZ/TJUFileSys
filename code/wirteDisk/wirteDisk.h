#pragma once
#include <fstream>

/// @brief 读磁盘
/// @param disk 文件流，对应读入文件
/// @param buffer 读入到buffer中
/// @param size 读入的字节数
/// @param offset 相对于文件开头的偏移量
void readDisk(std::fstream& disk, void* buffer, int size, int offset);

/// @brief 写磁盘
/// @param disk 文件流，对应写入文件
/// @param buffer 写入的内容
/// @param size 写入的大小，以字节为单位
/// @param offset 相对于文件开头的偏移量
void writeDisk(std::fstream& disk, void* buffer, int size, int offset);