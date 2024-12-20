﻿/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef UTIL_UTIL_H_
#define UTIL_UTIL_H_

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "function_traits.h"
#if defined(_WIN32)
#undef FD_SETSIZE
//修改默认64为1024路  [AUTO-TRANSLATED:90567e14]
// Modify the default 64 to 1024 paths
#define FD_SETSIZE 1024
#include <winsock2.h>
#pragma comment(lib, "WS2_32")
#else
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#endif  // defined(_WIN32)

#if defined(__APPLE__)
#include "TargetConditionals.h"
#if TARGET_IPHONE_SIMULATOR
#define OS_IPHONE
#elif TARGET_OS_IPHONE
#define OS_IPHONE
#endif
#endif  //__APPLE__

// 用于为名为class_name的类实现线程安全的单例模式
// 创建了一个静态的std::shared_ptr<class_name>类型的局部变量s_instance
// 这样涉及的类必须有public的构造函数和析构函数
// 否则如果设置为了private，shared_ptr无法delete
// 另一种做法是使用unique_ptr，并且自定义deleter, 同时构造函数和析构函数设置为public
// 但是就无法贡献所有权了
#define INSTANCE_IMP(class_name, ...)                    \
    class_name &class_name::Instance() {                 \
        static std::shared_ptr<class_name> s_instance(   \
            new class_name(__VA_ARGS__));                \
        static class_name &s_instance_ref = *s_instance; \
        return s_instance_ref;                           \
    }

namespace toolkit {

#define StrPrinter ::toolkit::_StrPrinter()
// 继承自std::string，用于拼接字符串
class _StrPrinter : public std::string {
   public:
    _StrPrinter() {}

    template <typename T>
    _StrPrinter &operator<<(T &&data) {
        _stream << std::forward<T>(data);
        this->std::string::operator=(_stream.str());
        return *this;
    }
    // 允许使用std::endl等流操作符使用<<
    // 参数是一个函数指针，返回值为std::ostream&, 指针名f，后面是参数列表, 为了和返回指针的函数区分
    std::string operator<<(std::ostream &(*f)(std::ostream &)) const {
        return *this;
    }

   private:
    std::stringstream _stream;
};

//禁止拷贝基类  [AUTO-TRANSLATED:a4ca4dcb]
// Prohibit copying of base classes
class noncopyable {
   protected:
    noncopyable() {}
    ~noncopyable() {}

   private:
    //禁止拷贝  [AUTO-TRANSLATED:e8af72e3]
    // Prohibit copying
    noncopyable(const noncopyable &that) = delete;
    noncopyable(noncopyable &&that) = delete;
    noncopyable &operator=(const noncopyable &that) = delete;
    noncopyable &operator=(noncopyable &&that) = delete;
};

#ifndef CLASS_FUNC_TRAITS
#define CLASS_FUNC_TRAITS(func_name)                                          \
    template <typename T, typename... ARGS>                                   \
    constexpr bool Has_##func_name(decltype(&T::on##func_name) /*unused*/) {  \
        using RET = typename function_traits<                                 \
            decltype(&T::on##func_name)>::return_type;                        \
        using FuncType = RET (T::*)(ARGS...);                                 \
        return std::is_same<decltype(&T::on##func_name), FuncType>::value;    \
    }                                                                         \
                                                                              \
    template <class T, typename... ARGS>                                      \
    constexpr bool Has_##func_name(...) {                                     \
        return false;                                                         \
    }                                                                         \
                                                                              \
    template <typename T, typename... ARGS>                                   \
    static void InvokeFunc_##func_name(                                       \
        typename std::enable_if<!Has_##func_name<T, ARGS...>(nullptr),        \
                                T>::type &obj,                                \
        ARGS... args) {}                                                      \
                                                                              \
    template <typename T, typename... ARGS>                                   \
    static typename function_traits<decltype(&T::on##func_name)>::return_type \
        InvokeFunc_##func_name(                                               \
            typename std::enable_if<Has_##func_name<T, ARGS...>(nullptr),     \
                                    T>::type &obj,                            \
            ARGS... args) {                                                   \
        return obj.on##func_name(std::forward<ARGS>(args)...);                \
    }
#endif  // CLASS_FUNC_TRAITS

