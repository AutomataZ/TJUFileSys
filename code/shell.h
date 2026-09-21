#pragma once
#include <string>
class DiskFile;
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
    void init(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table);
    void usr(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
};

void printCurrentPath(DiskFile& disk, MemInodeTable& i_table);
int getCurrentBlk(DiskFile& disk, MemInodeTable& i_table);
int getCurrentDirSubFileNum(DiskFile& disk, MemInodeTable& i_table);
int getFileModeByInodeIndex(DiskFile& disk, int index);
string getFileNameByInodeIndex(DiskFile& disk, int index);

void newFile(int mode, string name, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr, int size);
int openCloseFile(int mode, string name, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
std::string readWriteFile(int mode, string name, string& str, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);

int inputToCmd(const std::string& input, std::string& cmd, std::string(&args)[MAX_ARGS_NUM]);
int parseNonNegInt(const std::string& s, int& out);

void fformat(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
void ls(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int mkdir(std::string dirName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
void cd(std::string dirName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
int fcreat(std::string fileName, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
int fopen(std::string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
int fclose(std::string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
std::string fread(std::string fileName, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
void fwrite(std::string fileName, std::string& buffer, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
int flseek(string fileName, int offset, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table);
void fdelete(std::string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr);
