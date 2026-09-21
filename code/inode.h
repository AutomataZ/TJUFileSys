#pragma once
#include "define.h"
#include "superblock.h"
class DiskFile;
#include <cstring>

// inode占据磁盘的 2 ~ 191 块, 也就是 190 个盘块
// 只考虑单进程, 单设备, 单用户的情况下, 对 inode 的结构做一定的简化
// 同时, 控制单个 inode 为 64 字节, 使得一个盘块(512字节)中正好含有 8 个 inode

// 区分是否是目录文件
enum FILE_MODE{ empty, normal_file, dir_file };

class Inode {
protected:
    int d_mode; // 文件的类型, 这里只区分是否是目录文件
    int d_size; // 文件内容的大小，以字节为单位；目录文件，每2个字节代表1个inode
    int d_addr[10]; // 文件索引表, 6-2-2方式
    char load[16]; // 保持 inode 大小为 64 字节
public:
    Inode(){
        d_mode = 0;
        d_size = 0;
        memset(d_addr, 0, sizeof(d_addr));
        memset(d_addr, -1, sizeof(d_addr));
        // load 是保持 inode 大小为 64 字节的填充字段, 不参与逻辑,
        // 但会随 sizeof(Inode) 整体落盘, 不初始化会把栈上的残留数据写进磁盘
        memset(load, 0, sizeof(load));
    }
    bool isEmpty();

    /// @brief 向inode中追加一个块
    /// @param disk 需要写入的磁盘文件
    /// @param s 内存superblock，可能需要分配索引块
    /// @param inode_index 向这个inode中追加
    /// @param blkno 关联的块号
    /// @param b_mgr 分配索引块时盘块易主, 需要作废缓存里该块的旧副本
    void appendBlk(DiskFile&disk, SuperBlock& s, int inode_index, int blkno, BufferMgr& b_mgr);

    /// @brief 释放inode之前，释放该inode关联的所有盘块
    /// @param disk 需要写入的磁盘文件
    /// @param s 内存superblkock
    /// @param b_mgr 盘块易主, 需要作废缓存里这些块的旧副本
    void releaseAllBlk(DiskFile&disk, SuperBlock& s, BufferMgr& b_mgr);

    /// @brief 改变文件大小
    /// @param add 增加的大小
    void changeSize(int add){d_size += add;}

    /// @brief 将文件的逻辑块映射为物理块
    /// @param blkno 文件的逻辑块号
    /// @return 该逻辑块号对应于磁盘的哪个物理块
    int BMap(DiskFile& disk, int blkno);
    /// @brief 增加一个子inode，目录文件的尺寸做相应修改
    void addSubDirSize() { d_size += sizeof(int16_t); }
    /// @brief 删除一个子inode，目录文件的尺寸做相应修改
    void eraseSubDirSize() { d_size -= sizeof(int16_t); }
    int getMode() { return d_mode; }
    void setMode(int file_mode) { d_mode = file_mode; }
    int getSize() { return d_size; }

};