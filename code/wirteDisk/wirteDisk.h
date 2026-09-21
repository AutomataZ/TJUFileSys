#pragma once
#include <string>

/// @brief 模拟磁盘: 一个打开着的镜像文件
///
/// 这是全系统唯一的设备。用文件描述符而不是流, 是因为流有用户态缓冲: "写下去"只到了
/// 缓冲区, 与"落到设备上"是两回事, 而掉电保护要的正是后者 —— sync() 需要拿到 fd。
///
/// 位置是读写共用的一个: seek() 之后接着 read/write 就落在那里, 与流的行为一致
/// (readDisk/writeDisk 依赖这一点)。
class DiskFile
{
public:
    DiskFile();
    ~DiskFile();

    /// @brief 建立(或清空)一个 size 字节的镜像文件
    static bool create(const std::string& path, int size);
    /// @brief 打开已有的镜像文件, 不创建
    bool open(const std::string& path);
    bool isOpen() const { return fd >= 0; }
    void close();

    /// @brief 把已经写下去的数据真正送到设备上 (fsync)
    ///
    /// 提交点用。它保证的是"此前已经完成的写都落到设备上了", 不保证正在飞的写是原子的 ——
    /// 一次跨扇区的写掉电时仍可能只落一半。
    void sync();

    int read(void* buffer, int size);
    int write(const void* buffer, int size);
    void seek(int offset);

private:
    int fd;

    // 一个磁盘只有一个 fd, 不该被拷贝: 拷贝出来两个析构函数会关同一个 fd
    DiskFile(const DiskFile&);
    DiskFile& operator=(const DiskFile&);
};

/// @brief 读磁盘
/// @param disk 磁盘
/// @param buffer 读入到buffer中
/// @param size 读入的字节数
/// @param offset 相对于文件开头的偏移量; 负数表示不动位置, 就地读写
void readDisk(DiskFile& disk, void* buffer, int size, int offset);

/// @brief 写磁盘
/// @param disk 磁盘
/// @param buffer 写入的内容
/// @param size 写入的大小，以字节为单位
/// @param offset 相对于文件开头的偏移量; 负数表示不动位置, 就地读写
void writeDisk(DiskFile& disk, void* buffer, int size, int offset);
