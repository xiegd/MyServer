/**
 * @file Buffer.h
 * @brief 定义了一系列缓冲区相关的类，用于高效地管理和操作内存数据
 *
 * 本文件定义了以下主要类：
 * - Buffer: 抽象基类，定义了缓冲区的基本接口
 * - BufferOffset: 基于偏移量的缓冲区实现
 * - BufferRaw: 基于原始指针的缓冲区实现
 * - BufferLikeString: 类似std::string的缓冲区实现
 *
 * 这些类提供了灵活的内存管理方式，适用于网络编程、数据处理等场景。
 */

#ifndef ZLTOOLKIT_BUFFER_H
#define ZLTOOLKIT_BUFFER_H

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
// 资源池，作为BufferRaw的资源池
#include "Util/ResourcePool.h"
#include "Util/util.h"

namespace toolkit {

/**
 * @brief 用于检查类型是否为指针的模板结构体
 */
template <typename T>
struct is_pointer : public std::false_type {};

template <typename T>
struct is_pointer<std::shared_ptr<T>> : public std::true_type {};

template <typename T>
struct is_pointer<std::shared_ptr<T const>> : public std::true_type {};

template <typename T>
struct is_pointer<T *> : public std::true_type {};

template <typename T>
struct is_pointer<const T *> : public std::true_type {};

/**
 * @class Buffer
 * @brief 缓存基类，定义了缓冲区的基本接口
 */
class Buffer : public noncopyable {
public:
    using Ptr = std::shared_ptr<Buffer>;

    Buffer() = default;
    virtual ~Buffer() = default;

    /**
     * @brief 获取数据指针
     * @return 指向数据的字符指针
     */
    virtual char *data() const = 0;

    /**
     * @brief 获取数据长度
     * @return 数据长度
     */
    virtual size_t size() const = 0;

    /**
     * @brief 将缓冲区数据转换为字符串
     * @return 包含缓冲区数据的字符串
     * 使用char*和对应的长度构造std::string
     */
    virtual std::string toString() const { return std::string(data(), size()); }

    /**
     * @brief 获取缓冲区容量
     * @return 缓冲区容量
     */
    virtual size_t getCapacity() const { return size(); }

private:
    ObjectStatistic<Buffer> _statistic; // 对象个数统计
};

/**
 * @class BufferOffset
 * @brief 基于偏移量的缓冲区实现
 */
template <typename C>
class BufferOffset : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferOffset>;

    /**
     * @brief 构造函数
     * @param data 数据源
     * @param offset 偏移量
     * @param len 数据长度
     */
    BufferOffset(C data, size_t offset = 0, size_t len = 0)
        : _data(std::move(data)) {
        setup(offset, len);
    }

    ~BufferOffset() override = default;

    /**
     * @brief 获取数据指针
     * @return 指向数据的字符指针
     */
    char *data() const override {
        return const_cast<char *>(getPointer<C>(_data)->data()) + _offset;
    }

    /**
     * @brief 获取数据长度
     * @return 数据长度
     */
    size_t size() const override { return _size; }

    /**
     * @brief 将缓冲区数据转换为字符串
     * @return 包含缓冲区数据的字符串
     */
    std::string toString() const override {
        return std::string(data(), size());
    }

private:
    /**
     * @brief 设置偏移量和数据长度
     * @param offset 偏移量
     * @param size 数据长度
     */
    void setup(size_t offset = 0, size_t size = 0) {
        auto max_size = getPointer<C>(_data)->size();
        assert(offset + size <= max_size);
        if (!size) {
            size = max_size - offset;
        }
        _size = size;
        _offset = offset;
    }

    /**
     * @brief 获取指针
     * @param data 数据源
     * @return 指向数据的指针
     * 静态模板函数, 这里的typename用于指定类型，告诉后面的名称是一个类型
     * std::enable_if<T> 用于在编译时进行条件判断，根据条件启用/禁用模板函数
     */
    template <typename T>
    // 如果::toolkit::is_pointer<T>::value为true，则std::enable_if模板类定义一个类型别名 const T&;
    // type是在std::enable_if模板类中定义的类型别名，表示的是前面模板中的第二个参数T
    // ::value直接获取std::true_type或std::false_type模板类中定义的静态常量，是一个bool值，表示是否启用模板函数
    // is_pointer<T>根据不同的T类型，继承std::true_type或std::false_type
    static typename std::enable_if<::toolkit::is_pointer<T>::value, const T &>::type
    getPointer(const T &data) {
        return data;
    }

