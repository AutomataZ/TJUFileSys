#include "framework.h"
#include "fixture.h"
#include "filedir.h"
#include <string>

// 套件 3: 缓存队列 (LRU + 延迟写)
//
// BufferMgr 是全文件系统唯一的块设备入口: 所有盘块读写都要经过它。
// 队列上限 MAX_BQUEUE_SIZE 在 DEBUG_ENV 下是 5, 所以只要碰 6 个不同的块
// 就能触发 LRU 淘汰, 无需构造大负载。
//
// BQueue 的 q[] 是 protected, 因此顺序只能通过公开的 front() 观察:
// 队首即最久未使用、下一个被淘汰的缓存块。
//
// 关于坐标: 缓存内部按 blkno * BYTE_PER_BLOCK 定位, 与系统其余部分一致
// (块号是绝对块号, 见 define.h 中 FILE_AREA_OFFSET 处的说明)。
//
// 本文件末尾的两个用例守着缓存的「局部写」语义: Bwrite 走 getBlk
// 而非 Bread, 未命中时拿到的是全零缓存块, 回写却整块 512 字节写出 —— 块内没
// 参与本次写入的部分被清零, 落在文件首块上时抹掉的就是目录项。

namespace {

// 绕过缓存, 按缓存的坐标把一个盘块整体写成 fill 字符
void fillBlock(std::fstream& disk, int blkno, char fill)
{
    std::string s(BYTE_PER_BLOCK, fill);
    writeDisk(disk, &s[0], BYTE_PER_BLOCK, blkno * BYTE_PER_BLOCK);
}

} // namespace

UT_TEST(buffer, default_buffer_state, "新建缓存块的默认状态")
{
    Buffer b;
    UT_CHECK_EQ(b.getBlkno(), -1);
}

UT_TEST(buffer, queue_basic_semantics, "队列 push/pop/size/empty 基本语义")
{
    BQueue q;
    UT_CHECK_MSG(q.empty(), "新建队列应为空");
    UT_CHECK_EQ(q.size(), 0);

    q.pop();                       // 空队列 pop 应为 no-op, 不能崩
    UT_CHECK_MSG(q.empty(), "空队列 pop 之后仍应为空");

    q.push(Buffer(10));
    q.push(Buffer(11));
    UT_CHECK_EQ(q.size(), 2);
    UT_CHECK_MSG(!q.empty(), "push 之后队列不应为空");

    UT_CHECK_EQ(q.front().getBlkno(), 10);   // push 到队尾, front 是最早的

    q.pop();
    UT_CHECK_EQ(q.size(), 1);
    UT_CHECK_EQ(q.front().getBlkno(), 11);
}

UT_TEST(buffer, queue_push_respects_capacity, "队列满时 push 不增长")
{
    BQueue q;
    for (int i = 0; i < MAX_BQUEUE_SIZE + 3; i++)
        q.push(Buffer(200 + i));

    UT_CHECK_EQ(q.size(), MAX_BQUEUE_SIZE);
    UT_CHECK_EQ(q.front().getBlkno(), 200);
}

UT_TEST(buffer, find_miss_returns_sentinel, "find 未命中返回块号为 -1 的哨兵")
{
    BQueue q;
    q.push(Buffer(10));

    UT_CHECK_EQ(q.find(10)->getBlkno(), 10);
    UT_CHECK_EQ(q.find(999)->getBlkno(), -1);   // 哨兵, 不是 nullptr
}

UT_TEST(buffer, update_moves_hit_to_tail, "update 把命中的块移到队尾")
{
    FsFixture f;
    for (int i = 0; i < 3; i++)
        f.b_mgr.getBlk(f.disk, 100 + i);

    UT_CHECK_EQ(f.b_mgr.bq.front().getBlkno(), 100);
    UT_CHECK_EQ(f.b_mgr.bq.size(), 3);

    f.b_mgr.getBlk(f.disk, 100);   // 命中队首, 应被移到队尾

    UT_CHECK_EQ(f.b_mgr.bq.size(), 3);            // 命中不增加长度
    UT_CHECK_EQ(f.b_mgr.bq.front().getBlkno(), 101);
    UT_CHECK_EQ(f.b_mgr.bq.find(100)->getBlkno(), 100);   // 仍在队列中
}

