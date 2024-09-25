#ifndef _UTILITY_H_
#define _UTILITY_H_

#include <list>
#include <atomic>

// 禁止拷贝的工具类，父类拷贝/赋值是delete则派生类无法生成默认的构造/赋值
class Noncopyable {
// 声明为protected, 不能直接创建类实例
protected:
    Noncopyable() {}
    ~Noncopyable() {}
public:
    // 禁用拷贝构造函数和赋值运算符
    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator=(const Noncopyable&) = delete;
    // 删除拷贝构造和赋值运算符后，不会再生成默认的移动构造和移动赋值运算符，但是还是显式删除
    Noncopyable(Noncopyable&&) = delete;
    Noncopyable& operator=(Noncopyable&&) = delete;
};

// 继承std::list, 并添加append和forEach方法
template <typename T>
class List : public std::list<T> {
public:
    template <typename... ARGS>
    List(ARGS&&... args) : std::list<T>(std::forward<ARGS>(args)...){}
    ~List() = default;
public:
    void append(List<T>& other) {
        if (other.empty()) {
            return ;
        }
        this->insert(this->end(), other.begin(), other.end());
        other.clear();
    }

    template <typename FUNC>
    void forEach(FUNC&& func) {
        for (auto& t : *this) {
            func(t);
        }
    }
};

// 对象计数器类
template <class C>
class ObjectCounter {
public:
    ObjectCounter() {
       ++getCounter();
    }
    ~ObjectCounter() {
        --getCounter();
    }
    static size_t count() {
        return getCounter().load();
    }
private:
    static std::atomic<size_t>& getCounter();
};

// 对象计数器特化
#define StatisticImpl(Type)                                    \
    template <>                                                \
    std::atomic<size_t>& ObjectCounter<Type>::getCounter() {   \
        static std::atomic<size_t> instance(0);                \
        return instance;                                       \
    }

#endif


