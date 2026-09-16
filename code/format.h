// 在第一次使用文件系统时，需要对整个磁盘进行格式化
#pragma once
#include <fstream>
#include "buffer.h"
#include "openfile.h"
#include "define.h"

/*
    格式化的内容为
    一个 superblock 区
    一个 inode 区, 存在 1 个 inode, 对应一个根目录文件
    文件打开结构的格式化
    新建一个根目录文件 / 
    写完磁盘要打开根目录文件 / 
*/
void diskFormat(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);