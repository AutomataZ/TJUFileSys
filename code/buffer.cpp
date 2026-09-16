#include <cstring>
#include "define.h"
#include "buffer.h"
#include "wirteDisk/wirteDisk.h"

#ifdef DEBUG_ENV
#include <iostream>
#endif

const int BLKNO_NOT_FOUND = -1;

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
#endif

BQueue::BQueue()
{
    num = 0;
}

int BQueue::size()
{
    return num;
}

bool BQueue::empty()
{
    return num == 0;
}

Buffer BQueue::front()
{
    return q[0];
}

void BQueue::push(Buffer b)
{
    if (this->size() >= MAX_BQUEUE_SIZE)
        return;
    q[num++] = b;
}

void BQueue::pop()
{
    if (this->empty())
        return;
    for (int i = 1; i < num; i++)
        q[i - 1] = q[i];

    q[num - 1].b_blkno = -1;
    q[num - 1].del_write = false;
    q[num - 1].new_create = true;
    memset(q[num - 1].load, 0, sizeof(q[num - 1].load));

    num--;
}

// 将 blkno 对应的块移动到队列末尾
void BQueue::update(int blkno)
{
    int flag = BLKNO_NOT_FOUND;
    for (int i = 0; i < num; i++)
    {
        if (q[i].b_blkno == blkno)
        {
            flag = i;
            break;
        }
    }
    if (flag != BLKNO_NOT_FOUND && flag != num - 1)
    {
        Buffer temp = q[flag];
        for (int i = flag; i < num; i++)
        {
            q[i] = q[i + 1];
        }
        q[num - 1] = temp;
    }
}

Buffer* BQueue::find(int blkno)
{
    for (int i = 0; i < num; i++)
    {
        if(q[i].getBlkno() == blkno)
        {
            return q + i;
        }
    }
    return &err;
}

#ifdef DEBUG_ENV
void BQueue::print()
{
    std::cout << "队列中共有" << num << "个缓存块" << std::endl;
    for (int i = 0; i < num; i++)
    {
        std::cout << "第" << i+1 << "个缓存为: " << std::endl;
        q[i].print();
    }
}
#endif

Buffer* BufferMgr::getBlk(std::fstream& disk, int blkno)
{
    if (bq.find(blkno)->getBlkno() >= 0) // 先在已经分配过的缓存里寻找
    {
        bq.update(blkno);
    }
    else // 如果找不到就分配一个新的
    {
        if (bq.size() >= MAX_BQUEUE_SIZE) // 如果满了要先弹出一个(LRU)
        {
            if (bq.front().del_write) // 如果有延迟写, 需要写回磁盘
            {
                writeDisk(disk, bq.front().getLoad(), BYTE_PER_BLOCK, bq.front().getBlkno() * BYTE_PER_BLOCK);
            }
            bq.pop();
        }
        bq.push(Buffer(blkno));
    }
    return bq.find(blkno);
}

Buffer* BufferMgr::Bread(std::fstream& disk, int blkno)
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

Buffer* BufferMgr::Bwrite(std::fstream& disk, int blkno, std::string& buffer, int offset, int size)
{
    // 不论缓存中是否有内容, 返回一个跟 blkno 相关联的缓存块
    Buffer* bp = getBlk(disk, blkno);
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

void BufferMgr::clear(std::fstream& disk)
{
    while(!bq.empty())
    {
        if (bq.front().del_write)
        {
            writeDisk(disk, bq.front().getLoad(), BYTE_PER_BLOCK, bq.front().getBlkno() * BYTE_PER_BLOCK);
        }
        bq.pop();
    }
}