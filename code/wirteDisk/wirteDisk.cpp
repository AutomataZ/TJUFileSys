#include <fstream>
#include "./wirteDisk.h"
#include "../define.h"

using namespace std;

void readDisk(fstream& disk, void* buffer, int size, int offset)
{
    if (offset >= 0)
    {
        disk.seekp(offset, ios::beg);
    }
    disk.read((char*)buffer, size);
}

void writeDisk(fstream& disk, void* buffer, int size, int offset)
{
    if (offset >= 0)
    {
        disk.seekp(offset, ios::beg);
    }
    disk.write((char*)buffer, size);
}