UT_TEST(buffer, lru_eviction, "队满时淘汰最久未使用的块")
{
    FsFixture f;
    for (int i = 0; i < MAX_BQUEUE_SIZE; i++)
        f.b_mgr.getBlk(f.disk, 100 + i);

    UT_CHECK_EQ(f.b_mgr.bq.size(), MAX_BQUEUE_SIZE);
    UT_CHECK_EQ(f.b_mgr.bq.front().getBlkno(), 100);

    // 先触碰 100, 让它不再是"最久未使用"
    f.b_mgr.getBlk(f.disk, 100);
    UT_CHECK_EQ(f.b_mgr.bq.front().getBlkno(), 101);

    // 再放一个新块, 被淘汰的应是 101 而不是刚碰过的 100
    f.b_mgr.getBlk(f.disk, 999);

    UT_CHECK_EQ(f.b_mgr.bq.size(), MAX_BQUEUE_SIZE);
    UT_CHECK_EQ(f.b_mgr.bq.find(101)->getBlkno(), -1);    // 101 已被淘汰
    UT_CHECK_EQ(f.b_mgr.bq.find(100)->getBlkno(), 100);   // 100 因被触碰而存活
    UT_CHECK_EQ(f.b_mgr.bq.find(999)->getBlkno(), 999);
}

UT_TEST(buffer, bread_twice_hits_cache, "Bread 同一块两次, 第二次命中缓存")
{
    FsFixture f;
    const int blk = FILE_BLOCK_START;
    fillBlock(f.disk, blk, 'Z');

    char* first = f.b_mgr.Bread(f.disk, blk)->getLoad();
    UT_CHECK_EQ(first[0], 'Z');
    UT_CHECK_EQ(f.b_mgr.bq.size(), 1);

    char* second = f.b_mgr.Bread(f.disk, blk)->getLoad();
    UT_CHECK_EQ(second[0], 'Z');
    UT_CHECK_EQ(f.b_mgr.bq.size(), 1);   // 命中, 不新增缓存块
}

UT_TEST(buffer, bread_reads_modified_disk, "Bread 命中缓存时不再回读磁盘")
{
    // 缓存的意义就在于: 块已在缓存中时, 磁盘上的变化不会反映到缓存
    FsFixture f;
    const int blk = FILE_BLOCK_START;
    fillBlock(f.disk, blk, 'A');

    UT_CHECK_EQ(f.b_mgr.Bread(f.disk, blk)->getLoad()[0], 'A');

    fillBlock(f.disk, blk, 'B');   // 绕过缓存直接改盘

    UT_CHECK_EQ(f.b_mgr.Bread(f.disk, blk)->getLoad()[0], 'A');
}

UT_TEST(buffer, delayed_write_then_clear, "延迟写: Bwrite 不落盘, clear 才落盘")
{
    FsFixture f;
    const int blk = FILE_BLOCK_START;
    fillBlock(f.disk, blk, 'A');

    std::string data = "hello";
    f.b_mgr.Bwrite(f.disk, blk, data, 0, 5);

    // 只改了缓存, 磁盘上应还是原来的内容
    UT_CHECK_EQ(readBlockBytes(f.disk, blk, BYTE_PER_BLOCK).substr(0, 5),
                std::string("AAAAA"));

    f.b_mgr.clear(f.disk);
    UT_CHECK_EQ(readBlockBytes(f.disk, blk, BYTE_PER_BLOCK).substr(0, 5), data);
}

