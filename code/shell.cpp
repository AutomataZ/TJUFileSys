#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <climits>
#include <string>
#include "./shell.h"
#include "format.h"
#include "./wirteDisk/wirteDisk.h"
#include "inode.h"
#include "filedir.h"

using namespace std;

void Shell::init(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 读入磁盘的前1024字节作为superblock结构
    readDisk(disk, &sblk, sizeof(SuperBlock), 0);
    // 打开根目录 /
    // 逐个搜索inode，判断是否为根目录对应的inode
    MemInode inode;
    FileDir dir;
    int flag = 0;
    for (int i = 0; i < INODE_NUM; i++)
    {
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + i * sizeof(Inode));
        inode.i_number = i;
        int blk_index = inode.BMap(disk, 0);
        readDisk(disk, &dir, sizeof(FileDir), blk_index * BYTE_PER_BLOCK);
        if (dir.is_root())
        {
            flag = 1;
            break;
        }
    }
    if (!flag)
    {
        cout << "文件系统格式损坏, 请重新格式化" << endl;
        return;
    }
    if (!dir.is_open(i_table, f_table))
    {
        dir.open(disk, i_table, f_table);
        //当前目录设置为 /
        i_table.modifyCurrentDir(i_table.find(inode.i_number));
    }
}

//当前目录对应的物理块号
int getCurrentBlk(DiskFile& disk, MemInodeTable& i_table)
{
    int index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.BMap(disk, 0);
}

int getCurrentDirSubFileNum(DiskFile& disk, MemInodeTable& i_table)
{
    int index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.getSize() / 2;
}

int getFileModeByInodeIndex(DiskFile& disk, int index)
{
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    return inode.getMode();
}

string getFileNameByInodeIndex(DiskFile& disk, int index)
{
    Inode inode;
    readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
    int blkno = inode.BMap(disk, 0); //物理块号
    FileDir dir;
    readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
    return dir.getFileName();
}

