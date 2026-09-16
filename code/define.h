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

// 文件名的大小为12字节
const int FILE_NAME_LENGTH = 12;

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
    4   fcreat  新建文件         1参
    5   fopen   打开文件         1参
    6   fclose  关闭文件         1参
    7   fread   读文件           3参
    8   fwrite  写文件           3参
    9   flseek  定位文件读写指针  1参
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