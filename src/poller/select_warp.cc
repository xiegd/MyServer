#include "select_warp.h"

#include <sys/select.h>

namespace xkernel{

FdSet::FdSet() { ptr_ = new fd_set; }

FdSet::~FdSet() { delete reinterpret_cast<fd_set*>(ptr_); }

void FdSet::fdZero() { FD_ZERO(reinterpret_cast<fd_set*>(ptr_)); }

void FdSet::fdClear(int fd) { FD_CLR(fd, reinterpret_cast<fd_set*>(ptr_)); }

void FdSet::fdSet(int fd) { FD_SET(fd, reinterpret_cast<fd_set*>(ptr_)); }

bool FdSet::isSet(int fd) { return FD_ISSET(fd, reinterpret_cast<fd_set*>(ptr_)); }

int select(int count, FdSet* read, FdSet* write, FdSet* err, struct timeval* tv) {
    void* rd, *wt, *er;
    rd = read ? read->ptr_ : nullptr;
    wt = write ? write->ptr_ : nullptr;
    er = err ? err->ptr_ : nullptr;
    return ::select(count, reinterpret_cast<fd_set*>(rd), reinterpret_cast<fd_set*>(wt), 
                    reinterpret_cast<fd_set*>(er), tv);
}

}  // namespace xkernel
