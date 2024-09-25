#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "utility.h"


// 用于检查类型是否为指针的模板结构体
template <typename T>
struct is_pointer : public std::false_type {};

template <typename T>
struct is_pointer<std::shared_ptr<T>> : public std::true_type {};

template <typename T>
struct is_pointer<std::shared_ptr<T const>> : public std::true_type {};

template <typename T>
struct is_pointer<T*> : public std::true_type {};

template <typename T>
struct is_pointer<const T*> : public std::true_type {};

// 缓冲区基类，定义了缓冲区的基本接口
class Buffer : public Noncopyable {
public:
    using Ptr = std::shared_ptr<Buffer>;

    Buffer() = default;
    virtual ~Buffer() = default;

public:
    virtual char* data() const = 0;
    virtual size_t size() const = 0;
    virtual std::string toString() const = 0;
    virtual size_t getCapacity() const = 0;

private:
    ObjectCounter<Buffer> counter_;  // 统计Buffer实例的个数
};

// 基于偏移量的缓冲区实现
template <typename C>
class BufferOffset : public Buffer {
public:
    BufferOffset(C data, size_t offset = 0, size_t len = 0) {}
    ~BufferOffset() override = default;

public:
    char* data()  const override {
        return const_cast<char*>(getPointer<C>(data_)->data()) + offset_;
    }

    size_t size() const override { return size_; }
    size_t getCapacity() const override { return size(); }
    std::string toString() const override { return std::string(data(), size()); }

private:
    // 设置偏移和长度
    void setup(size_t offset = 0, size_t size = 0) {
        auto max_size = getPointer<C>(data_)->size();
        assert(offset + size <= max_size);
        if (!size) {
            size = max_size - offset;
        }
        size_ = size;
        offset_ = offset;
    }
    // 获取指针的静态模板函数
    template <typename T>
    static typename std::enable_if<is_pointer<T>::value, const T&>::type
    getPointer(const T& data) { return data; }

    template <typename T>
    static typename std::enable_if<!is_pointer<T>::value, const T*>::type
    getPointer(const T& data) { return &data; }

private:
    C data_;
    size_t size_;
    size_t offset_;
    ObjectCounter<BufferOffset> counter_;
};

using BufferString = BufferOffset<std::string>;

class BufferRaw : public Buffer {
public:
    using Ptr = std::shared_ptr<BufferRaw>;
    
    ~BufferRaw() override {
        if (data_) {
            delete [] data_;
        }
    }

public:
    char* data() const override { return data_; }
    size_t size() const override { return size_; }
    std::string toString() const override { return std::string(data(), size()); }
    size_t getCapacity() const override {return capacity_; }
    void setCapacity(size_t capacity) {
        if (data_) {
            do{
                if (capacity > capacity_) {
                    break;
                }
                if (capacity <= capacity_) {
                    break;
                }
            } while (false);
        }
        data_ = new char[capacity];
        capacity_ = capacity;
    }
    virtual void setSize(size_t size) {
        if (size > capacity_) {
            throw std::invalid_argument("Buffer::setSize out of range");
        }
        size_ = size;
    }
    // 设置缓冲区中的数据
    void assign(const char* data, size_t size = 0) {
        if (size <= 0) {
            size = strlen(data);
        }
        setCapacity(size + 1);
        memcpy(data_, data, size);
        data_[size] = '\0';
        setSize(size);
    }

protected:
    // friend class ResourcePool_l<BufferRaw>;

    BufferRaw(size_t capacity = 0) {
        if (capacity) {
            setCapacity(capacity);
        }
    }
    BufferRaw(const char* data, size_t size = 0) { assign(data, size); }

private:
    size_t size_ = 0;
    size_t capacity_ = 0;
    char* data_ = nullptr;
    ObjectCounter<BufferRaw> counter_;
};

class BufferLikeString : public Buffer {
public:
    BufferLikeString() : erase_head_{0}, erase_tail_{0} {}
    BufferLikeString(std::string str) : str_{std::move(str)}, erase_head_{0}, erase_tail_{0} {}
    BufferLikeString(const char* str) : str_{str}, erase_head_{0}, erase_tail_{0} {}
    BufferLikeString(const BufferLikeString& that) {
        str_ = that.str_;
        erase_head_ = that.erase_head_;
        erase_tail_ = that.erase_tail_;
    }
    BufferLikeString(BufferLikeString&& that) {
        str_ = std::move(that.str_);
        erase_head_ = that.erase_head_;
        erase_tail_ = that.erase_tail_;
        that.erase_head_ = 0;
        that.erase_tail_ = 0;
    }
    ~BufferLikeString() override = default;
    // 重载拷贝赋值运算符
    BufferLikeString& operator=(std::string str) {
        str_ = std::move(str);
        erase_head_ = 0;
        erase_tail_ = 0;
        return *this;
    }

