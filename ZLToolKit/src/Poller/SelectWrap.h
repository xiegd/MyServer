/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef SRC_POLLER_SELECTWRAP_H_
#define SRC_POLLER_SELECTWRAP_H_

#include "Util/util.h"

namespace toolkit {

/**
 * @class FdSet
 * @brief 文件描述符集合类
 * 
 * 该类封装了select系统调用中使用的文件描述符集合操作
 */
class FdSet {
   public:
    /**
     * @brief 构造函数
     */
    FdSet();

    /**
     * @brief 析构函数
     */
    ~FdSet();

    /**
     * @brief 清空文件描述符集合
     */
    void fdZero();

    /**
     * @brief 将文件描述符添加到集合中
     * @param fd 文件描述符
     */
    void fdSet(int fd);

    /**
     * @brief 从集合中移除文件描述符
     * @param fd 文件描述符
     */
    void fdClr(int fd);

    /**
     * @brief 检查文件描述符是否在集合中
     * @param fd 文件描述符
     * @return 是否在集合中
     */
    bool isSet(int fd);

    void *_ptr; ///< 内部指针，指向实际的文件描述符集合
};

/**
 * @brief 封装的select系统调用
 * 
 * 该函数封装了select系统调用，用于监视多个文件描述符的状态变化
 * 
 * @param cnt 文件描述符集合中最大的文件描述符加1
 * @param read 读文件描述符集合
 * @param write 写文件描述符集合
 * @param err 错误文件描述符集合
 * @param tv 超时时间
 * @return 就绪的文件描述符数量，失败时返回-1
 */
int zl_select(int cnt, FdSet *read, FdSet *write, FdSet *err,
              struct timeval *tv);

} /* namespace toolkit */
#endif /* SRC_POLLER_SELECTWRAP_H_ */
