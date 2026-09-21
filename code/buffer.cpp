#include <cstring>
#include "define.h"
#include "buffer.h"
#include "wirteDisk/wirteDisk.h"

#ifdef DEBUG_ENV
#include <iostream>
#include <iomanip>
#endif

Buffer::Buffer()
{
    b_blkno = -1;
    memset(load, 0, sizeof(load));
    del_write = false;
    new_create = true;
}

Buffer::Buffer(int blkno)
{
    b_blkno = blkno;
    memset(load, 0, sizeof(load));
    del_write = false;
    new_create = true;
}

#ifdef DEBUG_ENV
void Buffer::print()
{
    std::cout << "块号为: " << b_blkno << std::endl;
    std::cout << "缓存内容为: " << std::endl;
    for (int i = 0; i < BYTE_PER_BLOCK; i++)
        std::cout << load[i];
    std::cout << std::endl;
    std::cout << "延迟写标值为: " << del_write << std::endl;
}

void Buffer::printBrief()
{
    std::cout << "块号 " << b_blkno
              << (del_write ? "  [延迟写]" : "  [已同步]")
              << "  内容前16字节: ";
    for (int i = 0; i < 16; i++)
    {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)(unsigned char)load[i] << " ";
    }
    std::cout << std::dec << std::setfill(' ') << std::endl;
}
#endif

BQueue::BQueue()
{
}

int BQueue::size()
{
    return (int)q.size();
}

bool BQueue::empty()
{
    return q.empty();
}

Buffer BQueue::front()
{
    return q.front();
}

Buffer BQueue::back()
{
    return q.back();
}

void BQueue::push(const Buffer& b)
{
    // 同一个块号在队列里只能有一份: 出现第二份的话, index 会指向新节点,
    // 旧节点就成了链表里有、索引里没有的孤儿, 它以后被淘汰时还会误删别人的索引
    if (this->size() >= MAX_BQUEUE_SIZE || find(b.getBlkno())->getBlkno() >= 0)
        return;
    q.push_front(b); // 刚进来的块就是最近使用过的, 进队首
    index[b.getBlkno()] = q.begin();
}

void BQueue::pop()
{
    if (this->empty())
        return;
    index.erase(q.back().getBlkno());
    q.pop_back(); // 淘汰队尾那个最久未使用的
}

// 将 blkno 对应的块移动到队首(最近使用端)
void BQueue::update(int blkno)
{
    auto it = index.find(blkno);
    if (it == index.end())
        return;
    // splice 只改指针, 不拷贝元素, 被搬的迭代器也不失效 —— index 里存的正是它
    q.splice(q.begin(), q, it->second);
}

Buffer* BQueue::find(int blkno)
{
    auto it = index.find(blkno);
    if (it == index.end())
        return &err;
    return &(*it->second);
}

// 摘掉队列中间的某一项, 不回写。先摘链表节点, 再摘索引 —— 反过来的话,
// 链表节点还在, index 里却没有它了, 这个块就成了队列里没人认领的孤儿
void BQueue::remove(int blkno)
{
    auto it = index.find(blkno);
    if (it == index.end())
        return;
    q.erase(it->second);
    index.erase(it);
}

#ifdef DEBUG_ENV
void BQueue::print()
{
    std::cout << "队列中共有" << q.size() << "个缓存块" << std::endl;
    int i = 1;
    for (Buffer& b : q)
    {
        std::cout << "第" << i++ << "个缓存为: " << std::endl;
        b.print();
    }
}

void BQueue::printBrief()
{
    // 按下标从小到大就是按"最近使用 -> 最久未使用"排列, 最后一个下一个被淘汰
    std::cout << "缓存队列 (队首 = 最近使用, 队尾 = 最久未使用, 下一个被淘汰): "
              << q.size() << " / " << MAX_BQUEUE_SIZE << std::endl;
    int i = 0;
    for (Buffer& b : q)
    {
        std::cout << "  [" << i++ << "] ";
        b.printBrief();
    }
    if (q.empty())
        std::cout << "  (空)" << std::endl;
}
#endif

Buffer* BufferMgr::getBlk(DiskFile& disk, int blkno)
{
    if (bq.find(blkno)->getBlkno() >= 0) // 先在已经分配过的缓存里寻找
    {
        bq.update(blkno);
    }
    else // 如果找不到就分配一个新的
    {
        if (bq.size() >= MAX_BQUEUE_SIZE) // 如果满了要先淘汰一个(LRU: 队尾那个最久未使用)
        {
            Buffer victim = bq.back();
            if (victim.del_write) // 如果有延迟写, 需要写回磁盘
            {
                writeDisk(disk, victim.getLoad(), BYTE_PER_BLOCK, victim.getBlkno() * BYTE_PER_BLOCK);
            }
            bq.pop();
        }
        bq.push(Buffer(blkno));
    }
    return bq.find(blkno);
}

Buffer* BufferMgr::Bread(DiskFile& disk, int blkno)
{
    // 不论缓存中是否有内容, 返回一个跟 blkno 相关联的缓存块
    Buffer* bp = getBlk(disk, blkno);
    if (bp->new_create) // 不能重用, 缓存没内容, 需要先从磁盘中读入内容
    {
        bp->new_create = false;
        readDisk(disk, bp->getLoad(), BYTE_PER_BLOCK, blkno * BYTE_PER_BLOCK);
    }
    // 能够重用, 缓存有内容, 直接返回
    return bp;
}

Buffer* BufferMgr::Bwrite(DiskFile& disk, int blkno, std::string& buffer, int offset, int size)
{
    // 不论缓存中是否有内容, 返回一个跟 blkno 相关联的缓存块
    //
    // 走 Bread 而不是 getBlk: 这是一次局部写, 只覆盖 [offset, offset+size), 而回写时
    // 整块 512 字节都会写出去。Bread 未命中时会先把块内原有内容读进来, 因此这次写入
    // 构成一次完整的 read-modify-write —— 块内没参与本次写入的部分 (例如文件首块开头
    // 那 sizeof(FileDir) 字节的目录项) 原样保留。
    Buffer* bp = Bread(disk, blkno);
    // 写入缓存中
    int start = 0;
    if (offset >= 0)
    {
        start = offset;
    }
    for (int i = start; i < start + size; i++)
    {
        bp->load[i] = buffer[i - start];
    }
    //打上延迟写
    bp->del_write = true;
    bp->new_create = false;
    return bp;
}

void BufferMgr::remove(int blkno)
{
    bq.remove(blkno);
}

// 把挂着的脏块写回磁盘, 但缓存照留。提交点要的就是这个: clear 顺手把缓存清空,
// 用在提交点上会让后续访问全部落空, 而提交点只关心"盘上那份是全的"。
void BQueue::flush(DiskFile& disk)
{
    for (Buffer& b : q)
    {
        if (b.del_write)
        {
            writeDisk(disk, b.getLoad(), BYTE_PER_BLOCK, b.getBlkno() * BYTE_PER_BLOCK);
            // 已经落到盘上了, 再挂着这个标记只会让它在淘汰时被重复写一遍
            b.del_write = false;
        }
    }
}

void BufferMgr::flush(DiskFile& disk)
{
    bq.flush(disk);
}

void BufferMgr::clear(DiskFile& disk)
{
    while(!bq.empty())
    {
        Buffer victim = bq.back(); // 队尾 = 最久未使用, 正好是 pop 要摘的那个
        if (victim.del_write)
        {
            writeDisk(disk, victim.getLoad(), BYTE_PER_BLOCK, victim.getBlkno() * BYTE_PER_BLOCK);
        }
        bq.pop();
    }
}