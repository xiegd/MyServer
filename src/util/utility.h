#ifndef _UTILITY_H_
#define _UTILITY_H_

#include <list>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <cassert>
#include <condition_variable>
#include <typeinfo>
#include <cxxabi.h>
#include <sstream>

namespace xkernel {
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

// 确保某个函数只执行一次
class onceToken {
public:
    using task = std::function<void(void)>;

    template <typename FUNC>
    onceToken(const FUNC& onConstructed, task onDestructed = nullptr) {
        onConstructed();
        onDestructed_ = std::move(onDestructed);
    }
    onceToken(std::nullptr_t, task onDestructed = nullptr) {
        onDestructed_ = std::move(onDestructed);
    }
    ~onceToken() {
        if (onDestructed_) {
            onDestructed_();
        }
    }

private:
    onceToken() = delete;
    onceToken(const onceToken&) = delete;
    onceToken(onceToken&&) = delete;
    onceToken& operator=(const onceToken&) = delete;
    onceToken& operator=(onceToken&&) = delete;
private:
    task onDestructed_;
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
#define STATISTIC_IMPL(Type)                                    \
    template <>                                                \
    std::atomic<size_t>& ObjectCounter<Type>::getCounter() {   \
        static std::atomic<size_t> instance(0);                \
        return instance;                                       \
    }

// 用于为名为class_name的类实现线程安全的单例模式
#define INSTANCE_IMP(class_name, ...)                    \
    class_name &class_name::Instance() {                 \
        static std::shared_ptr<class_name> s_instance(   \
            new class_name(__VA_ARGS__));                \
        static class_name &s_instance_ref = *s_instance; \
        return s_instance_ref;                           \
    }

class StringUtil {
public:
    static std::string& strToLower(std::string& str);
    static std::string strToLower(std::string&& str);
    static std::string& strToUpper(std::string& str);
    static std::string strToUpper(std::string&& str);
    static std::vector<std::string> split(const std::string& s, const char* delim);
    static std::string limitString(const char* name, size_t max_size);  // 截断过长的name,从中间截断替换为...
    static bool startWith(const std::string& str, const std::string& substr);
    static bool endWith(const std::string& str, const std::string& substr);

private:
    StringUtil() = delete;
    ~StringUtil() = delete;
};

class semaphore {
public:
    explicit semaphore(size_t initial = 0);
    ~semaphore();
public:
    void post(size_t n = 1);  // 增加信号量
    void wait();  // 等待信号量

private:
    size_t count_;
    std::recursive_mutex mutex_;
    std::condition_variable_any condition_;
};

class ThreadUtil {
public:
    static void setThreadName(const char* name);
    static std::string getThreadName();
    static bool setThreadAffinity(int i);

private:
    ThreadUtil() = delete;
    ~ThreadUtil() = delete;
};


// 可以保存任意的对象, 通过set方法设置保存的对象, get方法获取保存的对象
// 实现了类型擦除
class Any {
public:
    using Ptr = std::shared_ptr<Any>;

    Any() = default;
    ~Any() = default;

    Any(const Any &that) = default;
    Any(Any &&that) {
        _type = that._type;
        _data = std::move(that._data);
    }

    Any &operator=(const Any &that) = default;
    Any &operator=(Any &&that) {
        _type = that._type;
        _data = std::move(that._data);
        return *this;
    }

    template <typename T, typename... ArgsType>
    void set(ArgsType &&...args) {
        _type = &typeid(T);
        _data.reset(new T(std::forward<ArgsType>(args)...),
                    [](void *ptr) { delete (T *)ptr; });
    }

    template <typename T>
    void set(std::shared_ptr<T> data) {
        if (data) {
            _type = &typeid(T);
            _data = std::move(data);
        } else {
            reset();
        }
    }

    template <typename T>
    T &get(bool safe = true) {
        if (!_data) {
            throw std::invalid_argument("Any is empty");
        }
        if (safe && !is<T>()) {
            throw std::invalid_argument(
                "Any::get(): " + demangle(_type->name()) + " unable cast to " +
                demangle(typeid(T).name()));
        }
        return *((T *)_data.get());
    }

    template <typename T>
    const T &get(bool safe = true) const {
        return const_cast<Any &>(*this).get<T>(safe);
    }

    template <typename T>
    bool is() const { return _type && typeid(T) == *_type; }

    operator bool() const { return _data.operator bool(); }
    bool empty() const { return !bool(); }

    void reset() {
        _type = nullptr;
        _data = nullptr;
    }

    Any &operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    std::string type_name() {
        if (!_type) {
            return "";
        }
        return demangle(_type->name());
    }

    // 将_type->name()转换为人类可读的名称
    std::string demangle(const char* mangled) {
        int status = 0;  // 失败返回NULL
        char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);  // 将mangled转换为人类可读的名称
        std::string out;
        if (status == 0 && demangled) {
            out.append(demangled);
            free(demangled);
        } else {
            out.append(mangled);
        }
        return out;
    }

private:
    const std::type_info *_type = nullptr;
    std::shared_ptr<void> _data;
};

// 用于保存一些外加属性  
class AnyStorage : public std::unordered_map<std::string, Any> {
public:
    AnyStorage() = default;
    ~AnyStorage() = default;
    using Ptr = std::shared_ptr<AnyStorage>;
};

// 继承自std::string，用于拼接字符串
#define StrPrinter ::xkernel::_StrPrinter()
class _StrPrinter : public std::string {
public:
    _StrPrinter() {}

    template <typename T>
    _StrPrinter& operator<<(T&& data) {
        stream_ << std::forward<T>(data);
        this->std::string::operator=(stream_.str());
        return *this;
    }

    std::string operator<<(std::ostream& (*f)(std::ostream&)) const {
        return *this;
    }

private:
    std::stringstream stream_;
};

// 计算数据传输速度的工具类
class BytesSpeed {
public:
    BytesSpeed();
    ~BytesSpeed();
    BytesSpeed& operator+=(size_t bytes);
    int getSpeed();

private:
    int computeSpeed();

private:
    int speed_;
    size_t bytes_;
    Ticker ticker_;
};

} // namespace xkernel

#endif


