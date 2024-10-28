#include "multiplexing_warp.h"

#include <sys/select.h>

namespace xkernel{

fdSet::fdSet() { ptr_ = new fd_set; }

fdSet::~fdSet() { delete reinterpret_cast<fd_set*>(ptr_); }

void fdSet::fdZero() { FD_ZERO(reinterpret_cast<fd_set*>(ptr_)); }

void fdSet::fdClear(int fd) { FD_CLR(fd, reinterpret_cast<fd_set*>(ptr_)); }

void fdSet::fdSet(int fd) { FD_SET(fd, reinterpret_cast<fd_set*>(ptr_)); }

bool fdSet::isSet(int fd) { return FD_ISSET(fd, reinterpret_cast<fd_set*>(ptr_)); }

int select(int count, fdSet* read, fdSet* write, fdSet* err, struct timeval* tv) {
    void* rd, *wt, *er;
    rd = read ? read->ptr_ : nullptr;
    wt = write ? write->ptr_ : nullptr;
    er = err ? err->ptr_ : nullptr;
    return ::select(count, reinterpret_cast<fd_set*>(rd), reinterpret_cast<fd_set*>(wt), 
                    reinterpret_cast<fd_set*>(er), tv);
}

}  // namespace xkernel
