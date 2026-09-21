#pragma once
#include <string>

// 定义调试环境
#ifndef DEBUG_ENV
#define DEBUG_ENV
#endif

#ifdef DEBUG_ENV
//#define DEBUG_BQUEUE
//#define DEBUG_BUFFER
#define FINAL_TEST
#define DEBUG_SUPERBLOCK
const int MAX_BQUEUE_SIZE = 5; // 缓存队列最大长度
#else
const int MAX_BQUEUE_SIZE = 100; // 缓存队列最大长度
#endif

/* 
    磁盘文件大小人为规定为 4M 
    在 512 字节为 1 块的前提下
    共有 8192 个块
    superblock 占据最开始的 2 块
    inode 约定占据 190 块
    剩余 8000 个块用于文件数据区
*/

const int DISK_SIZE = 4 * 1024 * 1024;
const int BYTE_PER_BLOCK = 512;
const int TOTAL_BLOCK_NUM = DISK_SIZE / BYTE_PER_BLOCK;
const int SUPER_BLOCK_NUM = 2;
const int INODE_BLOCK_NUM = 190;
const int FILE_BLOCK_NUM = TOTAL_BLOCK_NUM - SUPER_BLOCK_NUM - INODE_BLOCK_NUM;

// 约定单个 inode 大小为 64 字节
const int INODE_SIZE = 64;

const int DIRECT_INDEX_NUM = 6;
const int FIRST_LEVEL_INDIRECT_INDEX_NUM = DIRECT_INDEX_NUM + 2 * BYTE_PER_BLOCK / sizeof(int);
const int SECOND_LEVEL_INDIRECT_INDEX_NUM = FIRST_LEVEL_INDIRECT_INDEX_NUM + 2 * BYTE_PER_BLOCK / sizeof(int) * BYTE_PER_BLOCK / sizeof(int);

// superblock 直接管理的数量
const int SUPER_CONTROL_INODE_NUM = 100;
const int SUPER_CONTROL_FILE_NUM = 100;

// 一组空闲盘块最多 100 个
const int BLOCK_IN_GROUP = 100;

const int INODE_IS_OCCUPIED = -1;
const int INODE_NUM = INODE_BLOCK_NUM * BYTE_PER_BLOCK / INODE_SIZE;
const int INODE_AREA_OFFSET = SUPER_BLOCK_NUM * BYTE_PER_BLOCK;

const int FILE_BLOCK_IS_OCCUPIED = -1;
const int FILE_BLOCK_START = SUPER_BLOCK_NUM + INODE_BLOCK_NUM;
const int FILE_AREA_OFFSET = INODE_AREA_OFFSET + INODE_NUM * INODE_SIZE;

/*
    块号一律是「绝对块号」, 换算成字节偏移就是 blkno * BYTE_PER_BLOCK 这一个式子。

    成组链接法里的空闲块号、inode 里 d_addr 存的块号、分配/释放时传递的块号,
    全都是这种从 0 起算的绝对块号, 第 192 块就是文件数据区的第一块。

    FILE_AREA_OFFSET(= 192 * BYTE_PER_BLOCK)是数据区的起始字节偏移, 它描述的是
    「数据区从哪开始」, 不是一个要加到块号上去的基址 —— 块号本身已经含了这段。
    可以自检: 最后一块 8191 写成 8191 * BYTE_PER_BLOCK + BYTE_PER_BLOCK 正好等于
    DISK_SIZE, 再加一次 FILE_AREA_OFFSET 就越出磁盘了。
*/

// 文件名的大小为12字节
const int FILE_NAME_LENGTH = 12;

// 每个文件的目录项(FileDir)占 16 字节: inode / fa_inode 各 2 字节 + 12 字节文件名
const int FILE_DIR_SIZE = 2 * sizeof(int16_t) + FILE_NAME_LENGTH;

/*
    字节数与盘块数的换算, 全系统只有下面这一个式子。

    一个文件的内容从它首块的块内偏移 FILE_DIR_SIZE(16) 处开始 —— 块首那 16 字节
    是它自己的目录项。所以 N 字节内容要占

        N == 0 ? 0 : ceil((N + 16) / 512)

    个盘块: 首块只装得下 512 - 16 = 496 字节, 其后每块 512 字节。反过来, 占 B 个
    盘块的文件最多装得下 B * 512 - 16 字节。

    下面四个调用点必须用同一个式子 —— 它们对"一个文件占几块"的理解必须一致,
    否则 d_size 记的字节数会与分到的容量对不上:
      newFile       按它分配盘块
      appendBlk     按它挑 d_addr 的槽位
      releaseAllBlk 按它回收盘块
      readWriteFile 按它算一次读写要跨几块

    blocksSpanned 是从"块内偏移 + 字节数"问跨几块, 供读写循环用;
    读写循环的起点不一定是块首 (flseek 可以停在任意位置)。
*/
inline int blocksSpanned(int start_cur, int byte_count)
{
    if (byte_count <= 0)
        return 0;
    return (start_cur + byte_count - 1) / BYTE_PER_BLOCK + 1;
}

// 一个文件装下 content_bytes 字节内容需要几个盘块 (起点固定是首块的 FILE_DIR_SIZE 处)
inline int blocksForFileContent(int content_bytes)
{
    return blocksSpanned(FILE_DIR_SIZE, content_bytes);
}

// 内存inode表的大小为100
const int MEM_INODE_NUM = 100;
// 系统打开文件表的大小是100
const int OPEN_FILE_TABLE_SIZE = 100;

const std::string disk_name = "../../myDisk.img";

/*
    0   fformat 格式化文件卷      0参
    1   ls      列目录           0参
    2   mkdir   创建目录         1参
    3   cd      切换当前目录     1参
    4   fcreat  新建文件         2参
    5   fopen   打开文件         1参
    6   fclose  关闭文件         1参
    7   fread   读文件           2参
    8   fwrite  写文件           3参
    9   flseek  定位文件读写指针  2参
    10  fdelete 删除文件         1参
    11  exit    退出文件系统     0参

    以下为调试命令, 用于观察文件系统内部结构
    12  sb      打印superblock   0参
    13  cache   打印缓存队列     0参
    14  imem    打印内存inode表  0参
    15  ftab    打印打开文件表   0参
*/
const std::string cmd_supported[] = {
    "fformat",
    "ls",
    "mkdir",
    "cd",
    "fcreat",
    "fopen",
    "fclose",
    "fread",
    "fwrite",
    "flseek",
    "fdelete",
    "exit",
    "sb",
    "cache",
    "imem",
    "ftab"
};

// 最大参数个数
const int MAX_ARGS_NUM = 3;