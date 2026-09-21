#include <cstring>
#include "superblock.h"
#include "inode.h"
#include "buffer.h"
#include "wirteDisk/wirteDisk.h"
#ifdef DEBUG_ENV
#include <iostream>
#endif

using namespace std;

void SuperBlock::FormatFreeBlk(DiskFile& disk)
{
    // 以一个 int 变量为一个字
    // 盘块的前 101 个字用来储存前一个分组的索引
    // 第一个字是前一组中空闲盘块的数量
    // 后面 100 个字是空闲盘块的盘块号

    // 先把初始状态下所有做索引块的块号找出来
    // 文件数据区的块号从 192 开始, 因此最后一组唯一一个块的块号是 192
    // 储存 193~292 号的 100 个块
    // 下一个索引块号是 193, 储存 293~392 号的 100 个块
    // 下一个索引块号是 293, 储存 393~492 号的 100 个块
    // ...
    // 下一个索引块号是 7893, 储存 7993~8092 号的 100 个块
    // 最后一个索引块号是 7993, 储存 8093~8191 号的 99 个块
    // 共有 81 个分组, 80 个索引块
    int num = BLOCK_IN_GROUP;
    int num_last = BLOCK_IN_GROUP - 1;
    const int INDEX_BLOCK_NUM = 80;
    int file_index_blkno[INDEX_BLOCK_NUM];
    file_index_blkno[0] = FILE_BLOCK_START;
    file_index_blkno[1] = FILE_BLOCK_START + 1;
    for (int i = 2; i < INDEX_BLOCK_NUM; i++)
    {
        file_index_blkno[i] = file_index_blkno[1] + (i - 1) * BLOCK_IN_GROUP;
    }

    // 对第一个索引块
    int addr = file_index_blkno[0] * BYTE_PER_BLOCK; // 该索引块的偏移地址
    writeDisk(disk, &num, sizeof(num), addr);
    for (int i = 0; i < BLOCK_IN_GROUP; i++)
    {
        int index = file_index_blkno[0] + i + 1; //要指向的块
        int offset = addr + (i + 1) * sizeof(int); // 该索引块管理的每一个块应该写入的地址
        writeDisk(disk, &index, sizeof(index), offset);
    }

    // 对中间 78 个索引块, 写入前 101 个字
    // 第一个字是前一组中空闲盘块的数量
    // 后面 100 个字是空闲盘块的盘块号
    for (int i = 1; i <= INDEX_BLOCK_NUM - 2; i++)
    {
        int addr = file_index_blkno[i] * BYTE_PER_BLOCK; // 该索引块的偏移地址
        writeDisk(disk, &num, sizeof(num), addr);
        for (int j = 0; j < BLOCK_IN_GROUP; j++)
        {
            int index = file_index_blkno[i] + j + 100; // 要指向的块
            int offset = addr + (j + 1) * sizeof(int);
            writeDisk(disk, &index, sizeof(index), offset);
        }
    }

    // 写入最后一个索引块
    addr = file_index_blkno[INDEX_BLOCK_NUM - 1] * BYTE_PER_BLOCK; // 该索引块的偏移地址
    writeDisk(disk, &num_last, sizeof(num_last), addr);
    for (int i = 0; i < BLOCK_IN_GROUP; i++)
    {
        int offset = addr + (i + 1) * sizeof(int);
        if (i == 0) // 写入 0 作为索引结束标志
        {
            int t = 0;
            writeDisk(disk, &t, sizeof(t), offset);
        }
        else // 后面 99 个块正常写入
        {
            int index = file_index_blkno[INDEX_BLOCK_NUM - 1] + (i - 1) + 100; //要指向的块
            writeDisk(disk, &index, sizeof(index), offset);
        }
    }
}

SuperBlock::SuperBlock()
{
    // inode 区大小
    s_isize = INODE_BLOCK_NUM;
    // 空闲 inode 是 100 个(最大数量)
    s_ninode = SUPER_CONTROL_INODE_NUM;
    // 所有 inode 均空闲, 从 0 号顺着往下编号
    for (int i = 0; i < SUPER_CONTROL_INODE_NUM; i++)
    {
        s_inode[i] = i;
    }

    /*
        按照本课设中数据结构大小来看
        共有 8000 个文件数据盘块, 初始状态下全空闲
        因此分为 81 组
        最后一组有 1 个块
        中间 79 组有 100 个块
        第一组有 99 个块
    */

    // 文件数据区大小
    s_fsize = FILE_BLOCK_NUM;
    // 直接管理的空闲盘块有 1 个
    s_nfree = 1;
    // 盘块号是 192
    s_free[0] = 192;

    memset(load, -1, sizeof(load));
}

SuperBlock::SuperBlock(DiskFile& disk)
{
    readDisk(disk, this, sizeof(SuperBlock), 0);
}