void newFile(int mode, string name, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table, BufferMgr& b_mgr, int size)
{
    // size 只在 normal_file 分支参与分配。负数会让分配循环算出负的盘块数,
    // 并把 d_size 一并推成负数, 在这里直接拒绝
    if (mode == FILE_MODE::normal_file && size < 0)
    {
        cout << "文件大小不能为负数: " << size << endl;
        return;
    }

    // 查找当前目录下是否存在同名同类型的文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            mode == getFileModeByInodeIndex(disk, inode_index))
        {
            if (mode == FILE_MODE::dir_file)
                cout << "目录";
            else if (mode == FILE_MODE::normal_file)
                cout << "文件";
            cout << name << "已存在" << endl;
            return;
        }
    }

    // 申请一个空闲 inode
    Inode new_inode;
    int16_t inode_index = sblk.distributeInode(disk);

    // 分配之前先把 inode 体擦干净。这个 inode 可能是从别的文件回收来的, 盘上的体里
    // 还留着上一个主人的 d_size 与 d_addr —— 而它的盘块此时已经回到空闲链上了。
    // 不擦的话, 下面那次同步一旦落盘, 盘上就出现"一个 inode 声称有 N 字节, 而那些
    // 字节所在的盘块正被空闲链列着待分配"的状态。擦成"零字节、没有块"之后, 崩溃
    // 最坏是白占一个 inode 号, 不会有任何一块被两个主人同时指着
    if (inode_index >= 0)
    {
        Inode blank;
        blank.setMode(mode);
        writeDisk(disk, &blank, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
    }

    if (mode == FILE_MODE::dir_file)
    {
        // 申请一个空闲盘块
        int blk_index = sblk.distributeBlk(disk, b_mgr);
        // 将分配到的空闲盘块关联到分配到的inode
        new_inode.appendBlk(disk, sblk, inode_index, blk_index, b_mgr);
        new_inode.setMode(mode);
    }
    else if (mode == FILE_MODE::normal_file)
    {
        // 需要分配几个盘块: 首块开头 16 字节是自己的目录项, 装不下内容, 所以是
        // ceil((size + 16) / 512) —— blocksForFileContent 算的就是这个容量式
        int blk_num = blocksForFileContent(size);
        // 申报 0 字节时上面算出 0 块, 但文件至少要占一块: 目录项(文件名)就写在
        // 文件首块的开头, 没有块就没有地方写名字。
        if (blk_num == 0)
            blk_num = 1;

        for (int i = 0; i < blk_num; i++)
        {
            int blk_index = sblk.distributeBlk(disk, b_mgr);
            new_inode.appendBlk(disk, sblk, inode_index, blk_index, b_mgr);
            // 加完这一块之后 d_size 该是多少: 中间各块填满(装下 B 块的文件是
            // B*512 - 16 字节, 每块开头那 16 字节是目录项, 不装内容), 最后一块
            // 收在申报的 size 上。changeSize 是增量, 所以这里递推地补差值。
            int target = (i == blk_num - 1) ? size : (i + 1) * BYTE_PER_BLOCK - FILE_DIR_SIZE;
            new_inode.changeSize(target - new_inode.getSize());
        }
        new_inode.setMode(mode);
    }

    // 提交点: 先把"这个 inode 和这些盘块已经有主了"落到设备上。空闲 inode 表与空闲
    // 块链在分配时就已经写下去了, 但还在系统缓存里; 掉电丢掉它们, 这些资源重启后会被
    // 当成空闲的再分一次, 两个文件共用同一块
    disk.sync();

    // 将inode内容写入磁盘: d_addr 已由 appendBlk 逐个写进盘上了, 这一次补上
    // d_mode 与 d_size
    writeDisk(disk, &new_inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));

    // 在空闲盘块上新建文件目录项
    FileDir dir(name, inode_index, i_table.inode[i_table.getCurrentDir()].i_number);
    dir.create(disk, new_inode.BMap(disk, 0));

    // 提交点: 文件本体(自己的 inode 与首块里的目录项)落盘。父目录接下来就要指向它,
    // 这一步落不下去, 掉电后父目录里会留下一个指向空 inode 的名字
    disk.sync();

    // 登记进父目录放在最后: 父目录里一出现这个 inode 号, 文件就可达了 —— ls 会拿
    // BMap(disk, 0) 去读首块里的目录项。所以 inode 本体(含 d_addr[0])与首块里的
    // 目录项都要先落盘, 再让父目录指向它
    writeDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + sub_file_num * sizeof(int16_t));
    int c_inode_index = i_table.inode[i_table.getCurrentDir()].i_number;
    Inode c_inode;
    readDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));
    c_inode.addSubDirSize();
    writeDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));

    // 提交点: 父目录指向它, 是这条路径的最后一步。顺序不能反 —— 反过来掉电会留下
    // "父目录的 d_size 说有 N 项, 第 N 项却还是上一轮留下的陈旧 inode 号"的幻影
    // 目录项, 那个 inode 号早就属于别的文件了
    disk.sync();
}

int openCloseFile(int mode, string name, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
            if (dir.is_open(i_table, f_table) && mode == OpenClose::open)
            {
                cout << "文件" << name << "已打开" << endl;
                return 0;
            }
            else if (!dir.is_open(i_table, f_table) && mode == OpenClose::close)
            {
                cout << "文件" << name << "未打开" << endl;
                return 0;
            }
            else if (mode == OpenClose::open)
            {
                //打开
                dir.open(disk, i_table, f_table);
                return 1;
            }
            else if (mode == OpenClose::close)
            {
                //关闭
                dir.close(disk, i_table, f_table);
                return 1;
            }
        }
    }
    cout << "当前路径下不存在文件" << name << endl;
    return 0;
}

