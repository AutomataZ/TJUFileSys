#pragma once
#include "define.h"

// superblock占据磁盘的前2块, 也就是1024字节

// 这个类就是盘上那 1024 字节的布局, 不能加成员: sizeof(SuperBlock) 被若干处
// 序列化直接当作长度用。缓存管理器因此只能按参数传进来。
class BufferMgr;
class DiskFile;

class SuperBlock{
protected:
    // 对空闲inode的管理
    // 本课设中, 初始状态下只有一个 inode 被占用, 因此直接管理的空闲 inode 数是 100
    int s_isize; // inode区占用的块数
    int s_ninode; // superblock直接管理的空闲inode个数 不超过100
    int s_inode[SUPER_CONTROL_INODE_NUM]; // superblock直接管理的空闲inode的索引表

    // 对空闲数据区的管理
    // 在本课设中, 我们规定磁盘大小为 4M, 因此只有 8000 个文件数据块
    // 在初始状态下, 这些数据块全都可以被串到 s_free 中
    int s_fsize; // 数据区盘块的总数
    int s_nfree; // superblock直接管理的空闲数据块个数 不超过100
    int s_free[SUPER_CONTROL_FILE_NUM]; // super直接管理的空闲数据块索引表

    // 以上数据结构占据816字节 还差208字节
    char load[208];

public:
    // 格式化的 superblock 信息
    SuperBlock();

    // 从模拟磁盘的文件中初始化 superblock 的信息
    SuperBlock(DiskFile& disk);

    // 分配 inode
    int distributeInode(DiskFile& disk);

    // 释放 inode
    void releaseInode(int index);

    // 分配盘块
    /// @param b_mgr 盘块易主, 需要作废缓存里该块的旧副本
    int distributeBlk(DiskFile& disk, BufferMgr& b_mgr);

    // 释放盘块
    /// @param b_mgr 盘块易主, 需要作废缓存里该块的旧副本
    void releaseBlk(DiskFile& disk, int index, BufferMgr& b_mgr);

    // 将修改过的 superblock 信息存盘
    void save(DiskFile& file);

    // 将空闲盘块串起来
    // ps.写的不好, 考虑重写
    void FormatFreeBlk(DiskFile& disk);

    /// @brief 输出superblock的内部信息
    void print();

};