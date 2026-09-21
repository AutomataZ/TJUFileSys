#include "framework.h"
#include "fixture.h"
#include <vector>
#include <set>

// 套件 2: 6-2-2 混合索引
//
// d_addr[0..5] 直接索引, d_addr[6..7] 一级间接, d_addr[8..9] 二级间接。
// appendBlk 用 blocksForFileContent(d_size) 作为"下一个空闲槽位"的下标,
// 也就是说它信任调用方维持"已分配数据块数 == blocksForFileContent(d_size)"
// 这一不变式 —— appendBlk 自身不修改 d_size。
//
// 因此测试里每追加一块就把 d_size 推进到"装下这么多块"的字节数, 与 newFile
// 的分配循环保持一致。装下 B 块的文件是 B*512 - 16 字节: 每块开头那 16 字节
// 是目录项, 不装内容。

namespace {

// 往 inode 中追加 n 个数据块, 返回分配到的块号
std::vector<int> appendBlocks(std::fstream& disk, SuperBlock& s, Inode& inode,
                              int inode_index, int n)
{
    std::vector<int> blks;
    for (int i = 0; i < n; i++)
    {
        int blk = s.distributeBlk(disk);
        blks.push_back(blk);
        inode.appendBlk(disk, s, inode_index, blk);
        // 推进 d_size, 下次落到下一槽位。按当前已有的块数递推, 便于分多次调用
        int have = blocksForFileContent(inode.getSize());
        inode.changeSize((have + 1) * BYTE_PER_BLOCK - FILE_DIR_SIZE - inode.getSize());
    }
    return blks;
}

// 抽干内存里的空闲表, 得到"此刻空闲链上还剩哪几块"。
// distributeBlk 只读盘、不写盘, 所以抽完把 sblk 整个按值还原即可, 磁盘不受影响。
// 循环上界是防御性的: 万一空闲链里被塞进了假块号(比如 -1)绕成环, 逐个弹出会停不下来。
std::set<int> freeBlockSet(std::fstream& disk, SuperBlock& s)
{
    SuperBlock keep = s;
    std::set<int> got;
    for (int i = 0; i < TOTAL_BLOCK_NUM * 2; i++)
    {
        int blk = s.distributeBlk(disk);
        if (blk < 0)
            break;
        got.insert(blk);
    }
    s = keep;
    return got;
}

// 两个空闲块集合的差异, 供断言消息用:
// "少了" = 回收时漏掉了, 泄漏; "多了" = 往空闲链里塞了没分配过的块
std::string freeSetDiff(const std::set<int>& expect, const std::set<int>& actual)
{
    std::string r;
    for (std::set<int>::const_iterator it = expect.begin(); it != expect.end(); ++it)
        if (!actual.count(*it))
            r += " 少了 " + ut::to_str(*it);
    for (std::set<int>::const_iterator it = actual.begin(); it != actual.end(); ++it)
        if (!expect.count(*it))
            r += " 多了 " + ut::to_str(*it);
    return r.empty() ? "(无差异)" : r;
}

} // namespace

UT_TEST(inode, fresh_inode_state, "新建 inode 的初始状态")
{
    // inode 大小必须正好 64 字节, 否则一个盘块装不下 8 个, 整个 inode 区布局都会错
    UT_CHECK_EQ(sizeof(Inode), INODE_SIZE);

    Inode inode;
    UT_CHECK_EQ(inode.getMode(), 0);
    UT_CHECK_EQ(inode.getSize(), 0);
    UT_CHECK_MSG(inode.isEmpty(), "新建 inode 的 d_mode 为 0, 应被判为空闲");
}

UT_TEST(inode, empty_inode_bmap_is_minus_one, "未分配任何块的 inode 其 BMap 返回 -1")
{
    FsFixture f;
    Inode inode;
    // d_addr 全为 -1 时, 直接索引分支原样返回 d_addr[blkno]
    UT_CHECK_EQ(inode.BMap(f.disk, 0), -1);
    UT_CHECK_EQ(inode.BMap(f.disk, 5), -1);
}

UT_TEST(inode, direct_index_slots, "前 6 个块落在直接索引 d_addr[0..5]")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);          // 先把空 inode 落盘

    std::vector<int> blks = appendBlocks(f.disk, f.sblk, inode, 0, 6);

    readInodeMirror(f.disk, 0, m);               // 不重新快照, 验证 appendBlk 自己写盘的内容
    for (int i = 0; i < 6; i++)
        UT_CHECK_EQ(m.d_addr[i], blks[i]);

    // 6 块装得下 6*512 - 16 字节: 首块开头 16 字节是目录项, 不装内容
    UT_CHECK_EQ(inode.getSize(), 6 * BYTE_PER_BLOCK - FILE_DIR_SIZE);
}

