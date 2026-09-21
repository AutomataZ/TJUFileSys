#include "framework.h"
#include "fixture.h"
#include "filedir.h"
#include <string>

// 套件 4: 文件系统调用层 (shell.cpp 中的自由函数)
//
// 这里刻意不通过 Shell::usr() 走交互流程 —— 那会先无条件跑一遍 FINAL_TEST
// 的硬编码演示, 状态不可控。直接调用各命令函数, 每个用例从 fformat 开始。
//
// 注意 mkdir/fcreat 恒返回 0, fopen/fclose 恒返回 1, 都不能用来判断成败,
// 因此断言一律针对副作用: 磁盘内容、内存表状态、ls 输出、superblock 计数。

namespace {

// 目录项在父目录中占 2 字节 (int16_t 的 inode 号)
const int DIR_ENTRY_SIZE = static_cast<int>(sizeof(int16_t));

// ls 往 cout 打印, 临时换掉流缓冲把它接出来
std::string lsOutput(FsFixture& f)
{
    CaptureCout cap;
    ls(f.disk, f.sblk, f.i_table, f.f_table);
    return cap.str();
}

int countOccurrences(const std::string& hay, const std::string& needle)
{
    if (needle.empty())
        return 0;
    int n = 0;
    for (std::size_t p = hay.find(needle); p != std::string::npos;
         p = hay.find(needle, p + needle.size()))
        n++;
    return n;
}

// 当前目录下第 idx 个子文件的 inode 号
int subInodeAt(FsFixture& f, int idx)
{
    int c_dir_index = getCurrentBlk(f.disk, f.i_table);
    int off = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    int16_t v = 0;
    readDisk(f.disk, &v, DIR_ENTRY_SIZE, off + idx * DIR_ENTRY_SIZE);
    return v;
}

int currentDirInode(FsFixture& f)
{
    return f.i_table.inode[f.i_table.getCurrentDir()].i_number;
}

// 生成一段可辨认的内容, 便于逐字节比对
std::string pattern(int n)
{
    std::string s;
    for (int i = 0; i < n; i++)
        s += static_cast<char>('A' + (i % 26));
    return s;
}

} // namespace

UT_TEST(fileapi, format_initial_state, "fformat 后当前目录为 / 且只有根目录打开")
{
    FsFixture f;

    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/"));
    UT_CHECK_EQ(f.i_table.size(), 1);
    UT_CHECK_EQ(f.f_table.size(), 1);
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 0);

    // 根目录自身是一个目录文件, 占一个数据盘块
    InodeMirror root = inodeOf(f.disk, currentDirInode(f));
    UT_CHECK_EQ(root.d_mode, FILE_MODE::dir_file);
    UT_CHECK_EQ(root.d_size, 0);
    UT_CHECK_MSG(root.d_addr[0] >= FILE_BLOCK_START, "根目录应占用一个数据盘块");
}

UT_TEST(fileapi, mkdir_visible_in_ls, "mkdir 建立的目录能在 ls 中看到")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_CONTAINS(lsOutput(f), "alpha");
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);

    // 目录项占 2 字节, 父目录的 d_size 应随之增长
    UT_CHECK_EQ(inodeOf(f.disk, currentDirInode(f)).d_size, DIR_ENTRY_SIZE);

    // 新目录自己有 inode 和数据块, 但还没有子文件
    int sub = subInodeAt(f, 0);
    InodeMirror m = inodeOf(f.disk, sub);
    UT_CHECK_EQ(m.d_mode, FILE_MODE::dir_file);
    UT_CHECK_EQ(m.d_size, 0);
}

UT_TEST(fileapi, duplicate_mkdir_rejected, "重名 mkdir 被拒绝, 不产生第二个目录项")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    SuperBlockMirror after_first = superBlockOf(f.disk, f.sblk);

    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(countOccurrences(lsOutput(f), "alpha"), 1);
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);

    // 被拒绝时不应消耗 inode
    SuperBlockMirror after_second = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(after_second.s_ninode, after_first.s_ninode);
}

UT_TEST(fileapi, cd_enters_and_leaves, "cd 进入子目录, cd .. 返回父目录")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/"));

    cd("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    // getCurrentFullPath 逐级拼接时给每级都补了 "/", 因此非根目录带尾斜杠
    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/alpha/"));
    UT_CHECK_EQ(f.i_table.size(), 2);   // 根目录 + alpha

    cd("..", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/"));
    UT_CHECK_EQ(f.i_table.size(), 1);   // alpha 已被移出内存 inode 表
    UT_CHECK_EQ(f.f_table.size(), 1);
}

UT_TEST(fileapi, cd_dot_is_noop, "cd . 不改变当前目录")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    cd("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    cd(".", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/alpha/"));
}

