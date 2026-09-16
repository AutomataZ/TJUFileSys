#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include "./shell.h"
#include "format.h"
#include "./wirteDisk/wirteDisk.h"
#include "inode.h"
#include "filedir.h"

using namespace std;

void Shell::init(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 读入磁盘的前1024字节作为superblock结构
    readDisk(disk, &sblk, sizeof(SuperBlock), 0);
    // 打开根目录 /
    // 逐个搜索inode，判断是否为根目录对应的inode
    MemInode inode;
    FileDir dir;
    int flag = 0;
    for (int i = 0; i < INODE_NUM; i++)
    {
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + i * sizeof(Inode));
        inode.i_number = i;
        int blk_index = inode.BMap(disk, 0);
        readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blk_index * BYTE_PER_BLOCK);
        if (dir.is_root())
        {
            flag = 1;
            break;
        }
    }
    if (!flag)
    {
        cout << "文件系统格式损坏, 请重新格式化" << endl;
        return;
    }
    if (!dir.is_open(i_table, f_table))
    {
        dir.open(disk, i_table, f_table);
        //当前目录设置为 /
        i_table.modifyCurrentDir(i_table.find(inode.i_number));
    }
}

//当前目录对应的物理块号
int getCurrentBlk(std::fstream& disk, MemInodeTable& i_table)
{
    int index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.BMap(disk, 0);
}

int getCurrentDirSubFileNum(std::fstream& disk, MemInodeTable& i_table)
{
    int index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.getSize() / 2;
}

int getFileModeByInodeIndex(std::fstream& disk, int index)
{
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.getMode();
}

string getFileNameByInodeIndex(std::fstream& disk, int index)
{
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    int blkno = inode.BMap(disk, 0); //物理块号
    FileDir dir;
    readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
    return dir.getFileName();
}

void newFile(int mode, string name, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table, int size)
{
    // 查找当前目录下是否存在同名同类型的文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            mode == getFileModeByInodeIndex(disk, inode_index))
        {
            if (mode == FILE_MODE::dir_file)
                cout << "目录";
            else if (mode == FILE_MODE::normal_file)
                cout << "文件";
            cout << name << "已存在" << endl;
            return;
        }
    }

    // 申请一个空闲 inode
    Inode new_inode;
    int16_t inode_index = sblk.distributeInode(disk);

    // 在当前目录下储存子文件的 inode
    writeDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + sub_file_num * sizeof(int16_t));
    int c_inode_index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode c_inode;
    readDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));
    c_inode.addSubDirSize();
    writeDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));

    if (mode == FILE_MODE::dir_file)
    {
        // 申请一个空闲盘块
        int blk_index = sblk.distributeBlk(disk);
        // 将分配到的空闲盘块关联到分配到的inode
        new_inode.appendBlk(disk, sblk, inode_index, blk_index);
        new_inode.setMode(mode);
    }
    else if (mode == FILE_MODE::normal_file)
    {
        // 首先计算需要分配几个盘块
        int blk_num = size / BYTE_PER_BLOCK + !!(size % BYTE_PER_BLOCK);
        for (int i = 0; i < blk_num; i++)
        {
            int blk_index = sblk.distributeBlk(disk);
            new_inode.appendBlk(disk, sblk, inode_index, blk_index);
            if (i != blk_num - 1)
                new_inode.changeSize(BYTE_PER_BLOCK);
            else
                new_inode.changeSize(size % 512);
        }
        new_inode.setMode(mode);
    }
    // 将inode内容写入磁盘
    writeDisk(disk, &new_inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));

    // 在空闲盘块上新建文件目录项
    FileDir dir(name, inode_index, i_table.inode[i_table.getCurrentDir()].i_number);
    dir.create(disk, new_inode.BMap(disk, 0));
}

int openCloseFile(int mode, string name, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
            if (dir.is_open(i_table, f_table) && mode == OpenClose::open)
            {
                cout << "文件" << name << "已打开" << endl;
                return 0;
            }
            else if (!dir.is_open(i_table, f_table) && mode == OpenClose::close)
            {
                cout << "文件" << name << "未打开" << endl;
                return 0;
            }
            else if (mode == OpenClose::open)
            {
                //打开
                dir.open(disk, i_table, f_table);
                return 1;
            }
            else if (mode == OpenClose::close)
            {
                //关闭
                dir.close(disk, i_table, f_table);
                return 1;
            }
        }
    }
    cout << "当前路径下不存在文件" << name << endl;
    return 0;
}

