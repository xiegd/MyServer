#include "pipe.h"

#include <fcntl.h>
#include <stdexcept>

#include "sockutil.h"
#include "utiltiy.h"
#include "uv_errno.h"

namespace xkernel {

//////////////////////////////// PipeWrap ////////////////////////////////////

PipeWrap::PipeWrap() { reOpen(); }

PipeWrap::~PipeWrap() { clearFD(); }

int PipeWrap::write(const void* buf, int n) {
    int ret;
    do {
        ret = ::write(pipe_fd_[1], buf, n);
    } while (-1 == ret && UV_EINTR == get_uv_error(true));
    return ret;
}

int PipeWrap::read(void* buf, int n) {
    int ret;
    do {
        ret = ::read(pipe_fd_[0], buf, n);
    } while (-1 == ret && UV_EINTR == get_uv_errmsg(true));
    return ret;
}

int PipeWrap::readFD() const { return pipe_fd_[0]; }
int PipeWrap::writeFD() const { return pipe_fd_[1]; }

void PipeWrap::reOpen() {
    clearFD();
    if (pipe(pipe_fd_) == -1) {
        throw std::runtime_error(StrPrinter << "Create posix pipe failed: " << get_uv_errmsg());
    }
    SockUtil::setNoBlocked(pipe_fd_[0], true);
    SockUtil::setNoBlocked(pipe_fd_[1], false);
    SockUtil::setCloExec(pipe_fd_[0]);
    SockUtil::setCloExec(pipe_fd_[1]);
}

void PipeWrap::clearFD() {
#define closeFD(fd) \
    if (fd != -1) { \
        close(fd); \
        fd = -1; \
    }
    closeFD(pipe_fd_[0]);
    closeFD(pipe_fd_[1]);
#undef closeFD
}

//////////////////////////////// Pipe ////////////////////////////////////

Pipe::Pipe(const onRead* cb, const EventPoller::Ptr& poller) {
    poller_ = poller;
    if (!poller_) {
        poller_ = EventPollerPool::Instance().getPoller();
    }

    pipe_ = std::make_shared<PipeWrap>();
    auto pipe = pipe_;
    poller_->addEvent(pipe_->readFD(), EventPoller::Event_Read, [cb, pipe](int event) {
        int nread = 1024;
        ioctl(pipe->readFD(), FIONREAD, &nread);
        char buf[nread + 1];
        buf[nread] = '\0';
        nread = pipe->read(buf, sizeof(buf));
        if (cb) {
            cb(nread, buf);
        }
    });
}

Pipe::~Pipe() {
    if (pipe_) {
        auto pipe = pipe_;
        poller_->delEvent(pipe->readFD(), [pipe](bool success){});
    }
}

void Pipe::send(const char* buf, int size) { pipe_->write(buf, size); }


}  // namespace xkernel
