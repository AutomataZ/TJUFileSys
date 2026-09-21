#include <cstring>
#include "format.h"
#include "buffer.h"
#include "superblock.h"
#include "inode.h"
#include <filedir.h>
#include "wirteDisk/wirteDisk.h"

#ifdef DEBUG_ENV
#include <iostream>
    using namespace std;
#endif

void diskFormat(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    // 全盘填充 0 
    char FORMAT[DISK_SIZE];
    memset(FORMAT, 0, sizeof(FORMAT));
    writeDisk(disk, FORMAT, DISK_SIZE, 0);

    // 缓存队列是整个程序运行时都要用的, 应该在外部建立并进行初始化
    // 此处的写磁盘操作全都是直接操作磁盘

    // 创建一个格式化的 superblock 区
    SuperBlock s;
    sblk = s;

    //sblk.print();

    // 写入磁盘的前 1024 字节
    writeDisk(disk, &sblk, sizeof(SuperBlock), 0);

    // 格式化磁盘的文件数据区
    sblk.FormatFreeBlk(disk);

    // 分配一个 inode 出来, 分配到的应该是第 99 号 inode
    int inode_index = sblk.distributeInode(disk);

    // 分配一个空闲盘块出来
    int file_block_index = sblk.distributeBlk(disk, b_mgr);

    Inode inode;

    //sblk.print();

    // 将分配到的空闲盘块关联到分配到的inode上
    inode.appendBlk(disk, sblk, inode_index, file_block_index, b_mgr);
    inode.setMode(FILE_MODE::dir_file);

    // 将内存 inode 整体写入磁盘
    // appendBlk 只单独写了 d_addr, setMode 只改了内存对象, 此处必须落盘,
    // 否则 dir.open() 从磁盘重读 inode 时 d_mode 仍是 0
    writeDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));

    //cout << "文件的盘块是" << inode.BMap(disk, 0) << endl;

    // 在空闲盘块上新建一个根目录文件 / 
    FileDir dir("/", inode_index, inode_index);
    dir.create(disk, inode.BMap(disk, 0));

    // 修改内存inode和系统文件打开结构
    dir.open(disk, i_table, f_table);

    //i_table.print();
    //f_table.print();

    // 修改当前目录为 /
    i_table.modifyCurrentDir(i_table.find(inode_index));

    // 将superblock存盘
    writeDisk(disk, &sblk, sizeof(SuperBlock), 0);

    // 提交点: 格式化写完即落地。放在这里而不是 fformat 里, 是为了让直接调用
    // diskFormat 的路径(见 main.cpp 里注释掉的那处)也一并覆盖
    disk.sync();
}