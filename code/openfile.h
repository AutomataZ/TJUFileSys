#pragma once
#include "inode.h"
#include "define.h"
#include <fstream>

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
        cout << "inode号为: " << i_number << endl;
    }
#endif
};

class MemInodeTable {
protected:
    int i_size;
    int current_dir; //当前打开目录的下标，inode[current_dir]是打开目录的inode
public:
    MemInode inode[MEM_INODE_NUM];
    int size() {return i_size;}
    /// @brief 查找inode_index号inode是否已经打开
    /// @param inode_index inode号
    /// @return 查找到返回该inode对应的下标，找不到返回-1
    int find(int inode_index);
    int append(std::fstream& disk, int inode_index);
    int erase(std::fstream& disk, int inode_index);
    void clear();
    void modifyCurrentDir(int index) { current_dir = index; }
    int getCurrentDir() { return current_dir; }
    string getCurrentDirName(std::fstream& disk);
    string getCurrentFullPath(std::fstream& disk);
#ifdef DEBUG_ENV
    void print() {
        cout << "当前内存inode表是: " << endl;
        for (int i = 0; i < i_size; i++)
        {
            cout << i << ": ";
            inode[i].print();
        }
    }
#endif
};

class OpenFileDir {
protected:
public:
    int f_flag;
    MemInode* f_inode;
    int f_offset; //文件指针的位置
#ifdef DEBUG_ENV
    void print() {
        cout << "打开模式是: " << f_flag << endl;
        cout << "对应inode是: " << f_inode->i_number << endl;
        cout << "文件指针是: " << f_offset << endl;
    }
#endif
};

class OpenFileTable {
protected:
    int t_size;
public:
    OpenFileDir file[OPEN_FILE_TABLE_SIZE];
    int size() {return t_size;}
    int append(std::fstream& disk, OpenFileDir dir);
    int erase(std::fstream& disk, OpenFileDir dir);
    void clear();
    int find(int inode_index);
    int getOffset(int inode_index);
    void setOffset(int inode_index, int offset);
#ifdef DEBUG_ENV
    void print() {
        cout << "当前系统打开文件表是: " << endl;
        for (int i = 0; i < t_size; i++)
        {
            cout << "表项" << i << "是: " << endl;
            file[i].print();
        }
    }
#endif
};