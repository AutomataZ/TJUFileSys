#include "filedir.h"
#include "define.h"
#include "wirteDisk/wirteDisk.h"

FileDir::FileDir(std::string filename, int16_t inode, int16_t fa_inode)
{
    for (int i = 0; i < filename.length(); i++) 
        this->f_name[i] = filename[i];
    for (int i = filename.length(); i < FILE_NAME_LENGTH; i++)
        this->f_name[i] = '\0';
    this->inode = inode;
    this->fa_inode = fa_inode;
}

void FileDir::create(std::fstream& disk, int blkno)
{
    writeDisk(disk, this, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK);
}

bool FileDir::is_open(MemInodeTable& i_table, OpenFileTable& f_table)
{
    if (i_table.find(inode) == -1 && f_table.find(inode) == -1)
        return false;
    return true;
}

void FileDir::open(std::fstream& disk, MemInodeTable& i_table, OpenFileTable& f_table)
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

void FileDir::close(std::fstream& disk, MemInodeTable& i_table, OpenFileTable& f_table)
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