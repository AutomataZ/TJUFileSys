#pragma once
#include "define.h"

#include <list>
#include <unordered_map>

class DiskFile;

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
    /// @brief 直接改块号。改了它, BQueue 里那份"块号 -> 链表位置"的索引就指错了,
    ///        所以只能在该块还没入队时用
    void set(int blkno){b_blkno = blkno;}
    int getBlkno() const {return b_blkno;}
    friend class BufferMgr;
    friend class BQueue;
#ifdef DEBUG_ENV
    void print();
    /// @brief 单行输出缓存块状态, 用于 cache 命令展示整个队列
    void printBrief();
#endif
};

/*
    缓存队列, 用哈希表 + 双向链表实现 LRU:
      q      双向链表, 按访问顺序排列 —— 队首是最新访问的, 队尾是最久未使用的
      index  块号 -> 该块在链表里的位置。有它才能 O(1) 找到命中的块, 并把它 O(1) 挪到队首
    规则: 新块入队放队首; 命中时移到队首; 淘汰时摘队尾。
*/
class BQueue{
protected:
    std::list<Buffer> q;
    std::unordered_map<int, std::list<Buffer>::iterator> index;
    Buffer err; // find 未命中时返回的哨兵
public:
    BQueue();
    int size();
    bool empty();

    /// @brief 摘掉队尾, 即最久未使用的那一块 (淘汰)
    void pop();

    /// @brief 新块入队, 放到队首(最近使用端)
    /// @param b 要入队的缓存块
    void push(const Buffer& b);

    /// @brief 队首的缓存块: 最近使用过的那个
    Buffer front();

    /// @brief 队尾的缓存块: 最久未使用的那个, 下一个被淘汰
    Buffer back();

    /// @brief 命中后把该块移到队首, 刷成最近使用
    /// @param blkno 命中的盘块号; 队列里没有这个块就什么都不做
    void update(int blkno);

    Buffer* find(int blkno);

    /// @brief 把某个盘块的缓存从队列里摘掉, 不回写
    /// @param blkno 要摘掉的盘块号; 队列里没有这个块就什么都不做
    void remove(int blkno);

    /// @brief 把队列里所有脏块写回磁盘, 缓存照留 (与 clear 的区别就在这里)
    /// @param disk 用来模拟磁盘的文件
    void flush(DiskFile& disk);
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
    Buffer* getBlk(DiskFile& disk,int blkno);

    Buffer* Bread(DiskFile& disk, int blkno);

    /// @brief 向缓存中写入内容
    /// @param disk 用来模拟磁盘的文件
    /// @param blkno 要向这个块号中写入内容
    /// @param buffer 要写入的内容
    /// @param offset 块内的起始位置
    /// @param size 要写入的字节数，保证不会写超过块大小的内容
    /// @return 返回指向这个块的指针
    Buffer* Bwrite(DiskFile& disk, int blkno, std::string& buffer, int offset, int size);

    /// @brief 作废某个盘块的缓存副本
    ///
    /// 盘块易主(被回收进空闲链, 或从空闲链里分配出去)时调用。缓存里那份是旧主人的
    /// 内容, 留着它, 它迟早在淘汰或 clear 时被写回磁盘, 盖掉新主人刚写上去的东西。
    /// 这里直接丢弃不回写: 块已经不属于任何人了, 它的内容没有保留的价值。
    /// @param blkno 易主的盘块号
    void remove(int blkno);

    /// @brief 把队列里所有脏块写回磁盘, 缓存不清空
    ///
    /// 提交点用: 缓存里的内容还在往后延, 但这一刻必须保证磁盘上那份是全的。
    /// @param disk 用来模拟磁盘的文件
    void flush(DiskFile& disk);

    void clear(DiskFile& disk);

};