UT_TEST(buffer, eviction_flushes_dirty_block, "淘汰脏块时自动写回磁盘")
{
    FsFixture f;
    const int victim = FILE_BLOCK_START;
    fillBlock(f.disk, victim, 'A');

    std::string data = "world";
    f.b_mgr.Bwrite(f.disk, victim, data, 0, 5);
    UT_CHECK_EQ(f.b_mgr.bq.front().getBlkno(), victim);

    // 再塞满一整轮队列, 把 victim 挤出去
    for (int i = 0; i < MAX_BQUEUE_SIZE; i++)
        f.b_mgr.getBlk(f.disk, 500 + i);

    UT_CHECK_EQ(f.b_mgr.bq.find(victim)->getBlkno(), -1);   // 已被淘汰
    UT_CHECK_EQ(readBlockBytes(f.disk, victim, BYTE_PER_BLOCK).substr(0, 5), data);
}

// Bwrite 是一次局部写: 只覆盖 [offset, offset+size), 而回写时整块 512 字节都会
// 写出去。因此未命中时不能拿一块全零的新缓存直接改 —— 那会把块内没参与本次写入
// 的部分清掉。Bwrite 内部改用 Bread, 未命中时先读入块内原有内容, 构成一次完整的
// read-modify-write。
//
// 这条与 shell.cpp 的调用方式直接相关: 那里以 start = start_cur(块内偏移, 通常
// 非 0) 调用 Bwrite 写文件内容, 块首的目录项和上一块残留的数据都靠它保住。
UT_TEST(buffer, partial_write_preserves_tail, "局部写块时应保留块内未写入的原有内容")
{
    FsFixture f;
    const int blk = FILE_BLOCK_START + 1;
    fillBlock(f.disk, blk, 'A');

    std::string data = "hello";
    f.b_mgr.Bwrite(f.disk, blk, data, 0, 5);   // 未先 Bread, 只写前 5 字节
    f.b_mgr.clear(f.disk);

    std::string after = readBlockBytes(f.disk, blk, BYTE_PER_BLOCK);
    UT_CHECK_EQ(after.substr(0, 5), data);
    UT_CHECK_MSG(after.substr(5) == std::string(BYTE_PER_BLOCK - 5, 'A'),
                 "块内第 5 字节之后被清零, 原有数据丢失");
}

// 目录项写在文件第一个数据块的开头, 文件内容紧随其后 (README「四、目录结构」),
// 两者同处一块。地址统一之后 dir.create 与 fwrite 经缓存写入都按
// blkno * BYTE_PER_BLOCK 定位 (见 define.h), 落在同一块上。
//
// 这条用例固化的是随之而来的那个不变量: 往这个块里写内容, 起点是
// sizeof(FileDir) 而不是 0, 因此块首的目录项必须原样留着 —— 否则文件会当场从
// ls 里消失。它不涉及任何地址换算, 只要求"写内容不要动到目录项"。
UT_TEST(buffer, flush_must_not_clobber_file_dir,
         "缓存回写不得覆盖文件目录项")
{
    FsFixture f;
    const int n = 100;

    fcreat("t", 2000, f.disk, f.sblk, f.i_table, f.f_table);

    // 该文件的首块: 目录项在块首, 文件内容紧随其后
    int16_t ino = 0;
    readDisk(f.disk, &ino, sizeof(int16_t),
             getCurrentBlk(f.disk, f.i_table) * BYTE_PER_BLOCK + sizeof(FileDir));
    const int blk0 = inodeOf(f.disk, ino).d_addr[0];
    UT_CHECK_MSG(blk0 >= FILE_BLOCK_START, "首个数据块号非法: " + ut::to_str(blk0));

    const std::string dir_before = readBlockBytes(f.disk, blk0, sizeof(FileDir));

    // 经缓存写入文件内容, 然后回写
    fopen("t", f.disk, f.sblk, f.i_table, f.f_table);
    std::string buf(n, 'Q');
    fwrite("t", buf, n, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);
    f.b_mgr.clear(f.disk);

    const std::string dir_after = readBlockBytes(f.disk, blk0, sizeof(FileDir));
    UT_CHECK_MSG(dir_after == dir_before,
                 "块首的目录项被缓存回写改写, 写入内容本应只覆盖目录项之后的部分");

    // 端到端后果: 目录项没了, 文件就从当前目录里消失
    CaptureCout cap;
    ls(f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_CONTAINS(cap.str(), "t");
}