std::string readWriteFile(int mode, string name, string& str, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    std::string ret;
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index; //对应文件的inode标号
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (name == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            //blkno是该文件的起始块
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
            if (!dir.is_open(i_table, f_table))
            {
                cout << "文件" << name << "未打开" << endl;
                return ret;
            }
            else
            {
                //取得文件指针
                int cur = f_table.find(inode_index);
                int pointer_offset = f_table.file[cur].f_offset; //文件指针的位置
                int start_offset = blkno * BYTE_PER_BLOCK + sizeof(FileDir) + pointer_offset; //读写开始的位置
                //cout << "读写开始的位置是" << start_offset << endl;
                //int start_blk = start_offset / BYTE_PER_BLOCK + !!(start_offset % BYTE_PER_BLOCK); //读写开始的块号
                int start_blk = inode.BMap(disk, (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK);
                //cout << "start_blk是" << start_blk << endl;

                int start_cur = start_offset % BYTE_PER_BLOCK; //在块内，读写开始的位置
                int end_cur = (start_cur + size - 1) % BYTE_PER_BLOCK; //在块内，读写结束的位置
                int blk_num = blocksSpanned(start_cur, size); //需要读入的块数

                // 扩容路径的标志: 它决定了传输循环跑完之后要不要重写 inode 头部。
                // 之所以把那次写挪到循环后面, 是为了让内容先落盘 (见循环之后)
                bool grew = false;

                if (pointer_offset + size > inode.getSize()) //如果读写大小超过了文件上限
                {
                    //如果读大小超过文件上限
                    if (mode == ReadWrite::read)
                    {
                        //size修改为文件上限减去读写开始位置
                        size = inode.getSize() - pointer_offset;
                        end_cur = (start_cur + size - 1) % BYTE_PER_BLOCK;
                        blk_num = blocksSpanned(start_cur, size);
                    }
                    //如果写大小超过文件上限
                    else if (mode == ReadWrite::write)
                    {
                        // 扩容后的文件大小: 要装下"读写指针位置 + 本次写入"
                        const int new_size = pointer_offset + size;

                        // 现有的块数。0 字节文件也占着首块(目录项在里面), 至少按 1 块算
                        int old_blk_num = blocksForFileContent(inode.getSize());
                        if (old_blk_num == 0)
                            old_blk_num = 1;

                        // 先把 d_size 补齐到"已占 old_blk_num 块"的容量, 再往上加。
                        // appendBlk 是按 d_size 折算出的块数挑槽位的, d_size 与已占块数
                        // 对不上就会挑错槽位。0 字节文件正是这种情况: d_size 是 0, 折算
                        // 出 0 块, 第一次 appendBlk 会以为第 0 个槽位还空着, 把新块写进
                        // d_addr[0], 覆盖掉原首块。
                        inode.changeSize(old_blk_num * BYTE_PER_BLOCK - FILE_DIR_SIZE - inode.getSize());

                        // 目标块数 - 现有块数, 同样按 blocksForFileContent 折算
                        const int new_blk_num = blocksForFileContent(new_size) - old_blk_num;
                        //cout << "新申请" << new_blk_num << "个盘块" << endl;
                        for (int i = 0; i < new_blk_num; i++)
                        {
                            int blk_index = sblk.distributeBlk(disk, b_mgr);
                            inode.appendBlk(disk, sblk, inode_index, blk_index, b_mgr);
                            //cout << "新申请了盘块" << blk_index << endl;
                            // 同 newFile: 加完这一块之后 d_size 该是"装下
                            // old_blk_num + i + 1 块"的字节数。changeSize 是增量,
                            // 所以补的是目标值与当前值的差
                            int target = (old_blk_num + i + 1) * BYTE_PER_BLOCK - FILE_DIR_SIZE;
                            inode.changeSize(target - inode.getSize());
                        }
                        // 盘块备齐, d_size 落到准确的字节数
                        inode.changeSize(new_size - inode.getSize());
                        // 带新 d_size 的 inode 头部等到传输循环之后再写: d_size 一旦
                        // 落盘, 它就声明"这个文件有 new_size 字节", 那些字节必须已经
                        // 在设备上。这里只是把它标出来
                        grew = true;

                        //cout << "文件新大小为" << inode.getSize() << endl;
                    }
                }
                int offset = 0; //写指针, 记录已经写了多少个字节
                //根据文件的物理位置、文件指针位置、size确定需要读入的块
                for (int i = 0; i < blk_num; i++)
                {
                    //cout << i << ": 对" << inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK) << "操作" << endl;
                    int start = 0;
                    int end = BYTE_PER_BLOCK - 1;
                    if (i == 0) //首个物理块不一定从0开始
                    {
                        start = start_cur;
                    }
                    if (i == blk_num - 1) //最后一个物理块不一定从512结束
                    {
                        end = end_cur;
                    }
                    //cout << "开始" << start << "结束" << end << endl;
                    if (mode == ReadWrite::read)
                    {
                        //读入是从缓存中读入
                        //cout << "读到了: ";
                        char* sub = b_mgr.Bread(disk, inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK))->getLoad();
                        for (int i = start; i <= end; i++)
                            ret += sub[i];
                        //cout << ret << endl;
                        //ret += b_mgr.Bread(disk, inode.BMap(disk, i))->getLoad();
                        //cout << b_mgr.Bread(disk, inode.BMap(disk, i))->getLoad() << endl;
                        //b_mgr.bq.print();
                    }
                    else if (mode == ReadWrite::write)
                    {
                        //要写入的部分是从offset开始，end - start个
                        string buffer;
                        for (int i = offset; i <= offset + end - start; i++)
                        {
                            buffer += str[i];
                        }
                        //写入是写缓存
                        b_mgr.Bwrite(disk, inode.BMap(disk, i + (sizeof(FileDir) + pointer_offset) / BYTE_PER_BLOCK), buffer, start, end - start + 1);
                        offset += end - start + 1;
                    }
                    //更改文件读写指针
                    f_table.setOffset(inode_index, f_table.getOffset(inode_index) + end - start + 1);
                    //cout << "当前文件指针在" << f_table.getOffset(inode_index) << endl;
                }

                if (grew)
                {
                    // 提交点, 顺序是这里的关键: 内容经缓存写下去, 先 flush 再 sync 把
                    // 它推到设备上, 然后才写 d_size。反过来的话, 掉电后盘上的 inode
                    // 会说"这个文件有 N 字节", 而它指向的块里还留着上一个主人的内容 ——
                    // 读出来的是别人的数据, 不是丢一次写那么简单
                    b_mgr.flush(disk);
                    disk.sync();
                    // 将inode内容写入磁盘
                    writeDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
                    // 更新内存inode
                    readDisk(disk, &i_table.inode[i_table.find(inode_index)], sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
                    disk.sync();
                }
                return ret;
            }
            return ret;
        }
    }
    cout << "当前路径下不存在文件" << name << endl;
    return ret;
}