#ifndef CLASS_FUNC_INVOKE
#define CLASS_FUNC_INVOKE(T, obj, func_name, ...) \
    InvokeFunc_##func_name<T>(obj, ##__VA_ARGS__)
#endif  // CLASS_FUNC_INVOKE

CLASS_FUNC_TRAITS(Destory)
CLASS_FUNC_TRAITS(Create)

/**
 * 对象安全的构建和析构,构建后执行onCreate函数,析构前执行onDestory函数
 * 在函数onCreate和onDestory中可以执行构造或析构中不能调用的方法，比如说shared_from_this或者虚函数
 * @warning onDestory函数确保参数个数为0；否则会被忽略调用
 * Object-safe construction and destruction, execute the onCreate function after
 construction, and execute the onDestroy function before destruction
 * Methods that cannot be called during construction or destruction, such as
 shared_from_this or virtual functions, can be executed in the onCreate and
 onDestroy functions
 * @warning The onDestroy function must have 0 parameters; otherwise, it will be
 ignored

 * [AUTO-TRANSLATED:54ef34ac]
 */
class Creator {
   public:
    /**
     * 创建对象，用空参数执行onCreate和onDestory函数
     * @param args 对象构造函数参数列表
     * @return args对象的智能指针
     * Create an object, execute onCreate and onDestroy functions with empty
     parameters
     * @param args List of parameters for the object's constructor
     * @return Smart pointer to the args object

     * [AUTO-TRANSLATED:c6c90c2b]
     */
    template <typename C, typename... ArgsType>
    static std::shared_ptr<C> create(ArgsType &&...args) {
        std::shared_ptr<C> ret(new C(std::forward<ArgsType>(args)...),
                               [](C *ptr) {
                                   try {
                                       CLASS_FUNC_INVOKE(C, *ptr, Destory);
                                   } catch (std::exception &ex) {
                                       onDestoryException(typeid(C), ex);
                                   }
                                   delete ptr;
                               });
        CLASS_FUNC_INVOKE(C, *ret, Create);
        return ret;
    }

    /**
     * 创建对象，用指定参数执行onCreate函数
     * @param args 对象onCreate函数参数列表
     * @warning
     args参数类型和个数必须与onCreate函数类型匹配(不可忽略默认参数)，否则会由于模板匹配失败导致忽略调用
     * @return args对象的智能指针
     * Create an object, execute the onCreate function with specified parameters
     * @param args List of parameters for the object's onCreate function
     * @warning The type and number of args parameters must match the type of
     the onCreate function (default parameters cannot be ignored), otherwise it
     will be ignored due to template matching failure
     * @return Smart pointer to the args object

     * [AUTO-TRANSLATED:bd672150]
     */
    template <typename C, typename... ArgsType>
    static std::shared_ptr<C> create2(ArgsType &&...args) {
        std::shared_ptr<C> ret(new C(), [](C *ptr) {
            try {
                CLASS_FUNC_INVOKE(C, *ptr, Destory);
            } catch (std::exception &ex) {
                onDestoryException(typeid(C), ex);
            }
            delete ptr;
        });
        CLASS_FUNC_INVOKE(C, *ret, Create, std::forward<ArgsType>(args)...);
        return ret;
    }

   private:
    static void onDestoryException(const std::type_info &info,
                                   const std::exception &ex);

   private:
    Creator() = default;
    ~Creator() = default;
};

/* 对象统计计数的工具类
*  用于追踪特定类型对象的创建和销毁，从而可以统计特定类型的对象的个数
*  每实例化一个ObjectStatistic对象，就将getCounter()的返回值instance进行++
*  每销毁一个ObjectStatistic对象，就将getCounter()的返回值instance进行--
*/
template <class C>
class ObjectStatistic {
   public:
    ObjectStatistic() { ++getCounter(); }  // 对getCounter()的返回值instance进行++

    ~ObjectStatistic() { --getCounter(); }  // 对getCounter()的返回值instance进行--

    static size_t count() { return getCounter().load(); }

   private:
    static std::atomic<size_t> &getCounter();
};

