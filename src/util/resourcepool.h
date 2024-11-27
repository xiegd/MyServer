#ifndef _RESOURCEPOOL_H_
#define _RESOURCEPOOL_H_

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "utility.h"

namespace xkernel {

template <typename C>
class ResourcePool_l;
template <typename C>
class ResourcePool;

// 自定义智能指针类，继承自 std::shared_ptr, 添加了资源池相关功能
template <typename C>
class shared_ptr_impl : public std::shared_ptr<C> {
public:
    shared_ptr_impl() = default;
    shared_ptr_impl(C* ptr, const std::weak_ptr<ResourcePool_l<C>>& weakPool, 
                   std::shared_ptr<std::atomic_bool> quit, 
                   const std::function<void(C*)>& on_recycle) 
        : std::shared_ptr<C>(ptr, [weakPool, quit, on_recycle](C* ptr) {
            if (on_recycle) {
                on_recycle(ptr);
            }
            auto strongPool = weakPool.lock();
            if (strongPool && !(*quit)) {
                strongPool->recycle(ptr);
            } else {
                delete ptr;
            }
        }), quit_(std::move(quit)){}

    void quit(bool flag = true) {
        if (quit_) {
            *quit_ = flag;
        }
    }
private:
    std::shared_ptr<std::atomic_bool> quit_;  // 控制是否退出资源池循环的标志
};

template <typename C>
class ResourcePool_l : public std::enable_shared_from_this<ResourcePool_l<C>> {
public:
    friend class shared_ptr_impl<C>;
    friend class ResourcePool<C>;
    using ValuePtr = shared_ptr_impl<C>;

    ResourcePool_l() { alloc_ = []() -> C* { return new C(); }; }
    // 使用自定义参数创建对象
    template <typename... ArgTypes>
    ResourcePool_l(ArgTypes&&... args) {
        alloc_ = [args...]() -> C* { return new C(args...); };
    }

    ~ResourcePool_l() {
        for (auto ptr : objs_) {
            delete ptr;
        }
    }

    void setSize(size_t size) {
        pool_size_ = size;
        objs_.reserve(size);
    }

    ValuePtr obtain(const std::function<void(C*)>& on_recycle = nullptr) {
        return ValuePtr(getPtr(), weak_self_, std::make_shared<std::atomic_bool>(false), on_recycle);
    }

    std::shared_ptr<C> obtain2() {
        auto weak_self = weak_self_;
        return std::shared_ptr<C>(getPtr(), [weak_self](C* ptr) {
            auto strongPool = weak_self.lock();
            if (strongPool) {
                strongPool->recycle(ptr);
            } else {
                delete ptr;
            }
        });
    }

private:
    void recycle(C* obj) {
        auto is_busy = busy_.test_and_set();
        if (!is_busy) {
            if (objs_.size() >= pool_size_) {
                delete obj;
            } else {
                objs_.emplace_back(obj);
            }
            busy_.clear();
        } else {
            delete obj;
        }
    }

    C* getPtr() {
        C* ptr;
        auto is_busy = busy_.test_and_set();
        if (!is_busy) {
            if (objs_.size() == 0) {
                ptr = alloc_();
            } else {
                ptr = objs_.back();
                objs_.pop_back();
            }
            busy_.clear();
        } else {
            ptr = alloc_();
        }
        return ptr;
    }

    void setup() { weak_self_ = this->shared_from_this(); }


private:
    size_t pool_size_ = 8;
    std::vector<C*> objs_;  // 存储可用对象的队列
    std::function<C*(void)> alloc_;  // 分配器函数
    std::atomic_flag busy_{false};  // 标记池是否正在被访问
    std::weak_ptr<ResourcePool_l<C>> weak_self_;  // 弱引用自身
};

template <typename C>
class ResourcePool {
public:
    using ValuePtr = shared_ptr_impl<C>;
    ResourcePool() {
        pool_.reset(new ResourcePool_l<C>()); 
        pool_->setup();
    }

    template <typename... ArgTypes>
    ResourcePool(ArgTypes&&... args) {
        pool_ = std::make_shared<ResourcePool_l<C>>(std::forward<ArgTypes>(args)...);
        pool_->setup();
    }

    void setSize(size_t size) { pool_->setSize(size); }

    ValuePtr obtain(const std::function<void(C*)>& on_recycle = nullptr) {
        return pool_->obtain(on_recycle);
    }

    std::shared_ptr<C> obtain2() { return pool_->obtain2(); }

private:
    std::shared_ptr<ResourcePool_l<C>> pool_;
};

}  // namespace xkernel
#endif