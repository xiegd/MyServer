#include "buffer.h"
#include "utility.h"

namespace xkernel {
    // 统计缓冲区对象个数
    STATISTIC_IMPL(Buffer) 
    STATISTIC_IMPL(BufferString)
    STATISTIC_IMPL(BufferRaw) 
    STATISTIC_IMPL(BufferLikeString)
}