std::string readWriteFile(int mode, string name, string& str, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    std::string ret;
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index; //对应文件的inode标号
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            //blkno是该文件的起始块
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
            if (!dir.is_open(i_table, f_table))
            {
                cout << "文件" << name << "未打开" << endl;
                return ret;
            }
            else
            {
                //取得文件指针
                int cur = f_table.find(inode_index);
                int pointer_offset = f_table.file[cur].f_offset; //文件指针的位置
                int start_offset = FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK + sizeof(FileDir) + pointer_offset; //读写开始的位置
                //cout << "读写开始的位置是" << start_offset << endl;
                //int start_blk = start_offset / BYTE_PER_BLOCK + !!(start_offset % BYTE_PER_BLOCK); //读写开始的块号
                int start_blk = inode.BMap(disk, (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK);
                //cout << "start_blk是" << start_blk << endl;

                int start_cur = start_offset % BYTE_PER_BLOCK; //在块内，读写开始的位置
                int end_cur = (start_cur + size - 1) % BYTE_PER_BLOCK; //在块内，读写结束的位置
                int blk_num = size / BYTE_PER_BLOCK + !!(size % BYTE_PER_BLOCK); //需要读入的块数

                if (pointer_offset + size > inode.getSize()) //如果读写大小超过了文件上限
                {
                    //如果读大小超过文件上限
                    if (mode == ReadWrite::read)
                    {
                        //size修改为文件上限减去读写开始位置
                        size = inode.getSize() - pointer_offset;
                        end_cur = (start_cur + size - 1) % BYTE_PER_BLOCK;
                        blk_num = size / BYTE_PER_BLOCK + !!(size % BYTE_PER_BLOCK);
                    }
                    //如果写大小超过文件上限
                    else if (mode == ReadWrite::write)
                    {
                        //更新inode内容
                        //新增的字节数
                        int new_byte = pointer_offset + size - inode.getSize();
                        //cout << "new_byte: " << new_byte << endl;
                        //判断新增几个blk
                        //最后一个块还剩余多少空闲字节
                        int empty_byte = (inode.getSize() + sizeof(FileDir)) % BYTE_PER_BLOCK;
                        //cout << "empty_byte: " << empty_byte << endl;
                        int new_blk_num = new_byte > empty_byte ? 
                                            (new_byte - empty_byte) / BYTE_PER_BLOCK + !!((new_byte - empty_byte) % BYTE_PER_BLOCK) + 1 : 
                                            0;
                        //blk_num += new_blk_num;
                        for (int i = 0; i < new_blk_num; i++)
                        {
                            int blk_index = sblk.distributeBlk(disk);
                            inode.appendBlk(disk, sblk, inode_index, blk_index);
                            //cout << "新申请了盘块" << blk_index << endl;
                            if (i != new_blk_num - 1)
                            {
                                inode.changeSize(BYTE_PER_BLOCK);
                                //cout << "新加了" << BYTE_PER_BLOCK << endl;
                            }
                            else
                            {
                                inode.changeSize(new_byte - (new_blk_num - 1) * BYTE_PER_BLOCK);
                                //cout << "新加了" << (new_byte - (new_blk_num - 1) * BYTE_PER_BLOCK) << endl;
                            }
                        }
                        // 将inode内容写入磁盘
                        writeDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
                        
                        // 更新内存inode
                        readDisk(disk, &i_table.inode[i_table.find(inode_index)], sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
                        
                        //cout << "文件新大小为" << inode.getSize() << endl;
                    }
                }
                int offset = 0; //写指针, 记录已经写了多少个字节
                //根据文件的物理位置、文件指针位置、size确定需要读入的块
                for (int i = 0; i < blk_num; i++)
                {
                    //cout << i << ": 对" << inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK) << "操作" << endl;
                    int start = 0;
                    int end = BYTE_PER_BLOCK - 1;
                    if (i == 0) //首个物理块不一定从0开始
                    {
                        start = start_cur;
                    }
                    if (i == blk_num - 1) //最后一个物理块不一定从512结束
                    {
                        end = end_cur;
                    }
                    //cout << "开始" << start << "结束" << end << endl;
                    if (mode == ReadWrite::read)
                    {
                        //读入是从缓存中读入
                        //cout << "读到了: ";
                        char* sub = b_mgr.Bread(disk, inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK))->getLoad();
                        for (int i = start; i <= end; i++)
                            ret += sub[i];
                        //cout << ret << endl;
                        //ret += b_mgr.Bread(disk, inode.BMap(disk, i))->getLoad();
                        //cout << b_mgr.Bread(disk, inode.BMap(disk, i))->getLoad() << endl;
                        //b_mgr.bq.print();
                    }
                    else if (mode == ReadWrite::write)
                    {
                        //要写入的部分是从offset开始，end - start个
                        string buffer;
                        for (int i = offset; i <= offset + end - start; i++)
                        {
                            buffer += str[i];
                        }
                        //写入是写缓存
                        b_mgr.Bwrite(disk, inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK), buffer, start, end - start + 1);
                        offset += end - start + 1;
                    }
                    //更改文件读写指针 
                    f_table.setOffset(inode_index, f_table.getOffset(inode_index) + end - start + 1);
                    //cout << "当前文件指针在" << f_table.getOffset(inode_index) << endl;
                }
                return ret;
            }
            return ret;
        }
    }
    cout << "当前路径下不存在文件" << name << endl;
    return ret;
}

