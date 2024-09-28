/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef UTIL_ONCETOKEN_H_
#define UTIL_ONCETOKEN_H_

#include <functional>
#include <type_traits>

namespace toolkit {

// 确保某段代码只被执行一次, 在构造时执行指定的初始化函数, 析构时可选择执行指定的清理函数
class onceToken {
   public:
    using task = std::function<void(void)>;

    template <typename FUNC>
    onceToken(const FUNC &onConstructed, task onDestructed = nullptr) {
        onConstructed();
        _onDestructed = std::move(onDestructed);
    }

    onceToken(std::nullptr_t, task onDestructed = nullptr) {
        _onDestructed = std::move(onDestructed);
    }

    ~onceToken() {
        if (_onDestructed) {
            _onDestructed();
        }
    }

   private:
    // 删除默认构造，拷贝，移动，确保onceToken只能通过参数构造
    onceToken() = delete;
    onceToken(const onceToken &) = delete;
    onceToken(onceToken &&) = delete;
    onceToken &operator=(const onceToken &) = delete;
    onceToken &operator=(onceToken &&) = delete;

   private:
    task _onDestructed;
};

} /* namespace toolkit */
#endif /* UTIL_ONCETOKEN_H_ */
