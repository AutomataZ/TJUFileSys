#include "openfile.h"
#include "filedir.h"
#include "wirteDisk/wirteDisk.h"

int MemInodeTable::find(int inode_index)
{
    for (int i = 0; i < i_size; i++)
    {
        if (inode[i].i_number == inode_index)
            return i;
    }
    return -1;
}

int MemInodeTable::append(std::fstream& disk, int inode_index)
{
    if (i_size < MEM_INODE_NUM)
    {
        //读入对应的磁盘inode
        readDisk(disk, inode + i_size, sizeof(Inode), INODE_AREA_OFFSET + inode_index * INODE_SIZE);
        //写入inode标号
        inode[i_size].i_number = inode_index;
        i_size++;
        return 0;
    }
    return -1;
}

// 在inode_table中删除一个inode
int MemInodeTable::erase(std::fstream& disk, int inode_index)
{
    int cur = find(inode_index);
    if (cur != -1)
    {
        for (int i = cur; i < i_size - 1; i++)
        {
            inode[i] = inode[i + 1];
        }
        i_size--;
        return 0;
    }
    return -1;
}

string MemInodeTable::getCurrentDirName(std::fstream& disk)
{
    int index = inode[current_dir].BMap(disk, 0);
    FileDir dir;
    readDisk(disk, &dir, sizeof(FileDir), index * BYTE_PER_BLOCK);
    return dir.getFileName();
}

string MemInodeTable::getCurrentFullPath(std::fstream& disk)
{
    // index 是这个inode管理的第一个物理块块号，文件目录项在这里
    int index = inode[current_dir].BMap(disk, 0);
    string full_path;
    string split = "/";
    while(true)
    {
        FileDir dir;
        readDisk(disk, &dir, sizeof(FileDir), index * BYTE_PER_BLOCK);
        if (dir.is_root())
            full_path = dir.getFileName() + full_path;
        else
            full_path = dir.getFileName() + split + full_path;
        if (dir.is_root())
            break;
        else
        {
            index = inode[find(dir.getFaInode())].BMap(disk, 0);
        }
    }
    return full_path;
}

void MemInodeTable::clear()
{
    i_size = 0;
}

int OpenFileTable::append(std::fstream& disk, OpenFileDir dir)
{
    if (t_size < OPEN_FILE_TABLE_SIZE)
    {
        file[t_size++] = dir;
        return 0;
    }
    return -1;
}

int OpenFileTable::erase(std::fstream& disk, OpenFileDir dir)
{
    int cur = find(dir.f_inode_num);
    if (cur != -1)
    {
        for (int i = cur; i < t_size - 1; i++)
        {
            file[i] = file[i + 1];
        }
        t_size--;
        return 0;
    }
    return -1;
}

void OpenFileTable::clear()
{
    t_size = 0;
}

int OpenFileTable::find(int inode_index)
{
    for (int i = 0; i < t_size; i++)
    {
        if (inode_index == file[i].f_inode_num)
            return i;
    }
    return -1;
}

int OpenFileTable::getOffset(int inode_index)
{
    int cur = find(inode_index);
    if (cur != -1)
    {
        return file[cur].f_offset;
    }
    return -1;
}

void OpenFileTable::setOffset(int inode_index, int offset)
{
    int cur = find(inode_index);
    if (cur != -1)
    {
        file[cur].f_offset = offset;
        return;
    }
    return;
}