UT_TEST(fileapi, cd_missing_dir_has_no_side_effect, "cd 不存在的目录不改变当前目录")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    const int before_dir = f.i_table.getCurrentDir();
    const int before_size = f.i_table.size();

    cd("nope", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(f.i_table.getCurrentDir(), before_dir);
    UT_CHECK_EQ(f.i_table.size(), before_size);
    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/"));
}

UT_TEST(fileapi, cd_root_parent_is_noop, "在根目录 cd .. 应停住不动")
{
    FsFixture f;
    cd("..", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(f.i_table.getCurrentFullPath(f.disk), std::string("/"));
    UT_CHECK_EQ(f.i_table.size(), 1);   // 不能把根目录自己也关掉
}

UT_TEST(fileapi, fcreat_and_duplicate, "fcreat 建立文件, 重名被拒绝")
{
    FsFixture f;
    fcreat("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_CONTAINS(lsOutput(f), "alpha");

    InodeMirror m = inodeOf(f.disk, subInodeAt(f, 0));
    UT_CHECK_EQ(m.d_mode, FILE_MODE::normal_file);
    UT_CHECK_EQ(m.d_size, 100);
    UT_CHECK_MSG(m.d_addr[0] >= FILE_BLOCK_START, "100 字节应分配到 1 个数据盘块");

    SuperBlockMirror before = superBlockOf(f.disk, f.sblk);
    fcreat("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(countOccurrences(lsOutput(f), "alpha"), 1);
    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(after.s_ninode, before.s_ninode);
}

UT_TEST(fileapi, open_close_table_accounting, "fopen/fclose 正确增减打开文件表")
{
    FsFixture f;
    fcreat("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(f.f_table.size(), 1);   // 只有根目录

    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(f.f_table.size(), 2);
    UT_CHECK_EQ(f.i_table.size(), 2);
    UT_CHECK_EQ(f.f_table.getOffset(subInodeAt(f, 0)), 0);   // 新打开的文件指针归零

    fclose("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(f.f_table.size(), 1);
    UT_CHECK_EQ(f.i_table.size(), 1);
}

UT_TEST(fileapi, repeated_open_and_close_are_safe, "重复 fopen/fclose 不重复计数也不出错")
{
    FsFixture f;
    fcreat("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table);

    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);   // 已打开, 应被忽略
    UT_CHECK_EQ(f.f_table.size(), 2);

    fclose("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    fclose("alpha", f.disk, f.sblk, f.i_table, f.f_table);  // 未打开, 应被忽略
    UT_CHECK_EQ(f.f_table.size(), 1);
    UT_CHECK_EQ(f.i_table.size(), 1);
}

UT_TEST(fileapi, write_then_read_roundtrip, "fwrite/fread 逐字节往返一致")
{
    FsFixture f;
    const std::string content = pattern(100);
    fcreat("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    std::string buf = content;
    fwrite("alpha", buf, static_cast<int>(buf.size()),
           f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);

    // 写完指针应停在末尾
    UT_CHECK_EQ(f.f_table.getOffset(subInodeAt(f, 0)), 100);

    flseek("alpha", 0, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_EQ(fread("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr),
                content);
}

UT_TEST(fileapi, fseek_then_read_from_offset, "flseek 后从指定位置开始读")
{
    FsFixture f;
    const std::string content = pattern(100);
    fcreat("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    std::string buf = content;
    fwrite("alpha", buf, static_cast<int>(buf.size()),
           f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr);

    UT_CHECK_EQ(flseek("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table), 10);
    UT_CHECK_EQ(fread("alpha", 20, f.disk, f.sblk, f.i_table, f.f_table, f.b_mgr),
                content.substr(10, 20));
}

UT_TEST(fileapi, fseek_beyond_end_clamps, "flseek 越界应定位到文件尾")
{
    FsFixture f;
    fcreat("alpha", 100, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(flseek("alpha", 9999, f.disk, f.sblk, f.i_table, f.f_table), 100);
    // 负偏移表示"查询当前指针", 不应移动
    UT_CHECK_EQ(flseek("alpha", -1, f.disk, f.sblk, f.i_table, f.f_table), 100);
}

UT_TEST(fileapi, fdelete_file_recycles_inode, "fdelete 删除文件后 ls 不可见且 inode 被回收")
{
    FsFixture f;
    fcreat("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table);
    const int alpha_inode = subInodeAt(f, 0);

    SuperBlockMirror before = superBlockOf(f.disk, f.sblk);
    fdelete("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_NOT_CONTAINS(lsOutput(f), "alpha");
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 0);
    UT_CHECK_EQ(inodeOf(f.disk, currentDirInode(f)).d_size, 0);

    SuperBlockMirror after = superBlockOf(f.disk, f.sblk);
    UT_CHECK_EQ(after.s_ninode, before.s_ninode + 1);
    UT_CHECK_EQ(after.s_inode[after.s_ninode - 1], alpha_inode);
}

UT_TEST(fileapi, fdelete_refuses_open_file, "fdelete 拒绝删除未关闭的文件")
{
    FsFixture f;
    fcreat("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    fdelete("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_CONTAINS(lsOutput(f), "alpha");   // 仍然在
    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 1);
}

UT_TEST(fileapi, delete_all_then_recreate, "清空目录后仍能重新建文件")
{
    FsFixture f;
    for (int i = 0; i < 3; i++)
        fcreat(std::string("f") + static_cast<char>('a' + i), 50,
               f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 3);

    for (int i = 0; i < 3; i++)
        fdelete(std::string("f") + static_cast<char>('a' + i),
                f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_EQ(getCurrentDirSubFileNum(f.disk, f.i_table), 0);
    UT_CHECK_NOT_CONTAINS(lsOutput(f), "fa");

    // 回收过的 inode 应能被再次分配
    fcreat("fb", 50, f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_CONTAINS(lsOutput(f), "fb");
}

// fdelete 非空目录: 应当拒绝, 且目录项与父目录大小都原封不动
//
// 摘目录项与"目录是否为空"的检查有先后之分: 目录项一旦被后面的项前移覆盖, 目录就
// 不可达了, 它的 inode 与盘块再也回收不了。所以先判后摘 —— 打印"文件夹非空"时,
// 目录还必须是完好的, 删不掉也仍然找得到。
UT_TEST(fileapi, fdelete_nonempty_dir_keeps_entry, "fdelete 非空目录应保留目录项")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    cd("alpha", f.disk, f.sblk, f.i_table, f.f_table);
    fcreat("beta", 10, f.disk, f.sblk, f.i_table, f.f_table);
    cd("..", f.disk, f.sblk, f.i_table, f.f_table);

    UT_CHECK_CONTAINS(lsOutput(f), "alpha");
    UT_CHECK_EQ(inodeOf(f.disk, currentDirInode(f)).d_size, DIR_ENTRY_SIZE);

    fdelete("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    // 正确行为: 删不掉, 目录项和父目录大小都应保持不变
    UT_CHECK_MSG(getCurrentDirSubFileNum(f.disk, f.i_table) == 1,
                 "非空目录未被删除, 父目录的子文件数应仍为 1");
    UT_CHECK_CONTAINS(lsOutput(f), "alpha");
    UT_CHECK_EQ(inodeOf(f.disk, currentDirInode(f)).d_size, DIR_ENTRY_SIZE);
}

// fdelete 空目录: 它自己那个数据块也要回收
//
// 新建目录只 appendBlk 一个数据块, 不调用 changeSize, 因此 d_size 恒为 0 ——
// 按 d_size 折算要释放几块会得到 0。所以 releaseAllBlk 在 d_size 反推出 0 块时,
// 若 d_addr[0] 落在数据区就补一个下限。这条下限同样要兜住 0 字节的普通文件:
// 它也是 d_size==0 却占着一块 (目录项写在首块开头)。
UT_TEST(fileapi, fdelete_empty_dir_frees_block, "fdelete 空目录应回收其数据块")
{
    FsFixture f;
    mkdir("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    const int alpha_inode = subInodeAt(f, 0);
    const int blk = inodeOf(f.disk, alpha_inode).d_addr[0];
    UT_CHECK_MSG(blk >= FILE_BLOCK_START, "新建目录应已分配一个数据块");

    fdelete("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    // 该块回到空闲链顶端, 于是下一次分配正好拿回它
    UT_CHECK_EQ(f.sblk.distributeBlk(f.disk), blk);
}

// fdelete 只应受"要删的那个文件是否打开"影响
//
// "文件未关闭"的检查针对的是被删文件, 所以它必须落在名字比对之后: 若在比对之前
// 检查, 目录里只要存在任何一个打开的文件, 删除另一个已关闭的文件也会被拒绝。
// 名字比对放最前, 不匹配的子文件连 inode 都不必读。
UT_TEST(fileapi, fdelete_ignores_unrelated_open_file, "删除已关闭文件不应受其他打开文件影响")
{
    FsFixture f;
    fcreat("beta", 10, f.disk, f.sblk, f.i_table, f.f_table);
    fcreat("alpha", 10, f.disk, f.sblk, f.i_table, f.f_table);
    fopen("beta", f.disk, f.sblk, f.i_table, f.f_table);   // 打开的是 beta

    fdelete("alpha", f.disk, f.sblk, f.i_table, f.f_table);

    // alpha 本身是关着的, 应当被正常删除
    UT_CHECK_NOT_CONTAINS(lsOutput(f), "alpha");
}

// fcreat 的大小参数必须是个正经的非负整数。
//
// fcreat 的大小参数必须解析成一个非负整数
//
// atoi 对 "abc" / "kb" / "0x10" / "+0" 一律返回 0, 于是"敲错了参数"会被静默
// 当成"大小为 0"。这里固化解析这一层的判据: 合法的只有纯十进制非负整数。
UT_TEST(fileapi, size_argument_must_be_numeric, "fcreat 的大小参数必须是非负整数")
{
    int v = -1;

    UT_CHECK_MSG(parseNonNegInt("0", v) && v == 0, "\"0\" 应被接受");
    UT_CHECK_MSG(parseNonNegInt("512", v) && v == 512, "\"512\" 应被接受");
    UT_CHECK_MSG(parseNonNegInt("0000", v) && v == 0, "\"0000\" 应被接受并解析为 0");
    UT_CHECK_MSG(parseNonNegInt("+7", v) && v == 7, "\"+7\" 应被接受");

    UT_CHECK_MSG(!parseNonNegInt("", v), "空串应被拒绝");
    UT_CHECK_MSG(!parseNonNegInt("abc", v), "\"abc\" 不是数字, 不能静默当成 0");
    UT_CHECK_MSG(!parseNonNegInt("kb", v), "\"kb\" 应被拒绝");
    UT_CHECK_MSG(!parseNonNegInt("0x10", v), "\"0x10\" 应被拒绝, 而不是解析成 0");
    UT_CHECK_MSG(!parseNonNegInt("1.5", v), "\"1.5\" 应被拒绝");
    UT_CHECK_MSG(!parseNonNegInt("12a", v), "带尾随字符的 \"12a\" 应被拒绝");
    UT_CHECK_MSG(!parseNonNegInt("-5", v), "负数应被拒绝 (0 字节合法, 负数不合法)");
    UT_CHECK_MSG(!parseNonNegInt("+", v), "只有符号没有数字应被拒绝");
    UT_CHECK_MSG(!parseNonNegInt("99999999999999999999", v),
                 "溢出 int 的值应被拒绝, 而不是截断");
}

// 0 字节文件的盘块分配。
//
// 目录项写在文件首块开头, 所以申报 0 字节也得占一块 —— 否则 d_addr[0] 停在 -1,
// BMap 返回 -1, 目录项就没地方写了。这条从 fcreat 一侧固化该不变式;
// 打坏别人 inode 的那个后果见 test_boundary.cpp 的同名用例。
UT_TEST(fileapi, zero_size_file_still_gets_first_block, "0 字节文件仍应有首块, 且 d_size 为 0")
{
    FsFixture f;
    fcreat("empty", 0, f.disk, f.sblk, f.i_table, f.f_table);

    const int ino = subInodeAt(f, 0);
    const InodeMirror m = inodeOf(f.disk, ino);
    UT_CHECK_MSG(m.d_addr[0] >= FILE_BLOCK_START,
                 "0 字节文件没有首块, d_addr[0] = " + ut::to_str(m.d_addr[0]));
    UT_CHECK_EQ(m.d_size, 0);

    // 占了一块不等于写了内容: 首块开头只有目录项, 文件名因此在 ls 里可见
    UT_CHECK_CONTAINS(lsOutput(f), "empty");

    // 打开路径要读它的首块, 所以 0 字节的文件也必须能正常打开, 指针停在 0
    const int before = f.f_table.size();
    fopen("empty", f.disk, f.sblk, f.i_table, f.f_table);
    UT_CHECK_MSG(f.f_table.size() == before + 1, "0 字节的文件无法打开");
    UT_CHECK_EQ(f.f_table.getOffset(ino), 0);
}