int SuperBlock::distributeInode(DiskFile& disk)
{
    if (s_ninode > 0) // 退栈
    {
        int ret = s_inode[--s_ninode];
        s_inode[s_ninode] = INODE_IS_OCCUPIED;
        // 交出去之前先把空闲 inode 表落盘。盘上那份表是重启后判断"哪些 inode
        // 还能用"的唯一依据, 它慢一步, 已经分出去的 inode 就会被再分一次
        save(disk);
        return ret;
    }
    else // 在 inode 区搜索 100 个空闲 inode
    {
        Inode inode;
        for (int i = 0; i < INODE_NUM; i++)
        {
            readDisk(disk, &inode, sizeof(Inode), i * sizeof(Inode) + INODE_AREA_OFFSET);
            if (inode.isEmpty())
            {
                s_inode[s_ninode++] = i;
            }
            if (s_ninode >= SUPER_CONTROL_INODE_NUM)
            {
                break;
            }
        }
        if (s_ninode > 0)
        {
            int ret = s_inode[--s_ninode];
            s_inode[s_ninode] = INODE_IS_OCCUPIED;
            save(disk);   // 同上: 搜索出来的这张表也要在交出去之前落盘
            return ret;
        }
    }
    return -1;
}

void SuperBlock::releaseInode(int index)
{
    if (s_ninode < SUPER_CONTROL_INODE_NUM)
    {
        s_inode[s_ninode++] = index;
    }
}

int SuperBlock::distributeBlk(DiskFile& disk, BufferMgr& b_mgr)
{
    if (s_nfree > 0) // 取 s_free[] 的最后一个成员分配走
    {
        int ret = s_free[--s_nfree];
        s_free[s_nfree] = FILE_BLOCK_IS_OCCUPIED;
        if (s_nfree == 0) // 若 s_nfree 为 0 , 需要将分配走的索引块的索引填进来
        {
            // 读 ret 的前 101 个字
            int addr = ret * BYTE_PER_BLOCK;
            readDisk(disk, &s_nfree, sizeof(int), addr);
            // 索引块的排布是"字 0 记数量, 其后紧跟该数量的块号"。
            // 唯一的例外是最后一块 (7993): 它只有 99 个块号, 字 1 被留作结束标志,
            // 块号因此从字 2 开始 —— ini 就是给这个整体偏移用的。
            int ini = s_nfree == BLOCK_IN_GROUP - 1 ? 1 : 0;
            // 循环跑满 s_nfree 次, 填满 s_free[0..s_nfree-1]: 无论读几个块号, 起点一律
            // 是字 ini + 1, 也就是把 ini 算在地址上。最后一块只有 99 个块号, 所以它比
            // 常规的索引块少读一个 —— 数量由 s_nfree 决定, 与 ini 无关。
            for (int i = 0; i < s_nfree; i++)
            {
                readDisk(disk, s_free + i, sizeof(int), addr + (ini + 1 + i) * sizeof(int));
            }
        }
        // 块号交出去之前先把空闲链落盘: 盘上那份链要是还列着这个块, 重启后它会被
        // 再分一次, 两个文件共用同一块。回收方向(releaseBlk)不落盘 —— 少记一次
        // 释放最坏是块从链上消失, 找不回但也不会被谁用上。
        save(disk);
        // 缓存里那份是上一个主人的内容, 与这个块再无关系: 留着它, 淘汰或 clear 时
        // 就会把它写回磁盘, 盖掉新主人写上去的东西
        b_mgr.remove(ret);
        return ret;
    }
    return -1;
}

void SuperBlock::releaseBlk(DiskFile& disk, int index, BufferMgr& b_mgr)
{
    // 块刚离开它的主人, 缓存里那份副本就此作废。下面这个块还可能被当成新的分组
    // 索引块直接写上 101 个字, 更不能留着一份旧内容等着被写回去
    b_mgr.remove(index);

    if (s_nfree < 100)
    {
        s_free[s_nfree++] = index;
    }
    else
    {
        int addr = index * BYTE_PER_BLOCK;
        writeDisk(disk, &s_nfree, sizeof(int), addr);
        for (int i = 0; i < 100; i++)
        {
            writeDisk(disk, &s_free[i], sizeof(int), (i + 1) * sizeof(int) + addr);
        }
        s_free[0] = index;
        s_nfree = 1;
    }
}

void SuperBlock::save(DiskFile& file)
{
    // 空闲 inode 表与空闲盘块链都在这个对象里, 整块写回磁盘的前 1024 字节
    writeDisk(file, this, sizeof(SuperBlock), 0);
}

void SuperBlock::print()
{
    
    cout << "inode区占用的块数是: " << s_isize << endl;
    cout << "目前直接管理的空闲inode数量是: " << s_ninode << endl;
    cout << "目前直接管理的空闲inode是: " << endl;
    for (int i = 0; i < s_ninode; i++)
    {
        cout << s_inode[i] << " ";
    }
    cout << endl;
    cout << "数据区盘块总数是: " << s_fsize << endl;
    
    cout << "目前直接管理的空闲数据区块数是: " << s_nfree << endl;
    cout << "目前直接管理的空闲数据区块是: " << endl;
    for (int i = 0; i < s_nfree; i++)
    {
        cout << s_free[i] << " ";
    }
    cout << endl;
}