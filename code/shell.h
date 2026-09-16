#pragma once
#include <string>
#include <fstream>
#include "./define.h"
#include "superblock.h"
#include "openfile.h"
#include "buffer.h"

enum OpenClose {
    open, close
};

enum ReadWrite {
    read, write
};

class Shell {
protected:
public:
    Shell();
    ~Shell();
    void init(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table);
    void usr(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
};

void printCurrentPath(std::fstream& disk, MemInodeTable& i_table);
int getCurrentBlk(std::fstream& disk, MemInodeTable& i_table);
int getCurrentDirSubFileNum(std::fstream& disk, MemInodeTable& i_table);
int getFileModeByInodeIndex(std::fstream& disk, int index);
string getFileNameByInodeIndex(std::fstream& disk, int index);

void newFile(int mode, string name, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, int size = 0);
int openCloseFile(int mode, string name, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
std::string readWriteFile(int mode, string name, string& str, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);

int inputToCmd(const std::string& input, std::string& cmd, std::string(&args)[MAX_ARGS_NUM]);

void fformat(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
void ls(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int mkdir(std::string dirName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
void cd(std::string dirName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int fcreat(std::string fileName, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int fopen(std::string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int fclose(std::string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
std::string fread(std::string fileName, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
void fwrite(std::string fileName, std::string& buffer, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
int flseek(string fileName, int offset, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
void fdelete(std::string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
