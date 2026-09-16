#pragma once
#include <cstdlib>
#include <string>
#include <fstream>
#include "openfile.h"

// inode 节点最大为190 2字节可以表示

/*
    文件的目录项
    共占 16 字节
    前 2 字节是文件对应的 inode 节点
    后 14 字节是文件名
    目录项之后的空间就是文件的内容
    对于目录文件来说，目录项之后的空间储存的是当前目录下的子文件的inode节点
*/
class FileDir {
protected:
    int16_t inode; // 对应的 inode 节点
    int16_t fa_inode; // 父目录对应的 inode 节点
    char f_name[FILE_NAME_LENGTH]; // 文件名
public:
    FileDir() {}
    FileDir(std::string filename, int16_t inode, int16_t fa_inode);
    /// @brief 在磁盘的物理块上写入文件目录项
    /// @param disk 要写入的磁盘
    /// @param blkno 要写入的物理块号
    void create(std::fstream& disk, int blkno);
    
    /// @brief 将该文件加入系统打开结构中
    /// @param i_table 内存inode表
    /// @param f_table 系统打开文件表
    void open(std::fstream& disk, MemInodeTable& i_table, OpenFileTable& f_table);

    /// @brief 将该文件从系统打开结构中去除
    /// @param disk 
    /// @param i_table 内存inode表
    /// @param f_table 系统打开文件表
    void close(std::fstream& disk, MemInodeTable& i_table, OpenFileTable& f_table);

    bool is_root() { return inode == fa_inode; }

    bool is_open(MemInodeTable& i_table, OpenFileTable& f_table);

    int getInode() { return inode; }

    int getFaInode() { return fa_inode; }

    char* getFileName() { return f_name; }
};