UT_TEST(inode, seventh_block_goes_indirect, "第 7 个块进入一级间接索引表")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    std::vector<int> blks = appendBlocks(f.disk, f.sblk, inode, 0, 7);

    readInodeMirror(f.disk, 0, m);
    for (int i = 0; i < 6; i++)
        UT_CHECK_EQ(m.d_addr[i], blks[i]);

    // d_addr[6] 是一级索引块, 它的第 0 项应指向第 7 个数据块
    const int table = m.d_addr[6];
    UT_CHECK_MSG(table >= FILE_BLOCK_START, "一级索引块号应指向数据区");
    int entry0 = -1;
    readDisk(f.disk, &entry0, sizeof(int), table * BYTE_PER_BLOCK);
    UT_CHECK_EQ(entry0, blks[6]);
}

UT_TEST(inode, bmap_roundtrip_from_disk, "从磁盘重读 inode 后 BMap 能逐块反查 (10 块)")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    const int N = 10;
    std::vector<int> blks = appendBlocks(f.disk, f.sblk, inode, 0, N);

    // 关键: 换一个全新的 Inode 对象, 内容完全来自磁盘,
    // 这样 BMap 走的是磁盘上的 d_addr 与索引表, 而非内存中的对象
    Inode reloaded;
    readDisk(f.disk, &reloaded, sizeof(Inode), inodeOffset(0));

    for (int i = 0; i < N; i++)
        UT_CHECK_EQ(reloaded.BMap(f.disk, i), blks[i]);
}

UT_TEST(inode, bmap_roundtrip_large, "跨一级与二级间接的索引反查 (270 块)")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    const int N = 270;   // 6 直接 + 256 一级间接 + 8 二级间接
    std::vector<int> blks = appendBlocks(f.disk, f.sblk, inode, 0, N);

    Inode reloaded;
    readDisk(f.disk, &reloaded, sizeof(Inode), inodeOffset(0));

    bool all_ok = true;
    int first_bad = -1;
    for (int i = 0; i < N; i++)
    {
        if (reloaded.BMap(f.disk, i) != blks[i])
        {
            all_ok = false;
            first_bad = i;
            break;
        }
    }
    UT_CHECK_MSG(all_ok, first_bad < 0 ? std::string("")
                 : ("第 " + ut::to_str(first_bad) + " 个块的索引反查失败"));
}

UT_TEST(inode, slot_index_depends_on_size, "appendBlk 的槽位下标由 d_size 决定")
{
    // 不推进 d_size 时, 每次 append 都落在同一个槽位, 后写的覆盖先写的。
    // 这解释了为什么调用方必须同步维护 d_size
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    int b1 = f.sblk.distributeBlk(f.disk);
    int b2 = f.sblk.distributeBlk(f.disk);
    inode.appendBlk(f.disk, f.sblk, 0, b1);
    inode.appendBlk(f.disk, f.sblk, 0, b2);   // d_size 仍为 0

    readInodeMirror(f.disk, 0, m);
    UT_CHECK_EQ(m.d_addr[0], b2);
    UT_CHECK_EQ(m.d_addr[1], -1);
}

// 恰好 6 个数据块的文件, 其一级索引块也应随文件删除一起回收
//
// appendBlk 在追加第 6 个直接块时会顺手分配 d_addr[6] 作为一级索引块, 也就是
// "文件占几块"到达 6 时 d_addr[6] 就已经存在了。所以 releaseAllBlk 释放索引块的
// 阈值必须取 >= : 少了这个等号, 恰好 6 块 (3072 字节) 的文件就留着一个已经分配
// 出去的索引块不还, 永久泄漏。
//
// 后面几个索引块阈值同理, 见下面两个用例。
UT_TEST(inode, release_6block_file_frees_index, "删除恰好 6 块的文件应回收其一级索引块")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    std::vector<int> blks = appendBlocks(f.disk, f.sblk, inode, 0, 6);
    readInodeMirror(f.disk, 0, m);

    const int index_blk = m.d_addr[6];
    UT_CHECK_MSG(index_blk >= FILE_BLOCK_START,
                 "追加第 6 个直接块时应已预分配一级索引块 d_addr[6]");

    // 记录回收前空闲链顶端, 回收后该索引块应回到顶端
    SuperBlockMirror before = superBlockOf(f.disk, f.sblk);
    const int top_before = before.s_nfree;

    inode.releaseAllBlk(f.disk, f.sblk);

    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);
    UT_CHECK_MSG(after.s_nfree > top_before,
                 "releaseAllBlk 之后空闲块数应增加 (索引块被回收)");

    bool found = false;
    for (int i = 0; i < after.s_nfree; i++)
        if (after.s_free[i] == index_blk) { found = true; break; }
    UT_CHECK_MSG(found, "一级索引块 " + ut::to_str(index_blk) + " 未被回收, 已泄漏");
}