// 接受一个Type类型对象，为特定类型生成getCounter方法的特化实现, 生成一个std::atomic<size_t>类型的静态成员变量instance
// 第一次调用时初始化instance，
// 必须在定义ObjectStatistic模板类之后, 在对应的.cc文件中使用这个宏，这样
// 在编译时，编译器会根据模板特化生成对应的getCounter方法
// 如果直接实现，getCounter方法, 则这里的是函数内的静态变量，每个包含头文件的编译单元都会生成一个instance
// .cc文件inlcude时，只是把代码插入到前面，每个.cc文件独立编译，然后链接，在执行代码时，
// 来自不同.cc文件的同一类型的ObjectStatistic对象在执行getCounter方法时，会单独初始化static 的instance
// 导致计数错误

// 所以getCounter方法不能写在.h文件中，但是模板类方法不能分离写在.cc文件中，因为实例化模板在编译时进行
// 所以把getCounter方法写在对应使用了实例化的ObjectStatistic类的.cc文件中，避免了上述问题
#define StatisticImp(Type)                                     \
    // 模板参数为空，表示的时模板特化，在声明`ObjectStatistic模板类时已经特化了
    template <>                                                \
    std::atomic<size_t> &ObjectStatistic<Type>::getCounter() { \
        // 第一次调用时初始化局部静态变量instance，后续会直接返回
        static std::atomic<size_t> instance(0);                \
        return instance;                                       \
    }

class AssertFailedException : public std::runtime_error {
   public:
    template <typename... T>
    AssertFailedException(T &&...args)
        : std::runtime_error(std::forward<T>(args)...) {}
};

std::string makeRandStr(int sz, bool printable = true);
std::string hexdump(const void *buf, size_t len);
std::string hexmem(const void *buf, size_t len);
std::string exePath(bool isExe = true);
std::string exeDir(bool isExe = true);
std::string exeName(bool isExe = true);

std::vector<std::string> split(const std::string &s, const char *delim);
//去除前后的空格、回车符、制表符...  [AUTO-TRANSLATED:7c50cbc8]
// Remove leading and trailing spaces, line breaks, tabs...
std::string &trim(std::string &s, const std::string &chars = " \r\n\t");
std::string trim(std::string &&s, const std::string &chars = " \r\n\t");
// string转小写  [AUTO-TRANSLATED:bf92618b]
// Convert string to lowercase
std::string &strToLower(std::string &str);
std::string strToLower(std::string &&str);
// string转大写  [AUTO-TRANSLATED:0197b884]
// Convert string to uppercase
std::string &strToUpper(std::string &str);
std::string strToUpper(std::string &&str);
//替换子字符串  [AUTO-TRANSLATED:cbacb116]
// Replace substring
void replace(std::string &str, const std::string &old_str,
             const std::string &new_str, std::string::size_type b_pos = 0);
//判断是否为ip  [AUTO-TRANSLATED:288e7a54]
// Determine if it's an IP
bool isIP(const char *str);
//字符串是否以xx开头  [AUTO-TRANSLATED:585cf826]
// Check if a string starts with xx
bool start_with(const std::string &str, const std::string &substr);
//字符串是否以xx结尾  [AUTO-TRANSLATED:50cc80d7]
// Check if a string ends with xx
bool end_with(const std::string &str, const std::string &substr);
//拼接格式字符串  [AUTO-TRANSLATED:2f902ef7]
// Concatenate format string
template <typename... Args>
std::string str_format(const std::string &format, Args... args) {
    // Calculate the buffer size
    auto size_buf = snprintf(nullptr, 0, format.c_str(), args...) + 1;
    // Allocate the buffer
#if __cplusplus >= 201703L
    // C++17
    auto buf = std::make_unique<char[]>(size_buf);
#else
    // C++11
    std::unique_ptr<char[]> buf(new (std::nothrow) char[size_buf]);
#endif
    // Check if the allocation is successful
    if (buf == nullptr) {
        return {};
    }
    // Fill the buffer with formatted string
    auto result = snprintf(buf.get(), size_buf, format.c_str(), args...);
    // Return the formatted string
    return std::string(buf.get(), buf.get() + result);
}

#ifndef bzero
#define bzero(ptr, size) memset((ptr), 0, (size));
#endif  // bzero

#if defined(ANDROID)
template <typename T>
std::string to_string(T value) {
    std::ostringstream os;
    os << std::forward<T>(value);
    return os.str();
}
#endif  // ANDROID

