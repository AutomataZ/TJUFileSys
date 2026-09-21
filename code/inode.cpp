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
    // 当前 inode 中已经管理了几个块, 也就是新块该落在哪个槽位。
    // d_size 是字节数, 首块开头 16 字节是目录项, 所以要按 blocksForFileContent
    // 换算 —— 与 newFile 分配盘块时用的是同一个式子, 两边必须一致。
    int blk_num = blocksForFileContent(d_size);
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
        writeDisk(disk, &blkno, sizeof(int), table * BYTE_PER_BLOCK + cnt * sizeof(int));
        //如果加完后，第一个或第二个直接索引块用完了，需要分配一个新的索引块
        if (cnt == 127)
        {
            int blk_index = s.distributeBlk(disk);
            d_addr[DIRECT_INDEX_NUM + index + 1] = blk_index;
            writeDisk(disk, &blk_index, sizeof(int), inode_offset + (DIRECT_INDEX_NUM + index + 1) * sizeof(int));
            if (index) //如果分配的是二级间接索引块，还需要再分配一个一级间接索引块
            {
                int blk_index_2 = s.distributeBlk(disk);
                writeDisk(disk, &blk_index_2, sizeof(blk_index_2), blk_index * BYTE_PER_BLOCK);
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
        readDisk(disk, &table_2, sizeof(int), table_1 * BYTE_PER_BLOCK + cnt_1 * sizeof(int));
        //cout << "直接索引块块号是" << table_2 << endl;
        writeDisk(disk, &blkno, sizeof(int), table_2 * BYTE_PER_BLOCK + cnt_2 * sizeof(int));

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
            writeDisk(disk, &blk_index, sizeof(int), table_1 * BYTE_PER_BLOCK + (cnt_1 + 1) * sizeof(int));
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
        readDisk(disk, &ret, sizeof(int), table * BYTE_PER_BLOCK + cnt * sizeof(int));
    }
    else if (blkno < SECOND_LEVEL_INDIRECT_INDEX_NUM)
    {
        int num = blkno - FIRST_LEVEL_INDIRECT_INDEX_NUM; //在二级索引中已经占据了几个块
        int index = num / (128 * 128); //第一张间接索引表是否已经满了
        int cnt_1 = num % (128 * 128) / 128; //在哪张直接索引表
        int cnt_2 = num % 128; //在直接索引表中位置

        int table_1 = d_addr[index + DIRECT_INDEX_NUM + 2]; //间接索引块块号
        int table_2; //直接索引块块号，需要从磁盘中读出
        readDisk(disk, &table_2, sizeof(int), table_1 * BYTE_PER_BLOCK + cnt_1 * sizeof(int));
        readDisk(disk, &ret, sizeof(int), table_2 * BYTE_PER_BLOCK + cnt_2 * sizeof(int));
    }
    return ret;
}

void Inode::releaseAllBlk(std::fstream& disk, SuperBlock& s)
{
    //首先释放所有数据块
    // 目录一律只占一个数据块(mkdir 只分配一块, 满了也不扩), 它的 d_size 记的是
    // "子文件数 × 2"(见 addSubDirSize), 与盘块数无关, 所以目录固定按 1 块回收。
    int blk_num = d_mode == FILE_MODE::dir_file ? 1 : blocksForFileContent(d_size);
    // 普通文件至少有首块: 目录项(文件名)写在文件首块开头, 0 字节的文件也不例外。
    // 这种情况按 d_size 算出来是 0 块, 靠 d_addr[0] 落在数据区补回下限。
    if (blk_num == 0 && d_addr[0] >= FILE_BLOCK_START)
        blk_num = 1;
    for (int i = 0; i < blk_num; i++)
    {
        s.releaseBlk(disk, BMap(disk, i));
    }
    //其次释放所有索引块
    //计算用了几个索引块
    //阈值取 >=: appendBlk 对索引块是"预分配"的 —— 追加第 6 个直接块时
    //(此时 blk_num == 5) 就把一级索引块 d_addr[6] 备好了, 追加第 134 个块时备好
    //d_addr[7], 第 262 个块备好 d_addr[8], 第 16646 个块备好 d_addr[9]。所以恰好
    //6 / 134 / 262 / 16646 块的文件也各有一个已经分配出去的索引块, 要一并回收。
    const int INDEX_PER_TABLE = BYTE_PER_BLOCK / sizeof(int);   // 一个索引块装得下几个块号
    if (blk_num >= DIRECT_INDEX_NUM)
    {
        s.releaseBlk(disk, d_addr[6]);
    }
    if (blk_num >= DIRECT_INDEX_NUM + INDEX_PER_TABLE)
    {
        s.releaseBlk(disk, d_addr[7]);
    }
    if (blk_num >= FIRST_LEVEL_INDIRECT_INDEX_NUM)
    {
        // 二级间接索引块 d_addr[8] 下面还挂着一串预分配的直接索引表, 要连它们一起回收。
        // 也是 appendBlk 预分配的: blk_num == 261 时(一级间接的两张表刚好填满)分配
        // d_addr[8], 并顺手把它的第 0 项备好; 此后每填满一张表(cnt_2 == 127)再预分配
        // 下一张(写进 d_addr[8][cnt_1 + 1])。被预分配的表是空的, 光看 d_addr[8] 的
        // 内容数不出"一共备了几张", 只能按已经用掉的二级索引块数反推: 进入二级索引
        // 后每用满 128 块就多备一张, 首张是第 262 块时备下的。
        int sub_num = 1 + (blk_num - FIRST_LEVEL_INDIRECT_INDEX_NUM) / INDEX_PER_TABLE;
        if (sub_num > INDEX_PER_TABLE)   // d_addr[8] 里只有 128 个表项
            sub_num = INDEX_PER_TABLE;
        // 表号先读进局部数组, 再释放 d_addr[8]: 空闲组满时 releaseBlk 会把当前 s_free
        // 表整个写进被释放的那一块, 释放完再去读就读不到了。表号一旦读出来, 后续释放
        // 哪一块都不会影响它们 —— 每次只写被释放的那一块自己。
        int sub_table[INDEX_PER_TABLE];
        for (int i = 0; i < sub_num; i++)
            readDisk(disk, &sub_table[i], sizeof(int), d_addr[8] * BYTE_PER_BLOCK + i * sizeof(int));
        s.releaseBlk(disk, d_addr[8]);
        for (int i = 0; i < sub_num; i++)
        {
            s.releaseBlk(disk, sub_table[i]);
        }
    }
    // d_addr[9] 那档要 16646 块(约 8.5 MB)才开始用, 4 MB 的盘到不了; 且 appendBlk
    // 分配 d_addr[9] 时没有给它预分配子表, 没有额外的东西要回收
    if (blk_num >= SECOND_LEVEL_INDIRECT_INDEX_NUM)
    {
        s.releaseBlk(disk, d_addr[9]);
    }
}