/*
    将用户的一行输入转换为命令和参数
    返回 0 表示有错
    返回 1 表示正常转换
    不检查命令是否正确
*/
int inputToCmd(const string& input, string& cmd, string(&args)[MAX_ARGS_NUM])
{
    stringstream ss(input);
    ss >> cmd;
    if (cmd == "")
        return 0;
    for (int i = 0; i < MAX_ARGS_NUM; i++)
        ss >> args[i];
    return 1;
}

/*
    把参数解析成一个非负整数, 成功返回 1, 失败返回 0。

    不能直接用 atoi: 它对 "abc" / "kb" / "0x10" / "+0" / "" 一律返回 0,
    于是"敲错了"会被静默当成"大小为 0"。这里要求整个字符串都是数字, 非数字
    的参数一律判失败, 由调用方报用法错误。
*/
int parseNonNegInt(const string& s, int& out)
{
    if (s.empty())
        return 0;
    size_t i = (s[0] == '+') ? 1 : 0;   // 允许单个前导 '+', 与 atoi 的习惯一致
    if (i >= s.size())
        return 0;
    for (size_t j = i; j < s.size(); j++)
    {
        if (!isdigit(static_cast<unsigned char>(s[j])))
            return 0;
    }

    // 不用 atoi: 溢出时它是未定义行为。宁可把 "99999999999999999999"
    // 判成非法参数, 也不要拿到一个截断后的值去分配盘块。
    errno = 0;
    long v = strtol(s.c_str(), nullptr, 10);
    if (errno == ERANGE || v < 0 || v > INT_MAX)
        return 0;
    out = static_cast<int>(v);
    return 1;
}

void fformat(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    //清空所有打开项
    i_table.clear();
    f_table.clear();
    //缓存队列清空
    b_mgr.clear(disk);
    //格式化
    diskFormat(disk, sblk, i_table, f_table, b_mgr);
}

