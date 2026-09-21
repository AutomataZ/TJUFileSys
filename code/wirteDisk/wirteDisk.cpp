#include "./wirteDisk.h"
#include "../define.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

DiskFile::DiskFile()
{
    fd = -1;
}

DiskFile::~DiskFile()
{
    close();
}

bool DiskFile::create(const std::string& path, int size)
{
    int new_fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (new_fd < 0)
        return false;
    // 一次性撑到规定大小。镜像里没有半截文件的概念, 所以建好之后大小就不再变,
    // 也因此 ftruncate 只在这里出现一次
    if (ftruncate(new_fd, size) != 0)
    {
        ::close(new_fd);
        return false;
    }
    ::close(new_fd);
    return true;
}

bool DiskFile::open(const std::string& path)
{
    close();
    fd = ::open(path.c_str(), O_RDWR);
    return fd >= 0;
}

void DiskFile::close()
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

void DiskFile::sync()
{
    if (fd >= 0)
    {
        fsync(fd);
    }
}

int DiskFile::read(void* buffer, int size)
{
    if (fd < 0)
        return -1;
    return (int)::read(fd, buffer, (size_t)size);
}

int DiskFile::write(const void* buffer, int size)
{
    if (fd < 0)
        return -1;

    // write 可能少写(信号、管道、磁盘限额), 剩下的自己接着写完, 否则调用方会
    // 以为整块都写下去了
    const char* p = (const char*)buffer;
    int done = 0;
    while (done < size)
    {
        ssize_t n = ::write(fd, p + done, (size_t)(size - done));
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;
            return done;
        }
        done += (int)n;
    }
    return done;
}

void DiskFile::seek(int offset)
{
    if (fd >= 0)
    {
        lseek(fd, (off_t)offset, SEEK_SET);
    }
}

void readDisk(DiskFile& disk, void* buffer, int size, int offset)
{
    if (offset >= 0)
    {
        disk.seek(offset);
    }
    disk.read(buffer, size);
}

void writeDisk(DiskFile& disk, void* buffer, int size, int offset)
{
    if (offset >= 0)
    {
        disk.seek(offset);
    }
    disk.write(buffer, size);
}
