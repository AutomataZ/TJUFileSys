#include "inode.h"
#include "wirteDisk/wirteDisk.h"
#ifdef DEBUG_ENV
#include <iostream>
using namespace std;
#endif

bool Inode::isEmpty()
{
    return !d_mode;
}

void Inode::appendBlk(std::fstream& disk, SuperBlock& s, int inode_index, int blkno)
{
    int inode_offset = INODE_AREA_OFFSET + inode_index * sizeof(Inode) + 2 * sizeof(int); //这个inode的d_addr[0]的起始地址
    int blk_num = d_size / BYTE_PER_BLOCK + !!(d_size % BYTE_PER_BLOCK); //当前inode中已经管理了几个块
    if (blk_num < DIRECT_INDEX_NUM) // 直接索引
    {
        d_addr[blk_num] = blkno;
        writeDisk(disk, &blkno, sizeof(int), inode_offset + blk_num * sizeof(int));
        // 如果加完后, 直接索引块用完了, 需要分配一个一级索引块
        if (blk_num == DIRECT_INDEX_NUM - 1)
        {
            int blk_index = s.distributeBlk(disk);
            d_addr[DIRECT_INDEX_NUM] = blk_index;
            writeDisk(disk, &blk_index, sizeof(int), inode_offset + DIRECT_INDEX_NUM * sizeof(int));
        }
        return;
    }
    else if (blk_num < FIRST_LEVEL_INDIRECT_INDEX_NUM) // 一级间接索引，d_addr[6]和d_addr[7]
    {
        int num = blk_num - DIRECT_INDEX_NUM; // 在一级索引中已经占据了几个块，最大为255
        int index = num / 128; // 第一张索引表是否已经满了，只有0和1两个值
        int cnt = num % 128; // 在索引表的哪个位置
        int table = d_addr[index + DIRECT_INDEX_NUM]; // 取得索引块
        writeDisk(disk, &blkno, sizeof(int), FILE_AREA_OFFSET + table * BYTE_PER_BLOCK + cnt * sizeof(int));
        //如果加完后，第一个或第二个直接索引块用完了，需要分配一个新的索引块
        if (cnt == 127)
        {
            int blk_index = s.distributeBlk(disk);
            d_addr[DIRECT_INDEX_NUM + index + 1] = blk_index;
            writeDisk(disk, &blk_index, sizeof(int), inode_offset + (DIRECT_INDEX_NUM + index + 1) * sizeof(int));
            if (index) //如果分配的是二级间接索引块，还需要再分配一个一级间接索引块
            {
                int blk_index_2 = s.distributeBlk(disk);
                writeDisk(disk, &blk_index_2, sizeof(blk_index_2), FILE_AREA_OFFSET + blk_index * BYTE_PER_BLOCK);
            }
            //cout << "分配的新索引块是" << dec << blk_index << endl;
            //cout << "写入的位置是: " << hex << inode_offset + (DIRECT_INDEX_NUM + index + 1) * sizeof(int) << endl;
        }
        return;
    }
    else if (blk_num < SECOND_LEVEL_INDIRECT_INDEX_NUM) // 二级间接索引, 实际上本课设用来模拟磁盘的文件都没有这么大...
    {
        int num = blk_num - FIRST_LEVEL_INDIRECT_INDEX_NUM; //在二级索引中已经占据了几个块
        int index = num / (128 * 128); //第一张间接索引表是否已经满了
        int cnt_1 = num % (128 * 128) / 128; //在哪张直接索引表
        int cnt_2 = num % 128; //在直接索引表中位置

        int table_1 = d_addr[index + DIRECT_INDEX_NUM + 2]; //间接索引块块号
        //cout << "间接索引块块号是" << dec << table_1 << endl;
        int table_2; //直接索引块块号，需要从磁盘中读出
        readDisk(disk, &table_2, sizeof(int), FILE_AREA_OFFSET + table_1 * BYTE_PER_BLOCK + cnt_1 * sizeof(int));
        //cout << "直接索引块块号是" << table_2 << endl;
        writeDisk(disk, &blkno, sizeof(int), FILE_AREA_OFFSET + table_2 * BYTE_PER_BLOCK + cnt_2 * sizeof(int));

        //如果第一个间接索引块用完了，需要一个新的间接索引块
        if (cnt_1 == 127 && cnt_2 == 127)
        {
            int blk_index = s.distributeBlk(disk);
            d_addr[9] = blk_index;
            writeDisk(disk, &blk_index, sizeof(int), inode_offset + 9 * sizeof(int));
        }
        //如果间接索引块中的一个直接索引块用完了，需要一个新的直接索引块
        else if (cnt_2 == 127)
        {
            int blk_index = s.distributeBlk(disk);
            writeDisk(disk, &blk_index, sizeof(int), FILE_AREA_OFFSET + table_1 * BYTE_PER_BLOCK + (cnt_1 + 1) * sizeof(int));
        }
        return;
    }
    else // 没法加了, 什么都不做
    {
        return;
    }
}