#if defined(_WIN32)
int gettimeofday(struct timeval *tp, void *tzp);
void usleep(int micro_seconds);
void sleep(int second);
int vasprintf(char **strp, const char *fmt, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
const char *strcasestr(const char *big, const char *little);

#if !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

#if !defined(strncasecmp)
#define strncasecmp _strnicmp
#endif

#ifndef ssize_t
#ifdef _WIN64
#define ssize_t int64_t
#else
#define ssize_t int32_t
#endif
#endif
#endif  // WIN32

/**
 * 获取时间差, 返回值单位为秒
 */
long getGMTOff();

/**
 * 获取1970年至今的毫秒数
 * @param system_time
 是否为系统时间(系统时间可以回退),否则为程序启动时间(不可回退)
 */
uint64_t getCurrentMillisecond(bool system_time = false);

/**
 * 获取1970年至今的微秒数
 * @param system_time
 是否为系统时间(系统时间可以回退),否则为程序启动时间(不可回退)
 */
uint64_t getCurrentMicrosecond(bool system_time = false);

/**
 * 获取时间字符串
 * @param fmt 时间格式，譬如%Y-%m-%d %H:%M:%S
 * @return 时间字符串
 */
std::string getTimeStr(const char *fmt, time_t time = 0);

/**
 * 根据unix时间戳获取本地时间
 * @param sec unix时间戳
 * @return tm结构体
 */
struct tm getLocalTime(time_t sec);

/**
 * 设置线程名
 */
void setThreadName(const char *name);

/**
 * 获取线程名
 */
std::string getThreadName();

/**
 * 设置当前线程cpu亲和性
 * @param i cpu索引，如果为-1，那么取消cpu亲和性
 * @return 是否成功，目前只支持linux
 */
bool setThreadAffinity(int i);

/**
 * 根据typeid(class).name()获取类名
 */
std::string demangle(const char *mangled);

/**
 * 获取环境变量内容，以'$'开头
 */
std::string getEnv(const std::string &key);

// 可以保存任意的对象  
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
        // 使用std::forward将参数完美转发给T的构造函数
        // ...: 这里涉及到两个展开过程，ArgsType和args，
        // 第一个展开过程是ArgsType，将多个参数展开为单个参数
        // 第二个展开过程是args，将多个参数展开为单个参数
        // 从而对每一个参数都使用std::forward进行完美转发
        // std::forward<ArgsType...>(args...): 时对整个参数包调用std::forward
        // 用于参数包时在最后加上...会对参数包中的每个参数进行完美转发
        // 所以可以理解为forward针对参数包进行了特殊设计，在整个std::forward
        // 表达式最后添加上'...'，可以理解为对参数包中的每一个参数进行std::forward
        // 实际用时,T是FUNC对应的std::function类型,
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
        // 如果safe为true(安全触发事件)，并且类型不匹配，则抛出异常
        // 由于循环中没有相应捕获std::invalid_argument异常的catch块
        // 所以只会终止当前的迭代过程，继续执行下一次循环
        if (safe && !is<T>()) {
            throw std::invalid_argument(
                "Any::get(): " + demangle(_type->name()) + " unable cast to " +
                demangle(typeid(T).name()));
        }
        // _data是一个存放了void指针的shared_ptr, 
        // 先使用get()方法获取到其管理的指针，然后强制转换为*，
        // 然后解引用
        return *((T *)_data.get());
    }

    template <typename T>
    const T &get(bool safe = true) const {
        return const_cast<Any &>(*this).get<T>(safe);
    }

    template <typename T>
    bool is() const {
        return _type && typeid(T) == *_type;
    }

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

    std::string type_name() const {
        if (!_type) {
            return "";
        }
        return demangle(_type->name());
    }

   private:
    // std::type_info: 提供了运行时类型信息(RTTI)的接口， 
    // 主要在运行时获取和比较类型信息, 使用typeid运算符获取类型信息
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

}  // namespace toolkit

#ifdef __cplusplus
extern "C" {
#endif
extern void Assert_Throw(int failed, const char *exp, const char *func,
                         const char *file, int line, const char *str);
#ifdef __cplusplus
}
#endif

#endif /* UTIL_UTIL_H_ */
