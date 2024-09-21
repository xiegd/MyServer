
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
}