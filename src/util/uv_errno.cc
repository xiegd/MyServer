/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *  Copyright (c) 2016-2019 xiongziliang <771730766@qq.com>
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "uv_errno.h"

#include <cstdio>

#if defined(_WIN32)
#define FD_SETSIZE 1024  //修改默认64为1024路
#include <windows.h>
#include <winsock2.h>
#else
#include <cerrno>
#endif  // defined(_WIN32)

namespace xkernel {

static const char *uv__unknown_err_code(int err) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "Unknown system error %d", err);
    return buf;
}

#define UV_ERR_NAME_GEN(name, _) \
    case UV_##name:              \
        return #name;
const char *uv_err_name(int err) {
    switch (err) { UV_ERRNO_MAP(UV_ERR_NAME_GEN) }
    return uv__unknown_err_code(err);
}
#undef UV_ERR_NAME_GEN

#define UV_STRERROR_GEN(name, msg) \
    case UV_##name:                \
        return msg;
const char *uv_strerror(int err) {
    switch (err) { UV_ERRNO_MAP(UV_STRERROR_GEN) }
    return uv__unknown_err_code(err);
}
#undef UV_STRERROR_GEN

// 将POSIX错误码转换为libuv风格错误码
int uv_translate_posix_error(int err) {
    if (err <= 0) {
        return err;  // libuv错误码是负数
    }
    switch (err) {
        // 为了兼容windows/unix平台，信号EINPROGRESS
        //，EAGAIN，EWOULDBLOCK，ENOBUFS 全部统一成EAGAIN处理
        case ENOBUFS:  // 在mac系统下实测发现会有此信号发生
        case EINPROGRESS:
        case EWOULDBLOCK:
            err = EAGAIN;
            break;
        default:
            break;
    }
    return -err;
}

int get_uv_error(bool netErr) {
    // 读取当前的errno只，将其转换为libuv错误码
    return uv_translate_posix_error(errno);
}

// get_uv_errmsg() 获取系统设置的errno，
// 一个系统调用的错误码, 会保留到被另一个系统调用或库函数覆盖
const char *get_uv_errmsg(bool netErr) {
    return uv_strerror(get_uv_error(netErr));
}

}  // namespace xkernel
