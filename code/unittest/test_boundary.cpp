#include "framework.h"
#include "fixture.h"
#include "filedir.h"
#include <set>
#include <string>
#include <vector>

// 套件 5: 边界 / 压力 / 数据完整性
//
// 这一层不再孤立地测某个数据结构, 而是走完整的调用链
// (fcreat → fopen → fwrite → flseek → fread → fdelete),
// 断言的重点是"写进去的字节能原样读回来", 而不只是长度对不对。
//
// 块内布局: 每个文件的第一个数据盘块开头是 sizeof(FileDir) = 16 字节的目录项,
// 文件内容从块内偏移 16 开始。因此"从 0 偏移写 N 字节"实际占用块内 [16, 16+N),
// 一旦 16 + N 超过块边界, 就会走 readWriteFile 的多块分支。

namespace {

const int DIR_HDR = static_cast<int>(sizeof(FileDir));   // 16

// 生成可辨认的内容: 用质数取模, 避免周期性重复掩盖错位
std::string pattern(int n)
{
    std::string s;
    for (int i = 0; i < n; i++)
        s += static_cast<char>(i % 251);
    return s;
}

int currentDirInode(FsFixture& f)
{
    return f.i_table.inode[f.i_table.getCurrentDir()].i_number;
}

int firstInode(FsFixture& f)
{
    int c = getCurrentBlk(f.disk, f.i_table);
    int off = c * BYTE_PER_BLOCK + DIR_HDR;
    int16_t v = 0;
    readDisk(f.disk, &v, sizeof(int16_t), off);
    return v;
}

// 当前目录下第 k 个子文件的 inode 号 (k 从 0 开始)
int dirEntryInode(FsFixture& f, int k)
{
    int c = getCurrentBlk(f.disk, f.i_table);
    int16_t v = 0;
    readDisk(f.disk, &v, sizeof(int16_t), c * BYTE_PER_BLOCK + DIR_HDR + k * sizeof(int16_t));
    return v;
}

// 数据区每一块的快照, 用于事后比对哪些盘块被写脏。直接读镜像文件, 绕开缓存。
std::vector<std::string> dumpDataArea(std::fstream& disk)
{
    std::vector<std::string> v;
    v.reserve(FILE_BLOCK_NUM);
    for (int i = 0; i < FILE_BLOCK_NUM; i++)
        v.push_back(readBlockBytes(disk, FILE_BLOCK_START + i, BYTE_PER_BLOCK));
    return v;
}

// 建文件 → 打开 → 从 offset 写 size 字节 → 再读回来, 返回读到的内容
std::string writeThenRead(FsFixture& f, const std::string& name, int decl_size,
                          int offset, int size, int* ptr_after = nullptr)
{
    fcreat(name, decl_size, f.disk, f.sblk, f.i_table, f.f_table);
    const int ino = firstInode(f);
    fopen(name, f.disk, f.sblk, f.i_table, f.f_table);
    if (offset > 0)
        flseek(name, offset, f.disk, f.sblk, f.i_table, f.f_table);

    std::string buf = pattern(size);
    fwrite(name, buf, size, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    if (ptr_after)
        *ptr_after = f.f_table.getOffset(ino);   // 写完后的读写指针

    flseek(name, offset, f.disk, f.sblk, f.i_table, f.f_table);
    return fread(name, size, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
}

} // namespace

// ---- 单块内的往返 (这些尺寸下 readWriteFile 的算术是自洽的) ----

UT_TEST(boundary, single_block_roundtrip, "单块内的各种长度都能原样读回")
{
    const int sizes[] = {1, 10, 100, 300, 495, 496};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
    {
        FsFixture f;
        const int n = sizes[k];
        std::string got = writeThenRead(f, "t", 2000, 0, n);
        UT_CHECK_MSG(got.size() == static_cast<std::size_t>(n),
                     "长度 " + ut::to_str(n) + " 实际读到 " + ut::to_str(got.size()));
        UT_CHECK_MSG(got == pattern(n), "长度 " + ut::to_str(n) + " 的内容不一致");
    }
}

UT_TEST(boundary, multi_block_roundtrip, "跨块写入能原样读回")
{
    // 16 + size 不超过 ceil(size/512)*512, 这些尺寸走多块分支是自洽的
    const int sizes[] = {513, 600, 1000, 1008};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
    {
        FsFixture f;
        const int n = sizes[k];
        int ptr = -1;
        std::string got = writeThenRead(f, "t", 4000, 0, n, &ptr);
        UT_CHECK_MSG(got == pattern(n), "长度 " + ut::to_str(n) + " 的内容不一致");
        UT_CHECK_EQ(ptr, n);   // 读写指针应正好前进 n
    }
}

UT_TEST(boundary, write_exactly_to_block_end, "恰好写到块尾不出错")
{
    // 偏移 484 + 12 字节 = 块内 500..511, 正好压着块边界收尾
    FsFixture f;
    std::string got = writeThenRead(f, "t", 4000, 484, 12);
    UT_CHECK_EQ(got, pattern(12));
}

UT_TEST(boundary, in_place_overwrite, "原地覆盖写后读到的是新内容")
{
    FsFixture f;
    fcreat("t", 1000, f.disk, f.sblk, f.i_table, f.f_table);
    const int ino = firstInode(f);
    fopen("t", f.disk, f.sblk, f.i_table, f.f_table);

    std::string first = pattern(300);
    fwrite("t", first, 300, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    flseek("t", 0, f.disk, f.sblk, f.i_table, f.f_table);

    std::string second(300, 'X');
    fwrite("t", second, 300, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    flseek("t", 0, f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(fread("t", 300, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr), second);
    (void)ino;
}

// ---- 目录树 ----

UT_TEST(boundary, deep_directory_tree, "五层目录逐级进出")
{
    FsFixture f;
    const char* names[] = {"a", "b", "c", "d", "e"};

    std::string path = "/";
    for (int i = 0; i < 5; i++)
    {
        mkdir(names[i], f.disk, f.sblk, f.i_table, f.f_table);
        cd(names[i], f.disk, f.sblk, f.i_table, f.f_table);
        path += std::string(names[i]) + "/";
        UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), path);
        UT_CHECK_EQ(f.i_table.size(), i + 2);   // 根目录 + 沿途每一层
    }

    // 在最深处建文件, 确认不会串到上层
    fcreat("deep", 10, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);

    for (int i = 4; i >= 0; i--)
    {
        cd("..", f.disk, f.sblk, f.i_table, f.f_table);
        path.erase(path.size() - 2);            // 去掉 "x/"
        UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), path);
    }

    UT_CHECK_EQ(f.i_table.size(), 1);
    // 根目录下仍然只有 "a" 这一项; 逐级 cd .. 不应动到父目录的目录项数
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);
}