int Inode::BMap(std::fstream& disk, int blkno)
{
    int ret = -1;
    if (blkno < DIRECT_INDEX_NUM) // 直接索引
    {
        ret = d_addr[blkno];
    }
    else if (blkno < FIRST_LEVEL_INDIRECT_INDEX_NUM) // 一级间接索引，d_addr[6]和d_addr[7]
    {
        int num = blkno - DIRECT_INDEX_NUM; // 在一级索引中已经占据了几个块，最大为255
        int index = num / 128; // 第一张索引表是否已经满了，只有0和1两个值
        int cnt = num % 128; // 在索引表的哪个位置
        int table = d_addr[index + DIRECT_INDEX_NUM]; // 取得索引块
        readDisk(disk, &ret, sizeof(int), FILE_AREA_OFFSET + table * BYTE_PER_BLOCK + cnt * sizeof(int));
    }
    else if (blkno < SECOND_LEVEL_INDIRECT_INDEX_NUM)
    {
        int num = blkno - FIRST_LEVEL_INDIRECT_INDEX_NUM; //在二级索引中已经占据了几个块
        int index = num / (128 * 128); //第一张间接索引表是否已经满了
        int cnt_1 = num % (128 * 128) / 128; //在哪张直接索引表
        int cnt_2 = num % 128; //在直接索引表中位置

        int table_1 = d_addr[index + DIRECT_INDEX_NUM + 2]; //间接索引块块号
        int table_2; //直接索引块块号，需要从磁盘中读出
        readDisk(disk, &table_2, sizeof(int), FILE_AREA_OFFSET + table_1 * BYTE_PER_BLOCK + cnt_1 * sizeof(int));
        readDisk(disk, &ret, sizeof(int), FILE_AREA_OFFSET + table_2 * BYTE_PER_BLOCK + cnt_2 * sizeof(int));
    }
    return ret;
}

void Inode::releaseAllBlk(std::fstream& disk, SuperBlock& s)
{
    //首先释放所有数据块
    int blk_num = d_size / BYTE_PER_BLOCK + !!(d_size % BYTE_PER_BLOCK); //当前inode中已经管理了几个块
    for (int i = 0; i < blk_num; i++)
    {
        s.releaseBlk(disk, BMap(disk, i));
    }
    //其次释放所有索引块
    //计算用了几个索引块
    if (blk_num > 6)
    {
        s.releaseBlk(disk, d_addr[6]);
    }
    if (blk_num > 6 + 128)
    {
        s.releaseBlk(disk, d_addr[7]);
    }
    if (blk_num > 6 + 2 * 128)
    {
        s.releaseBlk(disk, d_addr[8]);
    }
    if (blk_num > 6 + 2 * 128 + 128 * 128)
    {
        s.releaseBlk(disk, d_addr[9]);
    }
}