void ls(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table)
{
    // 找到当前目录对应的物理块
    int c_dir_index = getCurrentBlk(disk, i_table);
    // 获得当前目录下共有多少个子文件
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);

    // 从磁盘读出所有子文件的inode号
    // 首先计算文件内容开始的地址
    // 人为限制单个文件下子文件数量，保证一个块内可以找完
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        cout << getFileNameByInodeIndex(disk, inode_index) << " ";
    }
    cout << endl;
}

int mkdir(string dirName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& t_table, BufferMgr& b_mgr)
{
    newFile(FILE_MODE::dir_file, dirName, disk, sblk, i_table, t_table, b_mgr, 0);
    return 0;
}

void cd(std::string dirName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    // 查找当前目录下是否存在同名目录
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    //cout << "开始寻找" << endl;
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (dirName == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::dir_file)
        {
            //打开这个目录
            Inode inode; //要打开文件对应的磁盘indoe数据
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            int blkno = inode.BMap(disk, 0); //物理块号
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
            dir.open(disk, i_table, f_table);
            i_table.modifyCurrentDir(i_table.find(inode_index));
            //cout << "当前打开inode是: " << endl;
            //i_table.inode[i_table.find(inode_index)].print();
            return;
        }
    }
    if (dirName == "..") //..是父目录
    {
        //关闭当前目录，并修改当前目录为父目录
        int index = i_table.inode[i_table.getCurrentDir()].i_number; //当前的inode号
        Inode inode; //当前磁盘inode数据
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + index * sizeof(Inode));
        int blkno = inode.BMap(disk, 0); //当前磁盘的物理块号
        FileDir dir;
        readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
        if (dir.is_root()) //根目录没有父目录
            return;
        i_table.erase(disk, dir.getInode());
        // find 找不到时返回 -1, 不加判断就用作下标会越界读到非法数据
        int f_cur = f_table.find(index);
        if (f_cur != -1)
        {
            f_table.erase(disk, f_table.file[f_cur]);
        }
        i_table.modifyCurrentDir(i_table.find(dir.getFaInode()));
        //cout << "当前打开inode是: " << endl;
        //i_table.inode[i_table.find(index)].print();
    }
    else if (dirName == ".") //.什么都不做
    {
        ;
    }
    else //其他情况出错
    {
        cout << "当前路径下不存在子目录" << dirName << endl;
    }
}

int fcreat(string fileName, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    newFile(FILE_MODE::normal_file, fileName, disk, sblk, i_table, f_table, b_mgr, size);
    return 0;
}

int fopen(string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    openCloseFile(OpenClose::open, fileName, disk, sblk, i_table, f_table);
    return 1;
}

int fclose(string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    openCloseFile(OpenClose::close, fileName, disk, sblk, i_table, f_table);

    // 提交点: 关闭即提交。原地覆盖写(不扩容)那条路径在 readWriteFile 里走的是
    // "不写 inode、不落盘"的分支 —— 内容只挂在缓存里, 全靠这里把它推上设备
    b_mgr.flush(disk);
    disk.sync();
    return 1;
}

string fread(string fileName, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    // 首先在文件打开结构中寻找该文件
    if (false)
    {
        cout << "该文件没有打开! " << endl;
        return "";
    }
    else
    {
        // 从文件读写指针的位置开始读入
        string str = "";
        return readWriteFile(ReadWrite::read, fileName, str, size, disk, sblk, i_table, f_table, b_mgr);
    }
}

void fwrite(string fileName, string& buffer, int size, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    // 首先在文件打开结构中寻找该文件
    if (false)
    {
        cout << "该文件没有打开! " << endl;
    }
    else
    {
        readWriteFile(ReadWrite::write, fileName, buffer, size, disk, sblk, i_table, f_table, b_mgr);
    }
}

