/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PipeWarp_h
#define PipeWarp_h

namespace toolkit {

/**
 * @class PipeWrap
 * @brief 封装了系统管道操作的类
 *
 * 这个类提供了一个跨平台的接口来创建和使用系统管道。
 * 它封装了底层的管道文件描述符，并提供了读写操作。
 */
class PipeWrap {
public:
    /**
     * @brief 构造函数
     *
     * 创建一个新的管道。
     */
    PipeWrap();

    /**
     * @brief 析构函数
     *
     * 关闭管道并释放相关资源。
     */
    ~PipeWrap();

    /**
     * @brief 向管道写入数据
     * @param buf 要写入的数据缓冲区
     * @param n 要写入的字节数
     * @return 实际写入的字节数
     */
    int write(const void *buf, int n);

    /**
     * @brief 从管道读取数据
     * @param buf 用于存储读取数据的缓冲区
     * @param n 要读取的最大字节数
     * @return 实际读取的字节数
     */
    int read(void *buf, int n);

    /**
     * @brief 获取管道的读取端文件描述符
     * @return 读取端文件描述符
     */
    int readFD() const { return _pipe_fd[0]; }

    /**
     * @brief 获取管道的写入端文件描述符
     * @return 写入端文件描述符
     */
    int writeFD() const { return _pipe_fd[1]; }

    /**
     * @brief 重新打开管道
     *
     * 关闭现有的管道（如果有），并创建一个新的管道。
     */
    void reOpen();

private:
    /**
     * @brief 清理文件描述符
     *
     * 关闭管道的两端（如果它们是打开的）。
     */
    void clearFD();

private:
    int _pipe_fd[2] = {-1, -1};  ///< 存储管道的文件描述符，[0]为读取端，[1]为写入端
};

} /* namespace toolkit */
#endif  // !PipeWarp_h
