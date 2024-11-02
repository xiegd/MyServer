#ifndef _PIPE_H_
#define _PIPE_H_

#include <functional>

#include "eventpoller.h"

namespace xkernel {

// 封装管道操作
class PipeWrap {
public:
    PipeWrap();
    ~PipeWrap();

public:
    int write(const void* buf, int n);
    int read(void* buf, int n);
    int readFD() const;
    int writeFD() const;
    void reOpen();

private:
    void clearFD();

private:
    int pipe_fd_[2] = {-1, -1};  // 存储管道的文件描述符
};

class Pipe {
public:
    using onRead = std::function<void(int size, const char* buf)>;

    Pipe(const onRead& cb = nullptr, const EventPoller::Ptr& poller = nullptr);
    ~Pipe();

public:
    void send(const char* buf, int size = 0);

private:
    std::shared_ptr<PipeWrap> pipe_;  // 底层管道包装对象
    EventPoller::Ptr poller_;  // 事件轮询器
};

}  // namespace xkernel

#endif // _PIPE_H_
