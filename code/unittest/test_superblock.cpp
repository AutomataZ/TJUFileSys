#include "framework.h"
#include "fixture.h"
#include <vector>
#include <set>

// 套件 1: 成组链接法 (superblock 直接管理的空闲盘块)
//
// 盘块的分配与回收是全文件系统最容易出错的部分: 直接管理区与索引块之间
// 的切换、跨组边界、末组的特殊性, 都靠 superblock.cpp 里一小段指针算术维持。

UT_TEST(superblock, fields_after_format, "格式化后 superblock 各字段正确")
{
    FsFixture f;
    SuperBlockMirror m = superBlockOf(f.disk, f.sblk);

    UT_CHECK_EQ(m.s_isize, INODE_BLOCK_NUM);       // 190
    UT_CHECK_EQ(m.s_fsize, FILE_BLOCK_NUM);        // 8000
    UT_CHECK_EQ(m.s_nfree, 100);

    // 根目录已经占用了 99 号 inode: SuperBlock() 按 0..99 顺序压栈,
    // distributeInode 从栈顶弹出, 于是根目录拿到的是 99 而不是 0
    UT_CHECK_EQ(m.s_ninode, SUPER_CONTROL_INODE_NUM - 1);

    bool inode_stack_ok = true;
    for (int i = 0; i < SUPER_CONTROL_INODE_NUM - 1; i++)
        if (m.s_inode[i] != i) { inode_stack_ok = false; break; }
    UT_CHECK_MSG(inode_stack_ok, "根目录占掉 99 号后, 栈内应剩 0..98");

    UT_CHECK_EQ(m.s_inode[SUPER_CONTROL_INODE_NUM - 1], INODE_IS_OCCUPIED);
}

UT_TEST(superblock, free_list_after_format, "格式化后空闲块表内容正确")
{
    FsFixture f;
    SuperBlockMirror m = superBlockOf(f.disk, f.sblk);

    // 根目录占用了块 192 这个索引块, 直接管理区因此是 193..292
    UT_CHECK_EQ(m.s_free[0], 193);
    UT_CHECK_EQ(m.s_free[99], 292);

    bool ok = true;
    for (int i = 0; i < 100; i++)
        if (m.s_free[i] != 193 + i) { ok = false; break; }
    UT_CHECK_MSG(ok, "空闲块表应为 193..292 连续递增");
}

UT_TEST(superblock, distribute_returns_distinct_blocks, "连续分配 200 个盘块互不重复")
{
    FsFixture f;
    std::set<int> seen;
    bool all_distinct = true;

    for (int i = 0; i < 200; i++)
    {
        int blk = f.sblk.distributeBlk(f.disk);
        if (blk < 0 || seen.count(blk)) { all_distinct = false; break; }
        seen.insert(blk);
    }
    UT_CHECK_MSG(all_distinct, "200 次分配中出现了重复或非法块号");
    UT_CHECK_EQ((int)seen.size(), 200);
}

UT_TEST(superblock, release_then_distribute_is_lifo, "释放后重新分配拿回同一块 (LIFO)")
{
    FsFixture f;

    int first = f.sblk.distributeBlk(f.disk);
    f.sblk.releaseBlk(f.disk, first);
    int again = f.sblk.distributeBlk(f.disk);

    // releaseBlk 压栈到 s_free 顶端, distributeBlk 从顶端取, 故应拿回同一块
    UT_CHECK_EQ(again, first);
}

UT_TEST(superblock, refill_across_group_boundary, "跨组边界时从索引块补充空闲块表")
{
    FsFixture f;
    SuperBlockMirror before = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(before.s_nfree, 100);

    // 把直接管理区恰好耗尽: 这一批的最后一次分配会触发读索引块
    std::vector<int> got;
    for (int i = 0; i < 100; i++)
        got.push_back(f.sblk.distributeBlk(f.disk));

    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);

    // 第二批来自块 193 的索引块, 内容为 293..392
    UT_CHECK_EQ(after.s_nfree, 100);
    UT_CHECK_EQ(after.s_free[0], 293);
    UT_CHECK_EQ(after.s_free[99], 392);

    // 被当作索引块读掉的 193 应当是这批里最后一个被分配的
    UT_CHECK_EQ(got[99], 193);
}

UT_TEST(superblock, release_fills_then_links_group, "回收盘块填满一组后写出新的索引块")
{
    FsFixture f;
    std::vector<int> taken;
    for (int i = 0; i < 100; i++)
        taken.push_back(f.sblk.distributeBlk(f.disk));

    // s_free 是栈, distributeBlk 弹栈顶, 所以 292 先出、193 后出
    UT_CHECK_EQ(taken[0], 292);
    UT_CHECK_EQ(taken[99], 193);

    SuperBlockMirror drained = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(drained.s_nfree, 100);   // 已从索引块补充为 293..392

    // 此时表是满的, 第一次回收就会走"表满 ⟶ 把整表写进被回收的那个盘块"分支,
    // 因此充当新索引块的是 taken[0] 而不是最后一个
    for (int i = 0; i < 100; i++)
        f.sblk.releaseBlk(f.disk, taken[i]);

    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(after.s_nfree, 100);
    UT_CHECK_EQ(after.s_free[0], taken[0]);     // 新索引块 = 第一次回收的 292
    UT_CHECK_EQ(after.s_free[99], taken[99]);   // 之后依次压入 291..193

    // 292 号盘块应当已经变成了索引块: 字 0 记数量, 字 1..100 记块号
    const int addr = taken[0] * BYTE_PER_BLOCK;
    int word0 = 0;
    readDisk(f.disk, &word0, sizeof(int), addr);
    UT_CHECK_EQ(word0, 100);

    bool index_ok = true;
    for (int i = 0; i < 100; i++)
    {
        int v = 0;
        readDisk(f.disk, &v, sizeof(int), addr + (i + 1) * sizeof(int));
        if (v != 293 + i) { index_ok = false; break; }
    }
    UT_CHECK_MSG(index_ok, "新索引块内的块号表应为 293..392");
}

