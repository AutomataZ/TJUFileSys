#pragma once
#include "inode.h"
#include "define.h"
class DiskFile;

#ifdef DEBUG_ENV
#include <iostream>
using namespace std;
#endif

enum FILE_PERMISSION {
    READ_ONLY, READ_AND_WRITE
};

class MemInode: public Inode {
protected:
public:
    int i_number; //inode号
#ifdef DEBUG_ENV
    void print() {
        const char* type = d_mode == FILE_MODE::dir_file ? "目录" :
                           d_mode == FILE_MODE::normal_file ? "文件" : "空";
        cout << "inode号 " << i_number << "  类型 " << type
             << "  大小 " << d_size << " 字节  索引表 [";
        for (int i = 0; i < 10; i++)
            cout << d_addr[i] << (i == 9 ? "" : " ");
        cout << "]" << endl;
    }
#endif
};

class MemInodeTable {
protected:
    int i_size;
    int current_dir; //当前打开目录的下标，inode[current_dir]是打开目录的inode
public:
    MemInode inode[MEM_INODE_NUM];

    // 表刚建立时是空的。没有这个构造函数, i_size 就是栈上的垃圾值, 而 find() 拿它
    // 当遍历上界 —— 表里一项都没有却要走过上亿个 inode[] 项
    MemInodeTable() : i_size(0), current_dir(0) {}

    int size() {return i_size;}
    /// @brief 查找inode_index号inode是否已经打开
    /// @param inode_index inode号
    /// @return 查找到返回该inode对应的下标，找不到返回-1
    int find(int inode_index);
    int append(DiskFile& disk, int inode_index);
    int erase(DiskFile& disk, int inode_index);
    void clear();
    void modifyCurrentDir(int index) { current_dir = index; }
    int getCurrentDir() { return current_dir; }
    string getCurrentDirName(DiskFile& disk);
    string getCurrentFullPath(DiskFile& disk);
#ifdef DEBUG_ENV
    void print() {
        cout << "当前内存inode表 (共 " << i_size << " / " << MEM_INODE_NUM
             << " 项, 当前目录下标 " << current_dir << "):" << endl;
        for (int i = 0; i < i_size; i++)
        {
            cout << "  [" << i << "] ";
            inode[i].print();
        }
    }
#endif
};

class OpenFileDir {
protected:
public:
    int f_flag;
    // 这里存 inode 号而不是指向 i_table.inode[] 的指针:
    // MemInodeTable::erase 会左移数组, 裸指针会随之整体偏移而失效
    int f_inode_num;
    int f_offset; //文件指针的位置
#ifdef DEBUG_ENV
    void print() {
        cout << "权限 " << (f_flag == FILE_PERMISSION::READ_ONLY ? "只读" : "读写")
             << "  inode号 " << f_inode_num
             << "  读写指针 " << f_offset << endl;
    }
#endif
};

class OpenFileTable {
protected:
    int t_size;
public:
    OpenFileDir file[OPEN_FILE_TABLE_SIZE];

    // 同上: 表刚建立时是空的
    OpenFileTable() : t_size(0) {}

    int size() {return t_size;}
    int append(DiskFile& disk, OpenFileDir dir);
    int erase(DiskFile& disk, OpenFileDir dir);
    void clear();
    int find(int inode_index);
    int getOffset(int inode_index);
    void setOffset(int inode_index, int offset);
#ifdef DEBUG_ENV
    void print() {
        cout << "当前系统打开文件表 (共 " << t_size << " / "
             << OPEN_FILE_TABLE_SIZE << " 项):" << endl;
        for (int i = 0; i < t_size; i++)
        {
            cout << "  [" << i << "] ";
            file[i].print();
        }
    }
#endif
};