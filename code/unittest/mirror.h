#pragma once
#include "define.h"
#include "superblock.h"
#include "inode.h"
#include "wirteDisk/wirteDisk.h"

// SuperBlock 与 Inode 的关键字段都是 protected, 测试无法直接读取。
// 这里定义布局完全相同的 POD 镜像结构, 借 readDisk/writeDisk 按字节搬运:
//
//   读: writeDisk(disk, &obj, sizeof(...), off) 把内存对象快照落盘,
//       再 readDisk 进镜像结构 --> 得到字段值
//   写: 反向操作 --> 可以直接在磁盘上摆出任意初始状态
//
// 后者是测试成组链接法最后一组的关键: 无需真的分配 8000 个盘块,
// 只要在磁盘上构造出"当前直接管理块数恰为 1 且指向最后一个索引块"的状态,
// 再调一次 distributeBlk 就能走到那条罕见分支。

struct SuperBlockMirror {
    int s_isize;
    int s_ninode;
    int s_inode[SUPER_CONTROL_INODE_NUM];
    int s_fsize;
    int s_nfree;
    int s_free[SUPER_CONTROL_FILE_NUM];
    // 4 个标量 int + 两张表 = 816 字节, superblock 共 1024 字节, 余下 208 为填充
    char load[SUPER_BLOCK_NUM * BYTE_PER_BLOCK - 4 * sizeof(int)
              - (SUPER_CONTROL_INODE_NUM + SUPER_CONTROL_FILE_NUM) * sizeof(int)];
};

struct InodeMirror {
    int d_mode;
    int d_size;
    int d_addr[10];
    char load[16];
};

// sizeof 必须与真实结构一致, 否则字节搬运会错位
static_assert(sizeof(SuperBlockMirror) == 1024, "SuperBlockMirror 布局须与 SuperBlock 一致");
static_assert(sizeof(InodeMirror) == INODE_SIZE, "InodeMirror 布局须与 Inode 一致");

inline int inodeOffset(int inode_index)
{
    return INODE_AREA_OFFSET + inode_index * INODE_SIZE;
}

// 把内存 superblock 快照落盘后读回镜像
inline void snapshotSuperBlock(DiskFile& disk, SuperBlock& sblk, SuperBlockMirror& m)
{
    writeDisk(disk, &sblk, sizeof(SuperBlock), 0);
    readDisk(disk, &m, sizeof(SuperBlockMirror), 0);
}

// 用镜像内容覆盖磁盘 superblock, 再载入内存 SuperBlock
inline void loadSuperBlock(DiskFile& disk, SuperBlockMirror& m, SuperBlock& sblk)
{
    writeDisk(disk, &m, sizeof(SuperBlockMirror), 0);
    readDisk(disk, &sblk, sizeof(SuperBlock), 0);
}

// 把内存 inode 快照落盘后读回镜像
inline void snapshotInode(DiskFile& disk, Inode& inode, int inode_index, InodeMirror& m)
{
    writeDisk(disk, &inode, sizeof(Inode), inodeOffset(inode_index));
    readDisk(disk, &m, sizeof(InodeMirror), inodeOffset(inode_index));
}

// 直接读取磁盘上某个 inode 的镜像
inline void readInodeMirror(DiskFile& disk, int inode_index, InodeMirror& m)
{
    readDisk(disk, &m, sizeof(InodeMirror), inodeOffset(inode_index));
}