/*
    将用户的一行输入转换为命令和参数
    返回 0 表示有错
    返回 1 表示正常转换
    不检查命令是否正确
*/
int inputToCmd(const string& input, string& cmd, string(&args)[MAX_ARGS_NUM])
{
    stringstream ss(input);
    ss >> cmd;
    if (cmd == "")
        return 0;
    for (int i = 0; i < MAX_ARGS_NUM; i++)
        ss >> args[i];
    return 1;
}

void fformat(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    //清空所有打开项
    i_table.clear();
    f_table.clear();
    //缓存队列清空
    b_mgr.clear(disk);
    //格式化
    diskFormat(disk, sblk, i_table, f_table);
}

void ls(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table)
{
    // 找到当前目录对应的物理块
    int c_dir_index = getCurrentBlk(disk, i_table);
    // 获得当前目录下共有多少个子文件
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);

    // 从磁盘读出所有子文件的inode号
    // 首先计算文件内容开始的地址
    // 人为限制单个文件下子文件数量，保证一个块内可以找完
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        cout << getFileNameByInodeIndex(disk, inode_index) << " ";
    }
    cout << endl;
}

int mkdir(string dirName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table)
{
    newFile(FILE_MODE::dir_file, dirName, disk, sblk, i_table, t_table);
    return 0;
}

void cd(std::string dirName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 查找当前目录下是否存在同名目录
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    //cout << "开始寻找" << endl;
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (dirName == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::dir_file)
        {
            //打开这个目录
            Inode inode; //要打开文件对应的磁盘indoe数据
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            int blkno = inode.BMap(disk, 0); //物理块号
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
            dir.open(disk, i_table, f_table);
            i_table.modifyCurrentDir(i_table.find(inode_index));
            //cout << "当前打开inode是: " << endl;
            //i_table.inode[i_table.find(inode_index)].print();
            return;
        }
    }
    if (dirName == "..") //..是父目录
    {
        //关闭当前目录，并修改当前目录为父目录
        int index = i_table.inode[i_table.getCurrentDir()].i_number; //当前的inode号
        Inode inode; //当前磁盘inode数据
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
        int blkno = inode.BMap(disk, 0); //当前磁盘的物理块号
        FileDir dir;
        readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
        if (dir.is_root()) //根目录没有父目录
            return;
        i_table.erase(disk, dir.getInode());
        f_table.erase(disk, f_table.file[f_table.find(index)]);
        i_table.modifyCurrentDir(i_table.find(dir.getFaInode()));
        //cout << "当前打开inode是: " << endl;
        //i_table.inode[i_table.find(index)].print();
    }
    else if (dirName == ".") //.什么都不做
    {
        ;
    }
    else //其他情况出错
    {
        cout << "当前路径下不存在子目录" << dirName << endl;
    }
}

int fcreat(string fileName, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    newFile(FILE_MODE::normal_file, fileName, disk, sblk, i_table, f_table, size);
    return 0;
}

int fopen(string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    openCloseFile(OpenClose::open, fileName, disk, sblk, i_table, f_table);
    return 1;
}

int fclose(string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    openCloseFile(OpenClose::close, fileName, disk, sblk, i_table, f_table);
    return 1;
}

