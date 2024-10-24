/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include "Buffer.h"

#include <cstdlib>

#include "Util/onceToken.h"

namespace toolkit {

// BufferOffset是一个模板类，不是一个具体的类型，对其使用StatisticImp时
// 无法确定为哪一个BufferOffset的实例化版本生成统计的ObjectCounter
StatisticImp(Buffer) StatisticImp(BufferRaw) StatisticImp(BufferLikeString)

    BufferRaw::Ptr BufferRaw::create() {
#if 0
    static ResourcePool<BufferRaw> packet_pool;
    static onceToken token([]() {
        packet_pool.setSize(1024);
    });
    auto ret = packet_pool.obtain2();
    ret->setSize(0);
    return ret;
#else
    return Ptr(new BufferRaw);
#endif
}

}  // namespace toolkit
