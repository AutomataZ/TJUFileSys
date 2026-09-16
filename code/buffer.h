#pragma once
#include "define.h"

/*
    缓存块
*/
class Buffer{
protected:
    int b_blkno; // 关联的盘块号
    char load[512]; // 缓存的内容
    int del_write; // 是否要延迟写
    int new_create; // 缓存是否是新创建的
public:
    Buffer();
    Buffer(int blkno);
    char* getLoad(){return load;}
    void set(int blkno){b_blkno = blkno;}
    int getBlkno(){return b_blkno;}
    friend class BufferMgr;
    friend class BQueue;
#ifdef DEBUG_ENV
    void print();
    /// @brief 单行输出缓存块状态, 用于 cache 命令展示整个队列
    void printBrief();
#endif
};

/*
    实现一个可以将任意元素放到队列末尾的队列
    队列元素为Buffer
    实际上是"假"队列, 使用数组模拟实现
    提供一个寻找函数
*/
class BQueue{
protected:
    Buffer q[MAX_BQUEUE_SIZE];
    int num;
    Buffer err;
public:
    BQueue();
    int size();
    bool empty();
    void pop();
    void push(Buffer b);
    Buffer front();
    void update(int blkno);
    Buffer* find(int blkno);
#ifdef DEBUG_ENV
    void print();
    /// @brief 按 LRU 顺序单行列出队列中每个缓存块
    void printBrief();
#endif
};

/*
    由于只有单设备单进程, 缓存队列简化为单个队列
    对文件系统提供接口, 进行缓存的读写
*/
class BufferMgr{
protected:
public:
    BQueue bq; // 已经分配过的缓存, 使用 LRU 管理队列
    
    /// @brief 分配一块缓存
    /// @param disk 用来模拟磁盘的文件
    /// @param blkno 要与这个物理块号相关联
    /// @return 分配到的缓存的地址
    Buffer* getBlk(std::fstream& disk,int blkno);

    Buffer* Bread(std::fstream& disk, int blkno);

    /// @brief 向缓存中写入内容
    /// @param disk 用来模拟磁盘的文件
    /// @param blkno 要向这个块号中写入内容
    /// @param buffer 要写入的内容
    /// @param offset 块内的起始位置
    /// @param size 要写入的字节数，保证不会写超过块大小的内容
    /// @return 返回指向这个块的指针
    Buffer* Bwrite(std::fstream& disk, int blkno, std::string& buffer, int offset, int size);

    void clear(std::fstream& disk);

};