string fread(string fileName, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    // 首先在文件打开结构中寻找该文件
    if (false)
    {
        cout << "该文件没有打开! " << endl;
        return "";
    }
    else
    {
        // 从文件读写指针的位置开始读入
        string str = "";
        return readWriteFile(ReadWrite::read, fileName, str, size, disk, sblk, i_table, f_table, b_mgr);
    }
}

void fwrite(string fileName, string& buffer, int size, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    // 首先在文件打开结构中寻找该文件
    if (false)
    {
        cout << "该文件没有打开! " << endl;
    }
    else
    {
        readWriteFile(ReadWrite::write, fileName, buffer, size, disk, sblk, i_table, f_table, b_mgr);
    }
}

int flseek(string fileName, int offset, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    std::string ret;
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index; //对应文件的inode标号
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (fileName == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            //blkno是该文件的起始块
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
            if (!dir.is_open(i_table, f_table))
            {
                cout << "文件" << fileName << "未打开" << endl;
                return -1;
            }
            else
            {
                //如果指定位置为负，什么都不做
                if (offset < 0)
                {
                    return f_table.getOffset(inode_index);
                }
                //如果指定位置超过文件尺寸，定位到文件结尾
                else if (offset > inode.getSize())
                {
                    f_table.setOffset(inode_index, inode.getSize());
                }
                else
                {
                    f_table.setOffset(inode_index, offset);
                }
                return f_table.getOffset(inode_index);
            }
            return -1;
        }
    }
    cout << "当前路径下不存在文件" << fileName << endl;
    return -1;
}

void fdelete(string fileName, std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = FILE_AREA_OFFSET + c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        //该文件的inode_index
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        
        //取要删除文件的inode
        Inode inode;
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));

        //不允许删除打开的文件
        int blkno = inode.BMap(disk, 0);
        FileDir dir;
        readDisk(disk, &dir, sizeof(FileDir), FILE_AREA_OFFSET + blkno * BYTE_PER_BLOCK); //读入目录项
        if (dir.is_open(i_table, f_table))
        {
            cout << "文件" << fileName << "未关闭" << endl;
            return;
        }
        
        //将之后的子inode号全都向前移动一个
        if (fileName == getFileNameByInodeIndex(disk, inode_index))
        {
            for (int j = i; j < sub_file_num - 1; j++)
            {
                int16_t tem;
                readDisk(disk, &tem, sizeof(int16_t), file_content_offset + (j + 1) * sizeof(int16_t));
                writeDisk(disk, &tem, sizeof(int16_t), file_content_offset + j * sizeof(int16_t));
            }

            //取当前目录对应的inode，缩减inode对应的d_size
            int c_inode_index = i_table.inode[i_table.getCurrentDir()].i_number;
            Inode c_inode;
            readDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));
            c_inode.eraseSubDirSize();
            writeDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));
        }

        // 对于普通文件可以直接删除
        if (fileName == getFileNameByInodeIndex(disk, inode_index) && 
            FILE_MODE::normal_file == getFileModeByInodeIndex(disk, inode_index))
        {
            inode.releaseAllBlk(disk, sblk);
            sblk.releaseInode(inode_index);
            return;
        }
        // 对于目录，需要判断目录是否为空，若有内容则不予删除
        else if (fileName == getFileNameByInodeIndex(disk, inode_index) && 
            FILE_MODE::dir_file == getFileModeByInodeIndex(disk, inode_index))
        {
            //判断inode里面的文件大小就可以
            if (inode.getSize() != 0)
            {
                cout << "文件夹" << fileName << "非空" << endl;
            }
            else
            {
                inode.releaseAllBlk(disk, sblk);
                sblk.releaseInode(inode_index);
            }
            return;
        }
    }
    cout << "当前路径下不存在文件" << fileName << endl;
    return;
}

void printCurrentPath(std::fstream& disk, MemInodeTable& i_table)
{
    cout << "MF " << i_table.getCurrentFullPath(disk) << " > ";
}

Shell::Shell()
{
}

Shell::~Shell()
{
}

void usage()
{
    cout << "fcreat filename filesize" << endl;
}

