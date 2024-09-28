/*
 * 
*/

#include <cassert>
#include <sys/socket.h>

#include "buffersock.h"

StatisticImpl(BufferList)

BufferSock::BufferSock(Buffer::Ptr buffer, struct sockaddr* addr, int addr_len) {
    if (addr) {

    }
    assert(buffer);
    buffer_ = std::move(buffer);
}


