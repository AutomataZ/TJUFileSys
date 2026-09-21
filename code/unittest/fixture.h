#pragma once
#include "define.h"
#include "shell.h"
#include "mirror.h"
#include "wirteDisk/wirteDisk.h"
#include <sstream>
#include <iostream>
#include <cstdlib>

// 测试固件: 每个用例构造一个独立的文件系统上下文
//
// 镜像路径刻意不用 define.h 里的 disk_name(那是 "../../myDisk.img",
// 相对路径且只给演示程序用), 而是当前工作目录下的 unittest.img。
// 因此测试既不依赖运行目录, 也不会污染演示程序的镜像。

inline const char* ut_image_path()
{
    return "unittest.img";
}

// 建立(或清空)4MB 的测试镜像
inline void createImageFile()
{
    if (!DiskFile::create(ut_image_path(), DISK_SIZE))
    {
        std::cerr << "无法建立测试镜像 " << ut_image_path() << std::endl;
        std::exit(2);
    }
}

class FsFixture {
public:
    DiskFile disk;
    SuperBlock sblk;
    MemInodeTable i_table;
    OpenFileTable f_table;
    BufferMgr b_mgr;

    explicit FsFixture(bool do_format = true)
    {
        createImageFile();
        if (!disk.open(ut_image_path()))
        {
            std::cerr << "无法打开测试镜像 " << ut_image_path() << std::endl;
            std::exit(2);
        }
        if (do_format)
            fformat(disk, sblk, i_table, f_table, b_mgr);
    }

    ~FsFixture()
    {
        disk.close();
    }

    // 磁盘流不可复制, 固件对象也不应被复制
    FsFixture(const FsFixture&) = delete;
    FsFixture& operator=(const FsFixture&) = delete;

    void reformat()
    {
        fformat(disk, sblk, i_table, f_table, b_mgr);
    }
};

// ---- 捕获命令输出 ----
// ls 等命令直接往 cout 打印, 断言其输出需要临时替换流缓冲

class CaptureCout {
public:
    CaptureCout() : old_(std::cout.rdbuf(buf_.rdbuf())) {}
    ~CaptureCout() { std::cout.rdbuf(old_); }
    std::string str() const { return buf_.str(); }
private:
    std::ostringstream buf_;
    std::streambuf* old_;
    CaptureCout(const CaptureCout&) = delete;
    CaptureCout& operator=(const CaptureCout&) = delete;
};

// ---- 常用断言辅助 ----

// 读磁盘上某个 inode 的镜像
inline InodeMirror inodeOf(DiskFile& disk, int inode_index)
{
    InodeMirror m;
    readInodeMirror(disk, inode_index, m);
    return m;
}

// 读当前内存 superblock 的镜像
inline SuperBlockMirror superBlockOf(DiskFile& disk, SuperBlock& sblk)
{
    SuperBlockMirror m;
    snapshotSuperBlock(disk, sblk, m);
    return m;
}

// ---- 盘块字节读取 ----
//
// 块号是绝对块号, 换算成字节偏移就是 blkno * BYTE_PER_BLOCK 这一个式子 ——
// 与 define.h 中 FILE_AREA_OFFSET 处的说明一致, 那里也是全系统唯一的换算口径。

inline std::string readBlockBytes(DiskFile& disk, int blkno, int n)
{
    std::string s(n, '\0');
    readDisk(disk, &s[0], n, blkno * BYTE_PER_BLOCK);
    return s;
}