int flseek(string fileName, int offset, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table)
{
    std::string ret;
    // 查找当前目录下是否存在同名的普通文件
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        int16_t inode_index; //对应文件的inode标号
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));
        if (fileName == getFileNameByInodeIndex(disk, inode_index) && 
            getFileModeByInodeIndex(disk, inode_index) == FILE_MODE::normal_file)
        {
            Inode inode;
            readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
            //blkno是该文件的起始块
            int blkno = inode.BMap(disk, 0);
            FileDir dir;
            readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
            if (!dir.is_open(i_table, f_table))
            {
                cout << "文件" << fileName << "未打开" << endl;
                return -1;
            }
            else
            {
                //如果指定位置为负，什么都不做
                if (offset < 0)
                {
                    return f_table.getOffset(inode_index);
                }
                //如果指定位置超过文件尺寸，定位到文件结尾
                else if (offset > inode.getSize())
                {
                    f_table.setOffset(inode_index, inode.getSize());
                }
                else
                {
                    f_table.setOffset(inode_index, offset);
                }
                return f_table.getOffset(inode_index);
            }
            return -1;
        }
    }
    cout << "当前路径下不存在文件" << fileName << endl;
    return -1;
}

void fdelete(string fileName, DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    int c_dir_index = getCurrentBlk(disk, i_table);
    int sub_file_num = getCurrentDirSubFileNum(disk, i_table);
    int file_content_offset = c_dir_index * BYTE_PER_BLOCK + sizeof(FileDir);
    for (int i = 0; i < sub_file_num; i++)
    {
        //该文件的inode_index
        int16_t inode_index;
        readDisk(disk, &inode_index, sizeof(int16_t), file_content_offset + i * sizeof(int16_t));

        // 先比名字, 不匹配的子文件直接跳过 —— 读 inode、判断"是否打开"都应当留在
        // 这道名字过滤器之内。"是否打开"针对的是要删的那个文件, 若在过滤之前就判断,
        // 目录里只要存在任何一个打开的文件, 删除另一个已关闭的文件也会被拒绝。
        if (fileName != getFileNameByInodeIndex(disk, inode_index))
            continue;

        //取要删除文件的inode
        Inode inode;
        readDisk(disk, &inode, sizeof(Inode), INODE_AREA_OFFSET + inode_index * sizeof(Inode));
        int mode = inode.getMode();

        //不允许删除打开的文件
        int blkno = inode.BMap(disk, 0);
        FileDir dir;
        readDisk(disk, &dir, sizeof(FileDir), blkno * BYTE_PER_BLOCK); //读入目录项
        if (dir.is_open(i_table, f_table))
        {
            cout << "文件" << fileName << "未关闭" << endl;
            return;
        }

        // 目录必须为空才允许删除。这个判断要放在摘目录项之前: 目录项一旦被后面的项
        // 前移覆盖, 目录就不可达了, 它的 inode 与盘块再也回收不了。先判后摘, 被拒绝
        // 的目录保持原样, 仍然看得见、也删得掉。
        if (mode == FILE_MODE::dir_file && inode.getSize() != 0)
        {
            cout << "文件夹" << fileName << "非空" << endl;
            return;
        }

        // 先改父目录的项数, 再搬目录项。顺序反过来是要出事的: 先搬完、项数还没减,
        // 盘上就成了一份"项数说还有 N 项, 而第 N 项的位置上留着上一轮搬过来的重复
        // 项"的目录 —— 那个名字后面被删掉一次, 就会释放掉一个还有第二个目录项指着
        // 的 inode。先减项数的话, 掉电最多让最后一项暂时看不见(纯泄漏), 被删的那个
        // 文件完好无损, 再删一次即可
        int c_inode_index = i_table.inode[i_table.getCurrentDir()].i_number;
        Inode c_inode;
        readDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));
        c_inode.eraseSubDirSize();
        writeDisk(disk, &c_inode, sizeof(Inode), INODE_AREA_OFFSET + c_inode_index * sizeof(Inode));

        // 将之后的子inode号全都向前移动一个
        for (int j = i; j < sub_file_num - 1; j++)
        {
            int16_t tem;
            readDisk(disk, &tem, sizeof(int16_t), file_content_offset + (j + 1) * sizeof(int16_t));
            writeDisk(disk, &tem, sizeof(int16_t), file_content_offset + j * sizeof(int16_t));
        }

        // 提交点: 目录项已经摘掉、父目录的项数也改好了, 这一步之后这个文件就不可达
        // 了。回收必须排在它后面 —— releaseAllBlk 在空闲表满时会往回收来的块里直写
        // 一张分组索引表, 那是一次整块覆盖。要是回收先落了盘而摘除还在缓存里, 掉电后
        // 目录项还在, 文件却已经没地方读了
        disk.sync();

        // 普通文件与空目录到此都可以回收 inode 与盘块了
        inode.releaseAllBlk(disk, sblk, b_mgr);
        sblk.releaseInode(inode_index);

        // 提交点: 回收结果落盘。releaseBlk/releaseInode 只改了内存里那张空闲表,
        // 它自己不会写盘, 所以这里补一次 save —— 不补的话掉电重启后这些块和 inode
        // 仍然被记作"占用", 从此谁也拿不到它们
        sblk.save(disk);
        disk.sync();
        return;
    }
    cout << "当前路径下不存在文件" << fileName << endl;
    return;
}