// 恰好 134 块的文件, 两张一级索引表都应随文件删除一起回收
//
// 索引块是预分配的, 释放阈值因此逐个都取 >= :
//
//     blk_num >= 6                          d_addr[6]
//     blk_num >= 6 + 128                    d_addr[7]
//     blk_num >= 6 + 2 * 128                d_addr[8]
//     blk_num >= 6 + 2 * 128 + 128 * 128    d_addr[9]
//
// 追加第 134 个块时 (blk_num == 133, cnt == 127) appendBlk 就把第二张表
// d_addr[7] 备好了, 所以恰好 134 块 (68608 字节) 的文件持有两张一级索引表,
// 一张都不能漏。
//
// d_addr[9] 那一档 (16646 块) 超出 4 MB 镜像的容量, 没有单独建用例; 262 块
// 那一档还额外牵扯二级间接索引自身的预分配, 见本文件末尾的用例。
UT_TEST(inode, release_134block_file_frees_second_index_table,
        "删除恰好 134 块的文件应回收两张一级索引表")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);

    const int N = 134;   // 6 直接 + 128 一级 + 进入第二张表的第 134 块
    appendBlocks(f.disk, f.sblk, inode, 0, N);
    readInodeMirror(f.disk, 0, m);

    const int table_1 = m.d_addr[6];
    const int table_2 = m.d_addr[7];
    // 这一条同时是下面两条断言的前提: 若 appendBlk 其实没有预分配 d_addr[7],
    // 把它当索引块释放就会往空闲链里塞进一个 -1
    UT_CHECK_MSG(table_1 >= FILE_BLOCK_START && table_2 >= FILE_BLOCK_START,
                 "134 块的文件应持有两张一级索引表, 实际 d_addr[6]=" +
                 ut::to_str(table_1) + " d_addr[7]=" + ut::to_str(table_2));
    UT_CHECK_MSG(table_1 != table_2, "两张一级索引表应是两个不同的盘块");

    inode.releaseAllBlk(f.disk, f.sblk);

    // 逐个弹出而不是在 s_free 里查找: 这里要回收 136 个块, 必然跨过成组链接法的
    // 分组边界, 块可能已经被写进上一组的索引块里, 只看当前这张表会漏判
    bool got_1 = false, got_2 = false;
    for (int i = 0; i < 4 * N && !(got_1 && got_2); i++)
    {
        int blk = f.sblk.distributeBlk(f.disk);
        if (blk < 0)
            break;
        if (blk == table_1) got_1 = true;
        if (blk == table_2) got_2 = true;
    }
    UT_CHECK_MSG(got_1, "一级索引表 " + ut::to_str(table_1) + " 未被回收, 已泄漏");
    UT_CHECK_MSG(got_2, "一级索引表 " + ut::to_str(table_2) + " 未被回收, 已泄漏");
}

