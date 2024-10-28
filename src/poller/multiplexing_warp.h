#ifndef _MULTIPLEXING_WARP_H_
#define _MULTIPLEXING_WARP_H_

#include "utility.h"

namespace xkernel {

// 封装select系统调用
class fdSet{
public:
    fdSet();
    ~fdSet();

public:
    void fdZero();  // 清空文件描述符集合
    void fdSet(int fd);  // 添加文件描述符到集合
    void fdClear(int fd);  // 从集合中移除文件描述符
    bool isSet(int fd);  // 检查文件描述符是否在集合中
    void* ptr_;  // 指向实际的文件描述符集合

};

int select(int count, fdSet* read, fdSet* write, fdSet* err, struct timeval* tv);

}  // namespace xkernel

#endif // _MULTIPLEXING_WARP_H_
