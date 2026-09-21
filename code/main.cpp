#include <iostream>
#include <cstdlib>
#include "./wirteDisk/wirteDisk.h"
#include "shell.h"
#include "define.h"
#include "format.h"
#include "superblock.h"
#include "test.h"
#include "buffer.h"
#include "openfile.h"

using namespace std;

int main()
{
    // 打开文件, 模拟加载磁盘
    DiskFile file;
    if (!file.open(disk_name.c_str()))
    {
        cout << "文件没有正确打开!" << endl;
        exit(-1);
    }

    // 在内存中建立缓存队列
    BufferMgr buffer_manager;

    // 内存superblock结构
    SuperBlock sblk;

    // 建立内存inode表
    MemInodeTable i_table;

    // 建立系统打开文件表
    OpenFileTable f_table;

    // 如果是第一次加载文件系统, 进行格式化
    //diskFormat(file, sblk, i_table, f_table, buffer_manager);

    test();
    //sblk.print();

    // 用户进行操作
    Shell shell;
    shell.usr(file, sblk, i_table, f_table, buffer_manager);

    //sblk.print();

    // 退出文件系统, 关闭磁盘
    file.close();
    return 0;
}