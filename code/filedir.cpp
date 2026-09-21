#include "filedir.h"
#include "define.h"
#include "wirteDisk/wirteDisk.h"
#include <iostream>

FileDir::FileDir(std::string filename, int16_t inode, int16_t fa_inode)
{
    for (int i = 0; i < filename.length(); i++) 
        this->f_name[i] = filename[i];
    for (int i = filename.length(); i < FILE_NAME_LENGTH; i++)
        this->f_name[i] = '\0';
    this->inode = inode;
    this->fa_inode = fa_inode;
}

void FileDir::create(DiskFile& disk, int blkno)
{
    // 块号为负说明调用方没有给这个文件分配首块。writeDisk 只在 offset >= 0 时
    // 才 seekp, 负偏移换算出来的 -512 会让它退化成"从流的当前位置写"——通常是
    // 紧邻的另一个 inode, 16 字节目录项盖上去就把别人的 inode 整个砸掉, 而且
    // 当场不会有任何异常。与其静默损坏, 不如拦住并报出来。
    // 目录项必须写在文件首块开头, 所以调用方有责任先分配好首块, 见 newFile。
    if (blkno < 0)
    {
        std::cout << "目录项无处可写: 文件没有可用的首块 (块号 "
                  << blkno << ")" << std::endl;
        return;
    }
    writeDisk(disk, this, sizeof(FileDir), blkno * BYTE_PER_BLOCK);
}

bool FileDir::is_open(MemInodeTable& i_table, OpenFileTable& f_table)
{
    if (i_table.find(inode) == -1 && f_table.find(inode) == -1)
        return false;
    return true;
}

void FileDir::open(DiskFile& disk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    if (is_open(i_table, f_table)) //先查询这个文件是否已经打开
    {
        return;
    }
    else //如果没有打开就修改i_table和f_table
    {
        i_table.append(disk, inode);
        f_table.append(disk, OpenFileDir{
            FILE_PERMISSION::READ_AND_WRITE, inode, 0
        });
    }
}

void FileDir::close(DiskFile& disk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    if (!is_open(i_table, f_table)) //先查询这个文件是否已经打开
    {
        return;
    }
    else //如果打开了就修改i_table和f_table
    {
        i_table.erase(disk, inode);
        f_table.erase(disk, OpenFileDir{
            FILE_PERMISSION::READ_AND_WRITE, inode, 0
        });
    }
}