    /**
     * @brief 获取指针
     * @param data 数据源
     * @return 指向数据的指针
     */
    template <typename T>
    static typename std::enable_if<!::toolkit::is_pointer<T>::value, const T *>::type
    getPointer(const T &data) {
        return &data;
    }

private:
    C _data;
    size_t _size;  // capaticty = size
    size_t _offset;
};

using BufferString = BufferOffset<std::string>;

/**
 * @class BufferRaw
 * @brief 基于原始指针的缓冲区实现
 */
class BufferRaw : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferRaw>;

    /**
     * @brief 创建BufferRaw对象
     * @return 指向BufferRaw对象的智能指针
     */
    static Ptr create();

    ~BufferRaw() override {
        if (_data) {
            delete[] _data;
        }
    }

    /**
     * @brief 获取数据指针
     * @return 指向数据的字符指针
     */
    char *data() const override { return _data; }

    /**
     * @brief 获取数据长度
     * @return 数据长度
     */
    size_t size() const override { return _size; }

    /**
     * @brief 设置缓冲区容量
     * @param capacity 容量大小
     */
    void setCapacity(size_t capacity) {
        if (_data) {
            // 使用do while循环，创建了一个局部作用域，可以使用break退出，而不需要使用if else结构
            // 有点幽默，分了三种情况，结果根本没处理
            do {
                if (capacity > _capacity) {
                    // 请求的内存大于当前内存，那么重新分配
                    break;
                }

                if (_capacity < 2 * 1024) {
                    // 2K以下，不重复开辟内存，直接复用
                    return;
                }

                if (2 * capacity > _capacity) {
                    // 如果请求的内存大于当前内存的一半，那么也复用
                    return;
                }
            } while (false);

            delete[] _data;
        }
        _data = new char[capacity];  // 如果为nullptr，那么开辟新内存
        _capacity = capacity;
    }

    /**
     * @brief 设置有效数据大小
     * @param size 数据大小
     */
    virtual void setSize(size_t size) {
        if (size > _capacity) {
            throw std::invalid_argument("Buffer::setSize out of range");
        }
        _size = size;
    }

    /**
     * @brief 赋值数据
     * @param data 数据指针
     * @param size 数据长度
     * 根据size设置缓冲区中的数据
     */
    void assign(const char *data, size_t size = 0) {
        if (size <= 0) {
            size = strlen(data);
        }
        setCapacity(size + 1);
        memcpy(_data, data, size);  // 从data指向的内存中复制size个字节的数据到_data指向的内存中(不关心数据内容)
        _data[size] = '\0';  // 添加末尾空字符
        setSize(size);
    }

    /**
     * @brief 获取缓冲区容量
     * @return 缓冲区容量
     */
    size_t getCapacity() const override { return _capacity; }

protected:
    friend class ResourcePool_l<BufferRaw>;

    /**
     * @brief 构造函数
     * @param capacity 初始容量
     */
    BufferRaw(size_t capacity = 0) {
        if (capacity) {
            setCapacity(capacity);
        }
    }

    /**
     * @brief 构造函数
     * @param data 数据指针
     * @param size 数据长度
     */
    BufferRaw(const char *data, size_t size = 0) { assign(data, size); }

private:
    size_t _size = 0;
    size_t _capacity = 0;
    char *_data = nullptr;
    ObjectStatistic<BufferRaw> _statistic; // 对象个数统计
};

/**
 * @class BufferLikeString
 * @brief 类似std::string的缓冲区实现
 */
class BufferLikeString : public Buffer {
public:
    ~BufferLikeString() override = default;

    /**
     * @brief 默认构造函数
     */
    BufferLikeString() {
        _erase_head = 0;
        _erase_tail = 0;
    }

    /**
     * @brief 构造函数
     * @param str 初始字符串
     */
    BufferLikeString(std::string str) {
        _str = std::move(str);
        _erase_head = 0;
        _erase_tail = 0;
    }