// 最后一组的索引块 (7993) 排布与别的索引块不同: 字 0 记 99, 字 1 是结束标志 0,
// 字 2..100 才是那 99 个块号 8093..8191 —— 整体多了一个字。distributeBlk 的补充
// 循环因此要把这个偏移算在地址上, 而不是当成"要跳过的槽位":
//
//     int ini = s_nfree == BLOCK_IN_GROUP - 1 ? 1 : 0;
//     for (int i = 0; i < s_nfree; i++)  readDisk(..., (ini + 1 + i) * sizeof(int) + addr);
//
// 循环次数由 s_nfree 决定, 与 ini 无关: 最后一组要填满 s_free[0..98], 顶端
// s_free[98] 落在 8191。少填一个的话, 这组装进来的同时就已经"看起来是空的" ——
// 下一次分配直接返回 -1, 余下的块一个都发不出去。
//
// 下面用镜像结构直接在磁盘上摆出"即将切到最后一组"的状态, 无需真的分配
// 8000 个盘块。
UT_TEST(superblock, last_group_refill_covers_all, "最后一组补充时应填满 99 个空闲块")
{
    FsFixture f;

    SuperBlockMirror m = superBlockOf(f.disk, f.sblk);
    // 构造: 直接管理区只剩 7993 这一个块, 它就是最后一个索引块。
    // 其余槽位按自然弹空后的样子置 -1 (每次分配都会把腾出的槽位写成 -1)
    for (int i = 0; i < SUPER_CONTROL_FILE_NUM; i++)
        m.s_free[i] = FILE_BLOCK_IS_OCCUPIED;
    m.s_nfree = 1;
    m.s_free[0] = 7993;
    loadSuperBlock(f.disk, m, f.sblk);

    int blk = f.sblk.distributeBlk(f.disk);
    UT_CHECK_EQ(blk, 7993);

    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);

    UT_CHECK_EQ(after.s_nfree, 99);        // 最后一组共 99 个空闲块
    UT_CHECK_EQ(after.s_free[0], 8093);
    UT_CHECK_EQ(after.s_free[97], 8190);
    UT_CHECK_EQ(after.s_free[98], 8191);   // 顶端是末组最后一个块
}

// 自然耗尽整条空闲链, 观察末组的表现
//
// 8000 个数据块里应发出 7999 个: 少的那个是 192 —— 格式化时已经分给根目录当
// 首块了, 它本就不在空闲链里。末组若没被填满, 链会在 7993 处提前断掉,
// 8093..8191 这 99 个块一个都发不出去, 而 s_nfree 显示的却是 99, 调用方会把它
// 当成"盘满"。分配过程中不会出现重复块号: 每个槽位被弹走时都写成了
// FILE_BLOCK_IS_OCCUPIED。
UT_TEST(superblock, drain_entire_free_list, "耗尽空闲链应发出除根目录盘块外的全部盘块")
{
    FsFixture f;

    std::vector<int> got;
    std::set<int> seen;
    bool duplicate = false;
    int blk;
    while ((blk = f.sblk.distributeBlk(f.disk)) != -1)
    {
        if (seen.count(blk)) { duplicate = true; break; }
        seen.insert(blk);
        got.push_back(blk);
    }

    UT_CHECK_MSG(!duplicate, "分配过程中出现了重复块号");
    UT_CHECK_EQ((int)got.size(), FILE_BLOCK_NUM - 1);   // 8000 块中 192 已归根目录
    UT_CHECK_MSG(seen.count(FILE_BLOCK_START) == 0,
                 "192 已经是根目录的盘块, 不该再由空闲链发一次");
    UT_CHECK_MSG(seen.count(FILE_BLOCK_START + FILE_BLOCK_NUM - 1) == 1,
                 "最后一个盘块 8191 从未被分配, 已泄漏");

    // 7999 个互不相同的块号, 若全部落在 193..8191 这 7999 个位置里, 就是恰好铺满
    bool in_range = true;
    for (std::size_t i = 0; i < got.size(); i++)
    {
        if (got[i] < FILE_BLOCK_START + 1 || got[i] > FILE_BLOCK_START + FILE_BLOCK_NUM - 1)
            in_range = false;
    }
    UT_CHECK_MSG(in_range, "发出来的块号应恰好铺满 193..8191");
}