UT_TEST(boundary, sibling_directories_are_isolated, "同级目录的内容互不干扰")
{
    FsFixture f;
    mkdir("left", f.disk, f.sblk, f.i_table, f.f_table);
    mkdir("right", f.disk, f.sblk, f.i_table, f.f_table);

    cd("left", f.disk, f.sblk, f.i_table, f.f_table);
    fcreat("only_left", 50, f.disk, f.sblk, f.i_table, f.f_table);
    cd("..", f.disk, f.sblk, f.i_table, f.f_table);

    cd("right", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 0);

    fcreat("only_right", 50, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);
}

// ---- 多文件并发打开 ----

UT_TEST(boundary, multiple_open_files_independent, "多文件同时打开时读写互不干扰")
{
    FsFixture f;
    const int n = 400;
    fcreat("alpha", n, f.disk, f.sblk, f.i_table, f.f_table);
    fcreat("beta", n, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    fopen("beta", f.disk, f.sblk, f.i_table, f.f_table);

    std::string a(n, 'A');
    std::string b(n, 'B');
    fwrite("alpha", a, n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    fwrite("beta", b, n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);

    // alpha 的指针动了, beta 的应保持独立
    flseek("alpha", 0, f.disk, f.sblk, f.i_table, f.f_table);
    flseek("beta", 0, f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(fread("alpha", n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr), a);
    UT_CHECK_EQ(fread("beta", n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr), b);
}

// ---- 数据完整性 ----

UT_TEST(boundary, data_integrity_10k, "10000 字节随机化内容逐字节一致")
{
    FsFixture f;
    const int n = 10000;
    std::string got = writeThenRead(f, "big", n, 0, n);

    UT_CHECK_EQ(got.size(), static_cast<std::size_t>(n));
    // 逐字节比对, 不只看长度
    int mismatch = -1;
    const std::string want = pattern(n);
    for (int i = 0; i < n && i < static_cast<int>(got.size()); i++)
        if (got[i] != want[i]) { mismatch = i; break; }
    UT_CHECK_MSG(mismatch < 0,
                 mismatch < 0 ? std::string("") : ("第 " + ut::to_str(mismatch) + " 字节不一致"));
}

UT_TEST(boundary, data_integrity_survives_delete, "删除其中一个文件不影响另一个的内容")
{
    FsFixture f;
    const int n = 300;
    fcreat("keep", n, f.disk, f.sblk, f.i_table, f.f_table);
    fcreat("drop", n, f.disk, f.sblk, f.i_table, f.f_table);

    fopen("keep", f.disk, f.sblk, f.i_table, f.f_table);
    std::string content = pattern(n);
    fwrite("keep", content, n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    fclose("keep", f.disk, f.sblk, f.i_table, f.f_table);
    f.b_mgr.clear(f.disk);   // 确保落盘

    fdelete("drop", f.disk, f.sblk, f.i_table, f.f_table);

    fopen("keep", f.disk, f.sblk, f.i_table, f.f_table);
    flseek("keep", 0, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(fread("keep", n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr), content);
}

// fcreat 申报的大小恰为 512 的倍数时, d_size 应等于申报值
//
// 分配循环的最后一块补的是"整块之外剩下的那部分字节":
//
//     size - (blk_num - 1) * BYTE_PER_BLOCK
//
// 若照 size % BYTE_PER_BLOCK 补, 512 的倍数会让它等于 0 —— 最后一块照样分配了
// (也已经记进 d_addr), 却没计入 d_size: 申报 512 得到 d_size = 0, 申报 4096
// 得到 3584, 文件末尾那个盘块就此落在文件逻辑范围之外。写法要与 fwrite 扩容
// 分支保持一致。
UT_TEST(boundary, creat_size_multiple_of_block, "fcreat 申报 512 的倍数时 d_size 应等于申报值")
{
    // 4096 是演示流程里真正用到的申报值 (fcreat("reports.md", 4*1024))
    const int sizes[] = {512, 1024, 1536, 4096};
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
    {
        FsFixture f;
        const int n = sizes[k];
        fcreat("t", n, f.disk, f.sblk, f.i_table, f.f_table);
        UT_CHECK_MSG(inodeOf(f.disk, firstInode(f)).d_size == n,
                     "fcreat 申报 " + ut::to_str(n) + " 字节, d_size 实际为 " +
                     ut::to_str(inodeOf(f.disk, firstInode(f)).d_size));
    }
}

// 相邻两个文件的内容不得互相串块
//
// 盘块是整块回写的 (见 README 中 Bwrite 的说明), 所以只要 BMap 解错一个下标、
// 或者缓存淘汰时写错了块号, 别人的数据就会被整块覆盖, 而且当场没有任何报错。
// 这里建两个都申报 4096 字节的文件, 一个全写 'A'、一个全写 'B', 再逐块核对:
// 谁的文件里就只准有谁的字符。
//
// 申报的字节数与盘块容量本来就对不齐: 首块开头 16 字节是目录项, 4096 字节要
// ceil((4096 + 16) / 512) = 9 个盘块, 而 9 块装得下 9 * 512 - 16 = 4592 字节,
// 多出来的那部分是写不满的。这不影响下面的断言 —— 它只要求写下去的字节能落在
// 自己的盘块里。
UT_TEST(boundary, writing_one_file_does_not_touch_its_neighbour,
        "写满一个文件不得触碰相邻文件的盘块")
{
    FsFixture f;
    fcreat("a", 4096, f.disk, f.sblk, f.i_table, f.f_table);
    const int a_ino = firstInode(f);
    fcreat("b", 4096, f.disk, f.sblk, f.i_table, f.f_table);
    const int b_ino = dirEntryInode(f, 1);

    const int inos[2] = {a_ino, b_ino};
    const char mine[2] = {'A', 'B'};
    const char theirs[2] = {'B', 'A'};

    // 两个文件都写满申报的字节数, 分别用 A 和 B 填充, 便于事后辨认
    for (int k = 1; k >= 0; k--)   // 先写 b 再写 a, 让 a 的写入覆盖在最后
    {
        const char* name = k == 0 ? "a" : "b";
        fopen(name, f.disk, f.sblk, f.i_table, f.f_table);
        std::string buf(4096, mine[k]);
        fwrite(name, buf, buf.size(), f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
        fclose(name, f.disk, f.sblk, f.i_table, f.f_table);
        f.b_mgr.clear(f.disk);   // 延迟写落盘, 之后直接从磁盘读
    }

    for (int k = 0; k < 2; k++)
    {
        InodeMirror m = inodeOf(f.disk, inos[k]);
        // 文件占几块要按容量的式子算: 首块开头 16 字节是目录项, 装不下内容
        int blk_num = blocksForFileContent(m.d_size);
        if (blk_num == 0)
            blk_num = 1;

        Inode inode;
        readDisk(f.disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inos[k] * sizeof(Inode));

        for (int i = 0; i < blk_num; i++)
        {
            const int blk = inode.BMap(f.disk, i);
            const std::string bytes = readBlockBytes(f.disk, blk, BYTE_PER_BLOCK);

            int own = 0, foreign = 0;
            for (std::size_t j = 0; j < bytes.size(); j++)
            {
                if (bytes[j] == mine[k]) own++;
                if (bytes[j] == theirs[k]) foreign++;
            }

            // 一个块里出现邻居的填充字符, 说明这次写入越过了文件的盘块边界
            const std::string tag = std::string(k == 0 ? "a" : "b") +
                                    " 的第 " + ut::to_str(i) + " 块 (盘块 " + ut::to_str(blk) + ")";
            UT_CHECK_MSG(foreign == 0,
                         tag + " 混入了邻居的内容 " + ut::to_str(foreign) + " 字节");
            UT_CHECK_MSG(own > 0, tag + " 没有写入自己的内容, 写入根本没落到这个块上");
        }
    }
}

// 跨越块边界的写入应完整写出
//
// readWriteFile 里"一次读写要跨几块"必须按 start_cur + size 来算。文件内容从块内
// 偏移 16 开始, 于是"偏移 0 写 497 字节"实际要占到块内 16..512, 已经跨进下一块;
// 若只看 size (size / 512 + !!(size % 512)) 就得到 1 块, end_cur 还会回绕成 0,
// end - start + 1 变成负数, 循环一次都不执行 —— 一个字节都没写, 读写指针还被推到
// 了负数上。破坏区间随起始偏移平移, 判据是 start_cur + size > blk_num * 512。
//
// 所以"跨几块"交给 blocksSpanned(): 它按 start_cur + size 来算, 并且对 size <= 0
// 返回 0 (读越过文件末尾时 size 会被算成负数, 那时一个块都不该碰)。
UT_TEST(boundary, cross_block_write_completes, "跨越块边界的写入应完整写出")
{
    struct Case { int offset; int size; };
    const Case cases[] = {
        {0,   497},    // 16 + 497 = 513 > 512
        {0,   512},    // 16 + 512 = 528 > 512
        {0,  1009},    // 16 + 1009 = 1025 > 1024
        {100, 412},    // 116 + 412 = 528 > 512
        {484,  13},    // 500 + 13 = 513 > 512
    };

    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        FsFixture f;
        const int off = cases[k].offset;
        const int n = cases[k].size;
        int ptr = -1;
        std::string got = writeThenRead(f, "t", 4000, off, n, &ptr);

        const std::string tag = "偏移 " + ut::to_str(off) + " 写 " + ut::to_str(n) + " 字节";
        UT_CHECK_MSG(ptr == off + n,
                     tag + ": 读写指针应停在 " + ut::to_str(off + n) + ", 实际 " + ut::to_str(ptr));
        UT_CHECK_MSG(got == pattern(n), tag + ": 读回的内容与写入不一致");
    }
}

// 分配盘块时同样要给首块那 16 字节目录项留出位置
//
// 上面那条说的是"一次读写要跨几块", 这条说的是"一个文件该分几块"。若按
//
//     size / BYTE_PER_BLOCK + !!(size % BYTE_PER_BLOCK)
//
// 分配, 申报 497..512 字节 (以及 512 的任意整数倍) 的文件就会少拿一块: d_size
// 记着 512, 手里却只有一个装得下 496 字节的块 —— 一个文件真正装得下的是
// B * 512 - 16 字节, 首块开头那 16 字节是它自己的目录项。
//
// 少掉的那块在 fwrite 时暴露: 第 497 个字节落在 d_addr[1] 上, 而它是 -1。BMap
// 返回 -1, Bwrite 拿 -1 当块号, writeDisk 负偏移不 seek, 512 字节就写到文件流
// 上一次停留的位置 —— 一个跟这个文件毫无关系的盘块。appendBlk 选槽位、
// releaseAllBlk 数块数也得用同一个式子, 三处必须一致, 否则槽位错开、两块挤进
// 同一个 d_addr。
//
// 判据: 写满申报的字节数之后, 数据区里被写脏的盘块必须只有这个文件自己的。
UT_TEST(boundary, declared_size_fits_allocated_blocks,
        "写满申报的字节数不得弄脏不属于它的盘块")
{
    const int sizes[] = {496, 497, 512, 1000, 4096};

    // 整张镜像的逐块快照。块 0 是 superblock(写完要落盘), 这里只关心数据区,
    // 所以快照取值时跳过 0..FILE_BLOCK_START-1。
    for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
    {
        FsFixture f;
        const int n = sizes[k];
        fcreat("t", n, f.disk, f.sblk, f.i_table, f.f_table);

        const int ino = firstInode(f);
        Inode inode;
        readDisk(f.disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + ino * sizeof(Inode));
        InodeMirror m = inodeOf(f.disk, ino);

        // 这个文件自己的盘块: 数据块 + 已分配的索引块
        std::set<int> own;
        int nblk = blocksForFileContent(m.d_size);
        if (nblk == 0)
            nblk = 1;
        for (int i = 0; i < nblk; i++)
            own.insert(inode.BMap(f.disk, i));
        for (int i = DIRECT_INDEX_NUM; i < 10; i++)
            if (m.d_addr[i] >= FILE_BLOCK_START)
                own.insert(m.d_addr[i]);

        // 容量不变式: 分到的块必须真的装得下申报的字节数
        UT_CHECK_MSG(nblk * BYTE_PER_BLOCK - DIR_HDR >= n,
                     "申报 " + ut::to_str(n) + " 字节只分到 " + ut::to_str(nblk) +
                     " 块, 装得下 " + ut::to_str(nblk * BYTE_PER_BLOCK - DIR_HDR) + " 字节");

        f.b_mgr.clear(f.disk);
        std::vector<std::string> before = dumpDataArea(f.disk);

        fopen("t", f.disk, f.sblk, f.i_table, f.f_table);
        std::string buf(n, 'Z');
        fwrite("t", buf, buf.size(), f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
        fclose("t", f.disk, f.sblk, f.i_table, f.f_table);
        f.b_mgr.clear(f.disk);

        std::vector<std::string> after = dumpDataArea(f.disk);
        for (int b = 0; b < FILE_BLOCK_NUM; b++)
        {
            if (before[b] == after[b] || own.count(b + FILE_BLOCK_START))
                continue;
            UT_CHECK_MSG(false, "申报 " + ut::to_str(n) + " 字节写满后, 盘块 " +
                                ut::to_str(b + FILE_BLOCK_START) +
                                " 被写脏, 但它不属于这个文件");
        }
    }
}

// 扩容: 补几块按块数相减算, d_size 按增量推
//
// fwrite 越过申报长度时 readWriteFile 要给文件补盘块, 补的块数是
//
//     blocksForFileContent(指针位置 + 本次写入) - 现有块数
//
// 拿字节数去估 (例如用块内剩余空闲反推) 既会多分 —— 多出来的块白占着, 落不进
// d_addr, 等于泄漏 —— 又会少分。
//
// 每补一块之后, d_size 必须推到「装下这么多块」的位置 —— 而且 changeSize 是增量,
// 补进去的是差值而不是目标值。这一点写错不会立刻报错, 只是让 appendBlk 按偏大的
// d_size 选槽位: 槽位跳着走, 中间的空槽位连同它该指向的数据块一起丢掉, 文件首块
// 开头的目录项被写入的内容盖住 —— 文件在 ls 里就此消失。
UT_TEST(boundary, grow_beyond_declared_size,
        "写越过申报长度后, 索引槽位与内容都应完整")
{
    struct Case { int decl; int write_n; };
    const Case cases[] = {
        {0,   2000},   // 0 字节文件长大: 首块已经占着, 补块从 d_addr[1] 起
        {10,  4096},   // 越过申报长度的同时跨进一级间接
        {496,  497},   // 恰好越过首块的容量
        {500,  512},
    };

    for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
    {
        FsFixture f;
        const int decl = cases[k].decl;
        const int wn = cases[k].write_n;
        const std::string tag = "申报 " + ut::to_str(decl) + " 字节写 " + ut::to_str(wn) + " 字节";

        fcreat("t", decl, f.disk, f.sblk, f.i_table, f.f_table);
        f.b_mgr.clear(f.disk);

        std::vector<std::string> before = dumpDataArea(f.disk);

        std::string w = pattern(wn);
        fopen("t", f.disk, f.sblk, f.i_table, f.f_table);
        fwrite("t", w, wn, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
        f.b_mgr.clear(f.disk);

        // 文件名还认得出来: 目录项写在首块开头, 数据不该盖到它头上
        const InodeMirror m0 = inodeOf(f.disk, firstInode(f));
        FileDir hdr;
        readDisk(f.disk, &hdr, sizeof(FileDir), m0.d_addr[0] * BYTE_PER_BLOCK);
        const std::string nm = hdr.getFileName();
        UT_CHECK_MSG(nm == "t", tag + ": 文件首块的目录项被写坏, 文件名成了 \"" + nm + "\"");

        // 内容原样读回
        flseek("t", 0, f.disk, f.sblk, f.i_table, f.f_table);
        std::string got = fread("t", wn, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
        UT_CHECK_MSG(got == w, tag + ": 扩容后读回的内容与写入不一致 (读到 " +
                               ut::to_str(got.size()) + " 字节)");
        fclose("t", f.disk, f.sblk, f.i_table, f.f_table);

        // 索引槽位必须连续且在数据区内 —— 跳槽位意味着中间那块已经找不回来了
        const int ino = firstInode(f);
        Inode inode;
        readDisk(f.disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + ino * sizeof(Inode));
        const int data_blk = blocksForFileContent(wn);
        std::set<int> seen;
        for (int i = 0; i < data_blk; i++)
        {
            const int blk = inode.BMap(f.disk, i);
            UT_CHECK_MSG(blk >= FILE_BLOCK_START,
                         tag + ": 第 " + ut::to_str(i) + " 块的块号是 " + ut::to_str(blk));
            if (seen.count(blk))
                UT_CHECK_MSG(false, tag + ": 第 " + ut::to_str(i) + " 块与前面的块重复: " +
                                    ut::to_str(blk));
            seen.insert(blk);
        }

        // 占用的块数: 数据块 + 预分配的索引块, 多一块就是白拿, 少一块就是越界
        std::vector<std::string> after = dumpDataArea(f.disk);
        int used = 0;
        for (int b = 0; b < FILE_BLOCK_NUM; b++)
            if (before[b] != after[b])
                used++;
        int expect = data_blk + (data_blk >= DIRECT_INDEX_NUM ? 1 : 0);
        UT_CHECK_MSG(used == expect, tag + ": 占用 " + ut::to_str(used) +
                                     " 个数据区盘块, 期望 " + ut::to_str(expect));
    }
}

// 申报 0 字节的 fcreat: 新文件仍要占一块, 且不能碰别人的 inode
//
// 若 blk_num = ceil(0/512) = 0, 一个盘块都不分配, 那么
//
//     dir.create(disk, new_inode.BMap(disk, 0));   // BMap 返回 -1
//
// 块号为 -1 时落点是 -512, 而 writeDisk 只在 offset >= 0 时才 seek:
//
//     if (offset >= 0) disk.seekp(offset, ios::beg);
//     disk.write((char*)buffer, size);
//
// 负偏移不 seek, 就写在流的当前位置上 —— 即上一次写入的结尾。newFile 里紧挨着的
// 上一次写入是把新 inode 整体存盘, 于是这 16 字节目录项正好落在紧随其后的那个
// inode 头上 (这里正好是父目录), 覆盖掉它的 d_mode / d_size / d_addr[0..1]。
//
// 所以 blk_num 至少为 1 —— 目录项写在文件首块开头, 哪怕 0 字节也必须占一块,
// 否则 BMap 恒为 -1。另外 FileDir::create 会拦下负块号, 把这种"目录项无处可写"
// 从静默损坏变成一条明确的报错。
//
// 断言不去盯某个固定的 inode 号, 也不去数"有几个 inode 变了" —— 父目录本来就会
// 因为 addSubDirSize 合法地变化, 数数量会把它和被砸的情况混为一谈。
// 这里直接盯住父目录 inode 的字段: 该变的只有 d_size。
UT_TEST(boundary, creat_zero_size_leaves_inode_area_intact,
        "fcreat 申报 0 字节不应打坏父目录的 inode")
{
    FsFixture f;
    const int parent = currentDirInode(f);
    // distributeInode 是弹栈, 栈顶就是即将分配给新文件的那个 inode 号。
    // 不通过目录项去认它 —— 目录项落错地方正是这条用例要抓的情况, 那时它已经不可信了。
    SuperBlockMirror sb = superBlockOf(f.disk, f.sblk);
    const int created = sb.s_inode[sb.s_ninode - 1];

    const InodeMirror parent_before = inodeOf(f.disk, parent);
    fcreat("t", 0, f.disk, f.sblk, f.i_table, f.f_table);

    // 新建的文件要有合法的首块 —— 它的目录项就写在首块开头, 没有块就等于没有名字
    const InodeMirror file_after = inodeOf(f.disk, created);
    UT_CHECK_MSG(file_after.d_addr[0] >= FILE_BLOCK_START,
                 "0 字节的 fcreat 没有给新文件分配首块, d_addr[0] = " +
                 ut::to_str(file_after.d_addr[0]) + ", 目录项无处可写");

    // 占了一块不等于有内容: 申报 0 字节, d_size 就该是 0
    UT_CHECK_MSG(file_after.d_size == 0,
                 "0 字节的文件 d_size 应为 0, 实际 " + ut::to_str(file_after.d_size));

    // 端到端后果: 名字写在首块里, 所以只要首块合法, 文件就该在 ls 中可见
    CaptureCout cap;
    ls(f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_MSG(cap.str().find("t") != std::string::npos,
                 "0 字节的文件没有出现在 ls 中, 它的目录项丢了");

    // 父目录的 inode 只该在 d_size 上变化 (多登记一个子文件项)。
    // 那 16 字节目录项若落到它头上, 覆盖的正是 d_mode / d_size / d_addr[0..1]。
    const InodeMirror parent_after = inodeOf(f.disk, parent);
    UT_CHECK_MSG(parent_after.d_mode == parent_before.d_mode,
                 "父目录 inode 的 d_mode 被目录项覆盖: " +
                 ut::to_str(parent_before.d_mode) + " -> " + ut::to_str(parent_after.d_mode));
    UT_CHECK_MSG(parent_after.d_size == parent_before.d_size + static_cast<int>(sizeof(int16_t)),
                 "父目录 d_size 应只增加一个目录项 (" +
                 ut::to_str(parent_before.d_size + static_cast<int>(sizeof(int16_t))) +
                 "), 实际为 " + ut::to_str(parent_after.d_size));
    UT_CHECK_MSG(parent_after.d_addr[0] == parent_before.d_addr[0],
                 "父目录 inode 的 d_addr[0] 被目录项覆盖: " +
                 ut::to_str(parent_before.d_addr[0]) + " -> " +
                 ut::to_str(parent_after.d_addr[0]));
}

// 盘满时 fcreat 应整体失败, 不留下半个文件
//
// newFile 的顺序是「先登记目录项、再分配盘块」: 目录项先写进父目录, 之后才走分配
// 循环。盘满时 distributeBlk 返回 -1, 于是
//
//   - 父目录的子文件数已经 +1, 目录项也已经落盘;
//   - 但新文件的 d_addr[0] 停在 -1, 没有首块, 目录项(名字)无处可写。
//
// 结果是一个"有目录项、没有名字"的文件: ls 遍历到它时, getFileNameByInodeIndex
// 拿 BMap(disk,0) == -1 去 readDisk, 而 readDisk 对负偏移不 seek, 就从流的当前位置
// 读出几个字节当名字 —— 显示成一串乱码。
//
// 这条用例断言的正确行为是: 分配失败时整体回滚 (不登记目录项、不消耗 inode)。
// FileDir::create 会拦下负块号并报错, 因此不会砸坏别人的 inode; 但"半个文件"这个
// 后果目前还在, 所以它是 UT_TEST_KNOWN_BUG。
//
// 构造盘满不必真的写满 8000 个块: distributeBlk 只读 sblk 里的空闲表,
// 在内存里抽干它即可。
UT_TEST_KNOWN_BUG(boundary, creat_on_full_disk_leaves_no_trace,
                  "盘满时 fcreat 应整体失败, 不留下半个文件")
{
    FsFixture f;
    while (f.sblk.distributeBlk(f.disk) != -1)
        ;   // 抽干空闲块表

    const int parent = currentDirInode(f);
    const InodeMirror parent_before = inodeOf(f.disk, parent);
    const int subs_before = getCurrentDirSubFileNum(f.disk, f.i_table);

    std::string ls_before;
    {
        CaptureCout cap;
        ls(f.disk, f.sblk, f.i_table, f.f_table);
        ls_before = cap.str();
    }

    fcreat("t", 10, f.disk, f.sblk, f.i_table, f.f_table);

    // 一个盘块都分配不到, 就该整体失败 —— 父目录不该多出一个子文件项
    UT_CHECK_MSG(getCurrentDirSubFileNum(f.disk, f.i_table) == subs_before,
                 "盘满时父目录仍多登记了一个子文件, 但那个文件没有首块, 名字无处可写");
    UT_CHECK_MSG(inodeOf(f.disk, parent).d_size == parent_before.d_size,
                 "盘满时父目录的 d_size 不该变化");

    std::string ls_after;
    {
        CaptureCout cap;
        ls(f.disk, f.sblk, f.i_table, f.f_table);
        ls_after = cap.str();
    }
    UT_CHECK_MSG(ls_after == ls_before,
                 "盘满时的 fcreat 在 ls 里留下了痕迹 (半个文件, 名字是乱码)");
}

// README「四、目录结构」写明: 目录项写在文件第一个数据块的开头, 目录项之后紧跟
// 文件内容 —— 两者必须同处一块。
//
// 块首那 16 字节要保留下来, 靠的是两件事: newFile / fwrite / BMap 用的是同一套
// 「块号 -> 字节偏移」算法, 同一个逻辑块只能落到同一个物理块; Bwrite 又必须做
// read-modify-write, 否则内容写进缓存时会把块首的目录项一起清零。
//
// 这个用例断言的是这两件事共同的结果 —— 目录项与内容同处一块 —— 而不是中间步骤。
UT_TEST(boundary, first_block_header_and_content_colocated,
        "文件首块的目录项与文件内容应位于同一盘块")
{
    FsFixture f;
    const int n = 100;
    int ptr = -1;
    writeThenRead(f, "t", 2000, 0, n, &ptr);

    const int ino_index = firstInode(f);
    Inode probe;
    readDisk(f.disk, &probe, sizeof(Inode), INODE_AREA_OFFSET + ino_index * sizeof(Inode));
    const int blk0 = probe.BMap(f.disk, 0);
    UT_CHECK_MSG(blk0 >= FILE_BLOCK_START, "首个数据块号非法: " + ut::to_str(blk0));

    // 内容此时只在缓存里, 落盘之后再看磁盘上的真实结果
    f.b_mgr.clear(f.disk);

    // 目录项之后紧跟文件内容, 两者都应在 blk0 这一块内
    const std::string head = readBlockBytes(f.disk, blk0, DIR_HDR + n);
    UT_CHECK_CONTAINS(head.substr(4, 12), "t");   // 块首 16 字节是目录项

    // 内容与写入不符时逐字节比对会打印二进制, 这里只给出可读的结论
    UT_CHECK_MSG(head.substr(DIR_HDR, 8) == pattern(n).substr(0, 8),
                 "块首目录项之后不是刚写入的文件内容, 二者不在同一物理块上");
}

// ---- 重量级: 大文件跨二级间接索引 ----
//
// 200000 字节 = 391 个数据块, 依次用到 6 个直接索引、256 个一级间接,
// 再进二级间接。这一规模会把成组链接法推过好几组, 也必然触发 d_addr[8]。
//
// 拆成两个用例:
//   - 索引结构本身走的是 fcreat/appendBlk/BMap, 全程不碰缓存;
//   - 内容的往返读写要经过缓存, 覆盖块分配与缓存回写的配合。

UT_TEST_FULL(boundary, large_file_index_structure, "20 万字节文件的 6-2-2 索引结构正确落盘")
{
    FsFixture f;
    const int n = 200000;
    const int data_blocks = n / BYTE_PER_BLOCK + !!(n % BYTE_PER_BLOCK);   // 391

    // 记下分配前的空闲链栈顶, 后面用来确认分配确实推进了组
    SuperBlockMirror before = superBlockOf(f.disk, f.sblk);
    const int initial_top = before.s_free[before.s_nfree - 1];

    fcreat("big", n, f.disk, f.sblk, f.i_table, f.f_table);

    // 从磁盘重新读入 inode, 确保断言的是真正落盘的结果
    const int ino_index = firstInode(f);
    InodeMirror ino = inodeOf(f.disk, ino_index);
    UT_CHECK_EQ(ino.d_size, n);

    // 391 块用满两级索引: d_addr[6]/[7] 是一级, d_addr[8] 是二级
    for (int i = 6; i <= 8; i++)
        UT_CHECK_MSG(ino.d_addr[i] >= FILE_BLOCK_START,
                     "d_addr[" + ut::to_str(i) + "] 应指向已分配的索引块, 实际 " +
                     ut::to_str(ino.d_addr[i]));

    // 用重新读入的 inode 走一遍 BMap: 每个逻辑块都应解析出唯一且合法的物理块
    Inode probe;
    readDisk(f.disk, &probe, sizeof(Inode), INODE_AREA_OFFSET + ino_index * sizeof(Inode));
    std::vector<int> seen;
    int bad = 0;
    for (int i = 0; i < data_blocks; i++)
    {
        int blk = probe.BMap(f.disk, i);
        if (blk < FILE_BLOCK_START)
        {
            if (bad < 3)
                UT_CHECK_MSG(false, "BMap(" + ut::to_str(i) + ") 返回非法块号 " +
                                    ut::to_str(blk));
            bad++;
            continue;
        }
        bool dup = false;
        for (std::size_t k = 0; k < seen.size(); k++)
            if (seen[k] == blk) dup = true;
        if (dup && bad < 3)
            UT_CHECK_MSG(false, "BMap(" + ut::to_str(i) + ") 返回了重复块号 " + ut::to_str(blk));
        if (dup) bad++;
        seen.push_back(blk);
    }
    UT_CHECK_MSG(bad == 0, "BMap 解析出的盘块有 " + ut::to_str(bad) + " 处异常");
    UT_CHECK_EQ(static_cast<int>(seen.size()), data_blocks);

    // 391 个数据块 + 若干索引块都从数据区里拿, 空闲链应确实变短了。
    // 分配是弹栈, 栈顶 s_free[s_nfree-1] 是下一个待分配的块号, 因此它会一路上涨;
    // 涨过初始栈顶就说明已经跨过了第一组, 触发了索引块补充。
    SuperBlockMirror m = superBlockOf(f.disk, f.sblk);
    UT_CHECK_MSG(m.s_nfree > 0 && m.s_nfree <= 100,
                 "s_nfree 应落在 1..100, 实际 " + ut::to_str(m.s_nfree));

    const int top = m.s_free[m.s_nfree - 1];
    UT_CHECK_MSG(top > initial_top,
                 "分配 396 个块后空闲链应已推进到后面的组: 栈顶从 " +
                 ut::to_str(initial_top) + " 变为 " + ut::to_str(top));

    int bad_free = 0;
    for (int i = 0; i < m.s_nfree; i++)
        if (m.s_free[i] < FILE_BLOCK_START) bad_free++;
    UT_CHECK_MSG(bad_free == 0, "空闲链里有 " + ut::to_str(bad_free) + " 个非法块号");
}

// 端到端: 20 万字节走完 fcreat → fopen → fwrite → flseek → fread 全流程,
// 内容逐字节一致, 且当前目录的目录项不能被动过
//
// 为什么这里要顺带盯住根目录: 回写落点是 blkno * BYTE_PER_BLOCK, 基址一旦用错,
// 数据区的块就会被写到相隔固定偏移的另一处。391 个数据块从 292 起分配, 回写落点
// 于是扫过整片数据区, 其中块 384 的落点正好是数据块 192 —— 根目录所在的那一块。
// 整块被覆盖后根目录当场消失: firstInode() 读到的 inode 号变成 0, d_size 也读成 0,
// 看着像 inode 坏了。
UT_TEST_FULL(boundary, large_file_roundtrip_survives_cache,
             "20 万字节大文件往返一致, 且不破坏根目录")
{
    FsFixture f;
    const int n = 200000;
    int ptr = -1;
    std::string got = writeThenRead(f, "big", n, 0, n, &ptr);

    UT_CHECK_EQ(ptr, n);

    // 根目录的目录项仍在: 能读回 "big" 的 inode 号, 且该 inode 有效
    const int ino = firstInode(f);
    UT_CHECK_MSG(ino > 0, "根目录的目录项已被覆盖, 读到的 inode 号为 " + ut::to_str(ino));

    UT_CHECK_EQ(got.size(), static_cast<std::size_t>(n));
    UT_CHECK_MSG(got == pattern(n), "大文件读回的内容与写入不一致");
}