// 二级间接索引里预分配的子表也要随文件删除一起回收
//
// 追加第 262 个块时 (blk_num == 261, 一级索引的第二张表恰好写满) appendBlk 连做
// 两件事: 分配一块作为二级间接索引块 d_addr[8], 再分配一块写进它的第 0 项, 作为
// 第一张直接索引表。两块都是预分配, 回收时一块都不能少 —— 否则每建一个 ≥ 262 块
// (131 KB) 的文件再删掉, 就少一块盘块。上面那条用例只做到 134 块, 再往上就要
// 牵扯这些子表了。
//
// 回收时表号必须"先读后放": 空闲组满时 releaseBlk 会把 s_free 表整个写进被释放的
// 那一块, 先释放 d_addr[8] 就把表号擦掉了。
//
// 判别标准取"回收前后的空闲块集合完全相同", 而不是仅仅"子表号又出现了":
// 前者同时挡住漏回收(少一块)和把没分配过的表项当块号塞进空闲链(多一块)。
UT_TEST(inode, release_262block_file_frees_second_level_subtable,
        "删除 262 块的文件应回收二级间接索引下预分配的子表")
{
    FsFixture f;
    Inode inode;
    InodeMirror m;
    snapshotInode(f.disk, inode, 0, m);
    const std::set<int> free_before = freeBlockSet(f.disk, f.sblk);

    const int N = 262;   // 6 直接 + 256 一级间接 (两张表) + 进入二级索引的第一块
    appendBlocks(f.disk, f.sblk, inode, 0, N);
    readInodeMirror(f.disk, 0, m);

    const int table_2 = m.d_addr[8];
    UT_CHECK_MSG(table_2 >= FILE_BLOCK_START,
                 "262 块的文件应持有二级间接索引块 d_addr[8], 实际 " + ut::to_str(table_2));

    int sub_table = -1;
    if (table_2 >= FILE_BLOCK_START)
        readDisk(f.disk, &sub_table, sizeof(int), table_2 * BYTE_PER_BLOCK);
    UT_CHECK_MSG(sub_table >= FILE_BLOCK_START,
                 "d_addr[8] 的第 0 项应是预分配的子表, 实际 " + ut::to_str(sub_table));

    inode.releaseAllBlk(f.disk, f.sblk);

    const std::set<int> free_after = freeBlockSet(f.disk, f.sblk);
    UT_CHECK_MSG(free_after.count(sub_table) > 0,
                 "二级间接索引的子表 " + ut::to_str(sub_table) + " 未被回收, 已泄漏");
    UT_CHECK_MSG(free_after.count(table_2) > 0,
                 "二级间接索引块 " + ut::to_str(table_2) + " 未被回收, 已泄漏");
    UT_CHECK_MSG(free_after == free_before,
                 "回收后的空闲块集合与建文件之前不一致:" +
                 freeSetDiff(free_before, free_after));
}

// 二级间接索引下的子表不止一张: d_addr[8] 的第 0 项是第 262 块时备下的, 之后每填满
// 一张 (cnt_2 == 127) 再备一张, 写进 d_addr[8][cnt_1 + 1]。备下的表是空的, 从内容上
// 数不出张数, 只能按已经用掉的二级索引块数反推 —— 1 + (N - 262) / 128。
//
// 这条式子两侧都要测: 恰好 1 张 / 2 张 (389 / 517 块) 时不能多回收一张, 否则就是把
// d_addr[8] 里从没被写过的表项当成块号塞进空闲链; 390 / 518 块时又必须把新备的那张
// 也收回来。判别标准同样是"回收前后的空闲块集合完全相同"。
UT_TEST(inode, release_large_file_frees_every_subtable,
        "删除大文件应回收二级间接索引下的每一张子表, 且不多收")
{
    struct Case { int blocks; int sub_tables; };
    const Case cases[] = {{262, 1}, {389, 1}, {390, 2}, {517, 2}, {518, 3}};

    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        const int N = cases[k].blocks;
        FsFixture f;
        Inode inode;
        InodeMirror m;
        snapshotInode(f.disk, inode, 0, m);
        const std::set<int> free_before = freeBlockSet(f.disk, f.sblk);

        appendBlocks(f.disk, f.sblk, inode, 0, N);
        readInodeMirror(f.disk, 0, m);
        const int table_2 = m.d_addr[8];

        // appendBlk 备下的这几张表, 回收后应当一张不少地回到空闲链
        std::vector<int> subs;
        if (table_2 >= FILE_BLOCK_START)
            for (int i = 0; i < cases[k].sub_tables; i++)
            {
                int blk = -1;
                readDisk(f.disk, &blk, sizeof(int), table_2 * BYTE_PER_BLOCK + i * sizeof(int));
                subs.push_back(blk);
            }

        inode.releaseAllBlk(f.disk, f.sblk);
        const std::set<int> free_after = freeBlockSet(f.disk, f.sblk);

        const std::string tag = ut::to_str(N) + " 块的文件 (d_addr[8] 下应有 " +
                                ut::to_str(cases[k].sub_tables) + " 张子表): ";
        UT_CHECK_MSG(table_2 >= FILE_BLOCK_START, tag + "没有拿到二级间接索引块 d_addr[8]");
        UT_CHECK_MSG((int)subs.size() == cases[k].sub_tables,
                     tag + "实际只数到 " + ut::to_str(subs.size()) + " 项");
        for (std::size_t i = 0; i < subs.size(); i++)
            UT_CHECK_MSG(free_after.count(subs[i]) > 0,
                         tag + "第 " + ut::to_str(i) + " 张子表 " +
                         ut::to_str(subs[i]) + " 未被回收, 已泄漏");
        UT_CHECK_MSG(free_after == free_before,
                     tag + "回收后的空闲块集合与建文件之前不一致:" +
                     freeSetDiff(free_before, free_after));
    }
}
