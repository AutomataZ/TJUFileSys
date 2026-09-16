#include "define.h"
#include "buffer.h"
#include <iostream>

using namespace std;

void test()
{
#ifdef DEBUG_BQUEUE
    Buffer b1(1), b2(2), b3(3), b4(4), b5(5);
    BQueue bq;

    bq.print();

    bq.push(b1);
    bq.push(b2);

    bq.print();

    bq.push(b3);
    bq.push(b4);

    bq.print();

    bq.push(b5);
    bq.update(2);

    bq.print();

    while(!bq.empty())
    {
        bq.print();
        bq.pop();
    }

    bq.print();
#endif
#ifdef DEBUG_BUFFER
    BufferMgr bmg;

    cout << "初始状态: " << endl;
    bmg.bq.print();
    cout << endl;

    Buffer* b = bmg.getBlk(0);
    cout << "申请一块与盘块0关联的缓存后: " << endl;
    cout << "取得的缓存: " << endl;
    b->print();
    cout << "队列: " << endl;
    bmg.bq.print();
    cout << endl;

    b->set(1);
    cout << "对盘块做修改后: " << endl;
    cout << "取得的缓存: " << endl;
    b->print();
    cout << "队列: " << endl;
    bmg.bq.print();
    cout << endl;

    b = bmg.getBlk(2);
    b = bmg.getBlk(3);
    b = bmg.getBlk(4);
    b = bmg.getBlk(5);
    cout << "队列: " << endl;
    bmg.bq.print();
    cout << endl;

    b = bmg.getBlk(3);
    b = bmg.getBlk(2);
    cout << "队列: " << endl;
    bmg.bq.print();
    cout << endl;

    b = bmg.getBlk(6);
    cout << "队列: " << endl;
    bmg.bq.print();
    cout << endl;
#endif
}