    BufferLikeString& operator=(const char* str) {
        str_ = str;
        erase_head_ = 0;
        erase_tail_ = 0;
        return *this;
    }

    BufferLikeString& operator=(const BufferLikeString& that) {
        str_ = that.str_;
        erase_head_ = that.erase_head_;
        erase_tail_ = that.erase_tail_;
        return *this;
    }
    // 重载移动赋值运算符
    BufferLikeString& operator=(BufferLikeString&& that) {
        str_ = std::move(that.str_);
        erase_head_ = that.erase_head_;
        erase_tail_ = that.erase_tail_;
        that.erase_head_ = 0;
        that.erase_tail_ = 0;
        return *this;
    }

public:
    char* data() const override { return const_cast<char*>(str_.data()) + erase_head_; }
    size_t size() const override { return str_.size() - erase_head_ - erase_tail_; }
    std::string toString() const override { return str_.substr(erase_head_, size()); }
    size_t getCapacity() const override { return str_.capacity(); }

    BufferLikeString& append(const char* data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        if (erase_head_ > str_.capacity() / 2) {
            moveData();
        }
        if (erase_tail_ == 0) {
            str_.append(data, len);
            return *this;
        }
        str_.insert(erase_head_ + size(), data, len);
        return *this;
    }

    BufferLikeString& append(const std::string& str) { return append(str.data(), str.size()); }
    BufferLikeString& append(const char* data) { return append(data, strlen(data)); }

    void push_back(char c) {
        if (erase_tail_ == 0) {
            str_.push_back(c);
            return ;
        }
        data()[size()] = c;
        --erase_tail_;
        data()[size()] = '\0';
    }

    BufferLikeString& insert(size_t pos, const char* s, size_t n) {
        if (erase_head_ > str_.capacity() / 2) {
            moveData();
        }
        str_.insert(erase_head_ + pos, s, n);
        return *this;
    }

    BufferLikeString& assign(const char* data, size_t len) {
        if (len <= 0) {
            return *this;
        }
        if (data >= str_.data() && data < str_.data() + str_.size()) {
            if (data + len > str_.data() + str_.size()) {
                throw std::out_of_range("BufferLikeString::assign out_of_range");
            }
            erase_head_ = data - str_.data();
            erase_tail_ = str_.size() - (erase_head_ + len);
            return *this;
        }
        str_.assign(data, len);
        erase_head_ = 0;
        erase_tail_ = 0;
        return *this;
    }
    BufferLikeString& assign(const char* data) { return assign(data, strlen(data)); }
    BufferLikeString& assign(const std::string& str) { return assign(str.data(), str.size()); }

    void clear() {
        erase_head_ = 0;
        erase_tail_ = 0;
        str_.clear();
    }

    char& operator[](size_t pos) {
        if (pos > size()) {
            throw std::out_of_range("BufferLikeString::operator[] out_of_range");
        }
        return data()[pos];
    }

    size_t capacity() const { return str_.capacity(); }
    void reserve(size_t size) { str_.reserve(size); }
    bool empty() const { return size() <= 0; }
    // 调整缓冲区大小，这里不需要根据size来调整erase_head_和erase_tail_吗？
    void resize(size_t size, char c = '\0') {
        str_.resize(size, c);
        erase_head_ = 0;
        erase_tail_ = 0;
    }

    std::string substr(size_t pos, size_t n = std::string::npos) const {
        if (n == std::string::npos) {
            if (pos >= size()) {
                throw std::out_of_range("BufferLikeString::substr out_of_range");
            }
            return str_.substr(erase_head_ + pos, size() - pos);
        }
        if (pos + n > size()) {
            throw std::out_of_range("BufferLikeString::substr out_of_range");
        }
        return str_.substr(erase_head_ + pos, n);
    }

private:
    void moveData() {
        if (erase_head_ > 0) {
            str_.erase(0, erase_head_);
            erase_head_ = 0;
        }
    }

private:
    size_t erase_head_ = 0;  // 记录str_前半部分无效内容的长度
    size_t erase_tail_ = 0;  // 记录str_后半部分无效内容的长度
    std::string str_;
    ObjectCounter<BufferLikeString> counter_;
};

// 统计缓冲区对象个数
StatisticImpl(Buffer) 
StatisticImpl(BufferRaw) 
StatisticImpl(BufferLikeString)
#endif