void printCurrentPath(DiskFile& disk, MemInodeTable& i_table)
{
    cout << "MF " << i_table.getCurrentFullPath(disk) << " > ";
}

Shell::Shell()
{
}

Shell::~Shell()
{
}

void usage()
{
    cout << "fcreat filename filesize" << endl;
}

void Shell::usr(DiskFile& disk, SuperBlock& sblk, MemInodeTable& i_table, OpenFileTable& f_table, BufferMgr& b_mgr)
{
    cout << "欢迎使用 misakifs 文件系统" << endl;
    init(disk, sblk, i_table, f_table);

#ifdef FINAL_TEST

    cout << "格式化文件卷" << endl;
    fformat(disk, sblk, i_table, f_table, b_mgr);
    cout << "格式化完成, 当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "新建bin etc home dev四个子文件夹" << endl;
    mkdir("bin", disk, sblk, i_table, f_table, b_mgr);
    mkdir("etc", disk, sblk, i_table, f_table, b_mgr);
    mkdir("home", disk, sblk, i_table, f_table, b_mgr);
    mkdir("dev", disk, sblk, i_table, f_table, b_mgr);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到home文件夹下" << endl;
    cd("home", disk, sblk, i_table, f_table);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "新建texts reports photos三个子文件夹" << endl;
    mkdir("texts", disk, sblk, i_table, f_table, b_mgr);
    mkdir("reports", disk, sblk, i_table, f_table, b_mgr);
    mkdir("photos", disk, sblk, i_table, f_table, b_mgr);
    cout << "当前目录内容为: " << endl;
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到texts目录下保存报告" << endl;
    cd("texts", disk, sblk, i_table, f_table);
    fcreat("reports.md", 4 * 1024, disk, sblk, i_table, f_table, b_mgr);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到reports目录下保存txt文件" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("reports", disk, sblk, i_table, f_table);
    fcreat("reports.md", 4 * 1024, disk, sblk, i_table, f_table, b_mgr);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "切换到photos目录下保存图片" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("photos", disk, sblk, i_table, f_table);
    fcreat("pic.jpg", 4 * 1024, disk, sblk, i_table, f_table, b_mgr);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    cout << "回到根目录" << endl;
    cd("..", disk, sblk, i_table, f_table);
    cd("..", disk, sblk, i_table, f_table);

    cout << "新建目录test, 并切换至test" << endl;
    mkdir("test", disk, sblk, i_table, f_table, b_mgr);
    cd("test", disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

    char ch[] = {'a', 'A'};
    string str, abc, content, content_new;
    for (int i = 0; i < 800; i++)
        str += ch[i < 500] + i % 26;

    cout << "新建文件Jerry, 并写入800个字节" << endl;
    fcreat("Jerry", 10, disk, sblk, i_table, f_table, b_mgr);
    fopen("Jerry", disk, sblk, i_table, f_table, b_mgr);
    fwrite("Jerry", str, str.length(), disk, sblk, i_table, f_table, b_mgr);

    cout << "当前文件的内容是: " << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    cout << fread("Jerry", str.length(), disk, sblk, i_table, f_table, b_mgr) << endl;

    cout << "定位文件指针到500字节" << endl;
    flseek("Jerry", 500, disk, sblk, i_table, f_table);
    cout << "读出500个字节到abc" << endl;
    abc = fread("Jerry", 500, disk, sblk, i_table, f_table, b_mgr);
    cout << "abc的内容是: " << endl;
    cout << abc << endl;

    cout << "将abc写回文件" << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    fwrite("Jerry", abc, abc.length(), disk, sblk, i_table, f_table, b_mgr);
    cout << "当前文件的内容是: " << endl;
    flseek("Jerry", 0, disk, sblk, i_table, f_table);
    cout << fread("Jerry", str.length(), disk, sblk, i_table, f_table, b_mgr) << endl;

    fclose("Jerry", disk, sblk, i_table, f_table, b_mgr);
    cout << "回到根目录" << endl;
    cd("..", disk, sblk, i_table, f_table);
    printCurrentPath(disk, i_table);
    ls(disk, sblk, i_table, f_table);

#endif

    string usr_input;
    while (true)
    {
        printCurrentPath(disk, i_table);
        //i_table.print();
        //f_table.print();
        string cmd;
        string args[MAX_ARGS_NUM];
        // getline 在输入流结束时会使流进入失败状态, 此时不会再有输入
        // 若不处理, 下面的空串解析会返回 0 并 continue, 导致循环空转不退出
        // 这里等价于输入了 exit, 走退出分支以保证缓存与 superblock 落盘
        if (!getline(cin, usr_input))
        {
            usr_input = "exit";
        }
        if (!inputToCmd(usr_input, cmd, args))
            continue;
        if (cmd == cmd_supported[0]) //fformat
        {
            fformat(disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[1]) //ls
        {
            ls(disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[2]) //mkdir
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            mkdir(args[0], disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[3])
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            cd(args[0], disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[4]) //fcreat
        {
            int size = 0;
            // 文件名和大小都必须给出, 且大小得是个非负整数:
            // "fcreat foo abc" 应当报用法错误, 而不是静默建出一个 0 字节的 foo
            if (args[0].empty() || !parseNonNegInt(args[1], size))
            {
                usage();
                continue;
            }
            fcreat(args[0], size, disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[5]) //fopen
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            fopen(args[0], disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[6])
        {
            if (args[0].empty())
            {
                usage();
                continue;
            }
            fclose(args[0], disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[7]) //fread
        {
            if (args[1].empty())
            {
                usage();
                continue;
            }
            int size = atoi(args[1].c_str());
            string str = fread(args[0], size, disk, sblk, i_table, f_table, b_mgr);
            cout << str << endl;
        }
        else if (cmd == cmd_supported[8]) //fwrite
        {
            if (args[1].empty())
            {
                usage();
                continue;
            }
            int size = min(atoi(args[2].c_str()), (int)args[1].length());
            if (args[2].empty())
                size = args[1].length();
            fwrite(args[0], args[1], size, disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[9])
        {
            int offset = atoi(args[1].c_str());
            int flag = flseek(args[0], offset, disk, sblk, i_table, f_table);
        }
        else if (cmd == cmd_supported[10])
        {
            fdelete(args[0], disk, sblk, i_table, f_table, b_mgr);
        }
        else if (cmd == cmd_supported[11])
        {
            //清空缓存队列，目的是将带有延迟写的数据块存盘
            b_mgr.clear(disk);
            //保存修改过的superblock
            writeDisk(disk, &sblk, sizeof(SuperBlock), 0);
            // 提交点: 正常退出也算一次提交 —— 清空的缓存、刚写下的 superblock,
            // 都要在进程走人之前真正落到设备上
            disk.sync();
            cout << "正在退出 misakifs ..." << endl;
            break;
        }
        else if (cmd == cmd_supported[12]) //sb
        {
            sblk.print();
        }
        else if (cmd == cmd_supported[13]) //cache
        {
            b_mgr.bq.printBrief();
        }
        else if (cmd == cmd_supported[14]) //imem
        {
            i_table.print();
        }
        else if (cmd == cmd_supported[15]) //ftab
        {
            f_table.print();
        }
        else
        {
            cout << cmd << "不是支持的命令" << endl;
        }
    }
}