void Shell::usr(std::fstream& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    cout << "欢迎使用 misakifs 文件系统" << endl;
    init(disk, sblk, i_table, f_table);

#ifdef FINAL_TEST

    cout << "格式化文件卷" << endl;
    fformat(disk, sblk, i_table, f_table, b_mgr);
    cout << "格式化完成, 当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "新建bin etc home dev四个子文件夹" << endl;
    mkdir("bin", disk, sblk, i_table, f_table);
    mkdir("etc", disk, sblk, i_table, f_table);
    mkdir("home", disk, sblk, i_table, f_table);
    mkdir("dev", disk, sblk, i_table, f_table);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到home文件夹下" << endl;
    cd("home", disk, sblk, i_table, f_table);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "新建texts reports photos三个子文件夹" << endl;
    mkdir("texts", disk, sblk, i_table, f_table);
    mkdir("reports", disk, sblk, i_table, f_table);
    mkdir("photos", disk, sblk, i_table, f_table);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到texts目录下保存报告" << endl;
    cd("texts", disk, sblk, i_table, f_table);
    fcreat("reports.md", 4 * 1024, disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到reports目录下保存txt文件" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("reports", disk, sblk, i_table, f_table);
    fcreat("reports.md", 4 * 1024, disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到photos目录下保存图片" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("photos", disk, sblk, i_table, f_table);
    fcreat("pic.jpg", 4 * 1024, disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "回到根目录" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("..", disk, sblk, i_table, f_table);

    cout << "新建目录test, 并切换至test" << endl;
    mkdir("test", disk, sblk, i_table, f_table);
    cd("test", disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    char ch[] = {'a', 'A'};
    string str, abc, content, content_new;
    for (int i = 0; i < 800; i++)
        str += ch[i < 500] + i % 26;

    cout << "新建文件Jerry, 并写入800个字节" << endl;
    fcreat("Jerry", 10, disk, sblk, i_table, f_table);
    fopen("Jerry", disk, sblk, i_table, f_table);
    fwrite("Jerry", str, str.length(), disk, sblk, i_table, f_table, b_mgr);

    cout << "当前文件的内容是: " << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    cout << fread("Jerry", str.length(), disk, sblk, i_table, f_table, b_mgr) << endl;

    cout << "定位文件指针到500字节" << endl;
    flseek("Jerry", 500, disk, sblk, i_table, f_table);
    cout << "读出500个字节到abc" << endl;
    abc = fread("Jerry", 500, disk, sblk, i_table, f_table, b_mgr);
    cout << "abc的内容是: " << endl;
    cout << abc << endl;

    cout << "将abc写回文件" << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    fwrite("Jerry", abc, abc.length(), disk, sblk, i_table, f_table, b_mgr);
    cout << "当前文件的内容是: " << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    cout << fread("Jerry", str.length(), disk, sblk, i_table, f_table, b_mgr) << endl;

    fclose("Jerry", disk, sblk, i_table, f_table);
    cout << "回到根目录" << endl;
    cd("..", disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

#endif

    string usr_input;
    while (true)
    {
        printCurrentPath(disk, i_table);
        //i_table.print();
        //f_table.print();
        string cmd;
        string args[MAX_ARGS_NUM];
        getline(cin, usr_input);
        if (!inputToCmd(usr_input, cmd, args))
            continue;
        if (cmd == cmd_supported[0]) //fformat
        {
            fformat(disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[1]) //ls
        {
            ls(disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[2]) //mkdir
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            mkdir(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[3])
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            cd(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[4]) //fcreat
        {
            int size = atoi(args[1].c_str());
            if (args[1].empty())
            {
                usage();
                continue;
            }
            fcreat(args[0], size, disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[5]) //fopen
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            fopen(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[6])
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            fclose(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[7]) //fread
        {
            if (args[1].empty())
            {
                usage();
                continue;
            }
            int size = atoi(args[1].c_str());
            string str = fread(args[0], size, disk, sblk, i_table, f_table, b_mgr);
            cout << str << endl;
        }
        else if (cmd == cmd_supported[8]) //fwrite
        {
            if (args[1].empty())
            {
                usage();
                continue;
            }
            int size = min(atoi(args[2].c_str()), (int)args[1].length());
            if (args[2].empty())
                size = args[1].length();
            fwrite(args[0], args[1], size, disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[9])
        {
            int offset = atoi(args[1].c_str());
            int flag = flseek(args[0], offset, disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[10])
        {
            fdelete(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[11])
        {
            //清空缓存队列，目的是将带有延迟写的数据块存盘
            b_mgr.clear(disk);
            //保存修改过的superblock
            writeDisk(disk, &sblk, sizeof(SuperBlock), 0);
            cout << "正在退出 misakifs ..." << endl;
            break;
        }
        else
        {
            cout << cmd << "不是支持的命令" << endl;
        }
    }
}