    /**
     * @brief 赋值运算符
     * @param str 字符串
     * @return 引用
     */
    BufferLikeString &operator=(std::string str) {
        _str = std::move(str);
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    /**
     * @brief 构造函数
     * @param str 初始字符串
     */
    BufferLikeString(const char *str) {
        _str = str;  // 利用string的赋值运算符重载
        _erase_head = 0;
        _erase_tail = 0;
    }

    /**
     * @brief 赋值运算符
     * @param str 字符串
     * @return 引用
     */
    BufferLikeString &operator=(const char *str) {
        _str = str;
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    /**
     * @brief 移动构造函数
     * @param that 源对象
     */
    BufferLikeString(BufferLikeString &&that) {
        _str = std::move(that._str);
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        that._erase_head = 0;
        that._erase_tail = 0;
    }

    /**
     * @brief 移动赋值运算符
     * @param that 源对象
     * @return 引用
     */
    BufferLikeString &operator=(BufferLikeString &&that) {
        _str = std::move(that._str);
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        that._erase_head = 0;
        that._erase_tail = 0;
        return *this;
    }

    /**
     * @brief 拷贝构造函数
     * @param that 源对象
     */
    BufferLikeString(const BufferLikeString &that) {
        _str = that._str;
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
    }

    /**
     * @brief 拷贝赋值运算符
     * @param that 源对象
     * @return 引用
     */
    BufferLikeString &operator=(const BufferLikeString &that) {
        _str = that._str;
        _erase_head = that._erase_head;
        _erase_tail = that._erase_tail;
        return *this;
    }

    /**
     * @brief 获取数据指针
     * @return 指向数据的字符指针
     * string的data()方法: 返回string中用于存储字符串的const char*
     */
    char *data() const override { return (char *)_str.data() + _erase_head; }

    /**
     * @brief 获取数据长度
     * @return 数据长度
     */
    size_t size() const override {
        return _str.size() - _erase_tail - _erase_head;
    }

    /**
     * @brief 移除数据
     * @param pos 起始位置
     * @param n 移除长度
     * @return 引用
     */
    BufferLikeString &erase(size_t pos = 0, size_t n = std::string::npos) {
        if (pos == 0) {
            // 移除前面的数据
            if (n != std::string::npos) {
                // 移除部分
                if (n > size()) {
                    // 移除太多数据了
                    throw std::out_of_range(
                        "BufferLikeString::erase out_of_range in head");
                }
                // 设置起始偏移量
                _erase_head += n;
                data()[size()] = '\0';
                return *this;
            }
            // 移除全部数据
            _erase_head = 0;
            _erase_tail = _str.size();
            data()[0] = '\0';
            return *this;
        }

        if (n == std::string::npos || pos + n >= size()) {
            // 移除末尾所有数据
            if (pos >= size()) {
                // 移除多余数据
                throw std::out_of_range(
                    "BufferLikeString::erase out_of_range in tail");
            }
            _erase_tail += size() - pos;
            data()[size()] = '\0';
            return *this;
        }

        // 移除中间的
        if (pos + n > size()) {
            // 超过长度限制
            throw std::out_of_range(
                "BufferLikeString::erase out_of_range in middle");
        }
        _str.erase(_erase_head + pos, n);
        return *this;
    }

    /**
     * @brief 追加数据
     * @param str 字符串
     * @return 引用
     */
    BufferLikeString &append(const BufferLikeString &str) {
        return append(str.data(), str.size());
    }

    /**
     * @brief 追加数据
     * @param str 字符串
     * @return 引用
     */
    BufferLikeString &append(const std::string &str) {
        return append(str.data(), str.size());
    }

    /**
     * @brief 追加数据
     * @param data 字符串
     * @return 引用
     */
    BufferLikeString &append(const char *data) {
        return append(data, strlen(data));
    }

    /**
     * @brief 追加数据
     * @param data 数据指针
     * @param len 数据长度
     * @return 引用
     */
    BufferLikeString &append(const char *data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        // 如果前半部分已经移除了超过string容量一半的字符，则将string中的有效数据移动到开头
        if (_erase_head > _str.capacity() / 2) {
            moveData();
        }
        // 如果后半部分已经满了，则append
        if (_erase_tail == 0) {
            _str.append(data, len);
            return *this;
        }
        _str.insert(_erase_head + size(), data, len);
        return *this;
    }

    /**
     * @brief 追加单个字符
     * @param c 字符
     */
    void push_back(char c) {
        if (_erase_tail == 0) {
            _str.push_back(c);
            return;
        }
        // 将erase_tail_部分的首元素进行替换，有效部分++
        data()[size()] = c;
        --_erase_tail;
        data()[size()] = '\0';  // 确保字符串与C风格字符串兼容
    }

    /**
     * @brief 插入数据
     * @param pos 插入位置
     * @param s 数据指针
     * @param n 数据长度
     * @return 引用
     * 这里为什么没有考虑更新erase_head_和erase_tail_?
     */
    BufferLikeString &insert(size_t pos, const char *s, size_t n) {
        // 不需要更新_erase_tail_因为, _erase_tail_指的是_str最后几个无效的内容
        // 和_str的长度无关
        _str.insert(_erase_head + pos, s, n);
        return *this;
    }

    /**
     * @brief 赋值数据
     * @param data 数据指针
     * @return 引用
     */
    BufferLikeString &assign(const char *data) {
        return assign(data, strlen(data));
    }

    /**
     * @brief 赋值数据
     * @param data 数据指针
     * @param len 数据长度
     * @return 引用
     */
    BufferLikeString &assign(const char *data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        // 赋值的数据是_str的一部分，需要更新_erase_head和_erase_tail
        if (data >= _str.data() && data < _str.data() + _str.size()) {
            _erase_head = data - _str.data();
            if (data + len > _str.data() + _str.size()) {
                throw std::out_of_range(
                    "BufferLikeString::assign out_of_range");
            }
            _erase_tail = _str.data() + _str.size() - (data + len);
            return *this;
        }
        // 赋值的数据不是_str的一部分, 则重新赋值_str
        _str.assign(data, len);
        _erase_head = 0;
        _erase_tail = 0;
        return *this;
    }

    /**
     * @brief 清空数据
     */
    void clear() {
        _erase_head = 0;
        _erase_tail = 0;
        _str.clear();
    }

    /**
     * @brief 下标运算符
     * @param pos 位置
     * @return 字符引用
     */
    char &operator[](size_t pos) {
        if (pos >= size()) {
            throw std::out_of_range(
                "BufferLikeString::operator[] out_of_range");
        }
        return data()[pos];
    }

    /**
     * @brief 下标运算符
     * @param pos 位置
     * @return 常量字符引用
     */
    const char &operator[](size_t pos) const {
        return (*const_cast<BufferLikeString *>(this))[pos];
    }

    /**
     * @brief 获取容量
     * @return 容量
     */
    size_t capacity() const { return _str.capacity(); }

    /**
     * @brief 预分配内存
     * @param size 大小
     * 设置_str的容量
     */
    void reserve(size_t size) { _str.reserve(size); }

    /**
     * @brief 重置大小
     * @param size 大小
     * @param c 填充字符
     * 为啥resize之后，_erase_head和_erase_tail置为0
     * 不应该讨论一下和size的关系吗？
     */
    void resize(size_t size, char c = '\0') {
        _str.resize(size, c);
        _erase_head = 0;
        _erase_tail = 0;
    }

    /**
     * @brief 判断是否为空
     * @return 是否为空
     */
    bool empty() const { return size() <= 0; }

    /**
     * @brief 获取子串
     * @param pos 起始位置
     * @param n 长度
     * @return 子串
     * npos: 静态常量，为size_t类型的最大值，这里表示直到字符串末尾
     */
    std::string substr(size_t pos, size_t n = std::string::npos) const {
        if (n == std::string::npos) {
            // 获取末尾所有的
            if (pos >= size()) {
                throw std::out_of_range(
                    "BufferLikeString::substr out_of_range");
            }
            return _str.substr(_erase_head + pos, size() - pos);
        }
        // 超出有效范围
        if (pos + n > size()) {
            throw std::out_of_range("BufferLikeString::substr out_of_range");
        }
        // 获取部分
        return _str.substr(_erase_head + pos, n);
    }

private:
    /**
     * @brief 移动数据
     */
    void moveData() {
        if (_erase_head) {
            // 调用string的erase方法，删除前半部分数据, 会移动剩余字符填补被删除的空间
            // _str.data()的首地址不变, 整个过程和_erase_tail无关
            _str.erase(0, _erase_head);  
            _erase_head = 0;
        }
    }

private:
    size_t _erase_head;  // _str的前半部分无效的内容的长度
    size_t _erase_tail;  // _str的后半部分无效的内容的长度
    std::string _str;
    ObjectStatistic<BufferLikeString> _statistic; // 对象个数统计
};

} // namespace toolkit
#endif // ZLTOOLKIT_BUFFER_H
