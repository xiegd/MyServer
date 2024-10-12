/*
 * 分析和获取各种函数类型
 * 提取函数的返回类型，参数数量，参数类型等信息
*/
#ifndef SRC_UTIL_FUNCTION_TRAITS_H_
#define SRC_UTIL_FUNCTION_TRAITS_H_

#include <functional>
#include <tuple>

namespace toolkit {

// 主模板声明，用于特化不同类型的函数
template <typename T>
struct function_traits;

// 普通函数的特化
// 这里看似有两个模板参数,实际上是对主模板的特化
// Ret(Args...)作为一个整体,匹配主模板中的T
template <typename Ret, typename... Args>
struct function_traits<Ret(Args...)> {
public:
    // 函数参数的数量
    // sizeof...(Args) 是一个C++11引入的操作符，用于获取可变参数模板中参数包的大小
    // 在这里，它返回函数参数的数量, 
    enum { arity = sizeof...(Args) };  // arity指一个函数/操作所需的参数/操作数的数量
    // 这里使用枚举有以下几个原因:
    // 1. 编译时常量: 枚举值是编译时常量,可以用在模板参数、数组大小等需要编译时常量的地方
    // 2. 不占用存储空间: 枚举不会在运行时占用对象的存储空间
    // 3. 更好的封装: 相比于静态常量成员,枚举提供了更好的封装,不能被外部修改
    // 4. 兼容性: 这种技巧在C++11之前就存在,可以在不支持constexpr的旧编译器上使用
    
    // 定义函数类型
    // 等价于using function_type = Ret(Args...);
    typedef Ret function_type(Args...);
    
    // 定义返回类型
    typedef Ret return_type;
    
    // 对应的 std::function 类型
    using stl_function_type = std::function<function_type>;
    
    // 函数指针类型
    typedef Ret (*pointer)(Args...);  // 定义类型别名pointer

    // 用于获取函数参数列表中第 I 个参数类型的模板
    // 非类型模板参数，使用特定类型的常量作为模板参数，允许在编译时使用具体的数值
    // 从而实例化模板，从而在编译期确定类型, 
    // 实例化模板类后使用: Traits::arg<0>::type即可获取第0个参数的类型
    template <size_t I>
    struct args {
        // 静态断言确保 I 小于参数数量
        static_assert(I < arity, "index is out of range, index must less than sizeof Args");
        // std::tuple<Args...>, 使用传入的可变模板参数对tuple模板类进行特化, 没有创建实例
        // 使用 std::tuple_element 获取第 I 个参数的类型
        using type = typename std::tuple_element<I, std::tuple<Args...>>::type;  // 使用typename明确告诉编译器这是一个类型
    };
};

// 函数指针的特化
template <typename Ret, typename... Args>
struct function_traits<Ret (*)(Args...)> : function_traits<Ret(Args...)> {};

// std::function 的特化
template <typename Ret, typename... Args>
struct function_traits<std::function<Ret(Args...)>> : function_traits<Ret(Args...)> {};

// 成员函数的特化
// 这个宏用于生成不同 cv 限定符的成员函数特化
#define FUNCTION_TRAITS(...)                                               \
    template <typename ReturnType, typename ClassType, typename... Args>   \
    struct function_traits<ReturnType (ClassType::*)(Args...) __VA_ARGS__> \
        : function_traits<ReturnType(Args...)> {};

// 生成非 const 非 volatile 的成员函数特化
FUNCTION_TRAITS()
// 生成 const 成员函数特化
FUNCTION_TRAITS(const)
// 生成 volatile 成员函数特化
FUNCTION_TRAITS(volatile)
// 生成 const volatile 成员函数特化
FUNCTION_TRAITS(const volatile)

// 函数对象（包括 lambda 表达式）的特化
template <typename Callable>
struct function_traits : function_traits<decltype(&Callable::operator())> {};

} /* namespace toolkit */

#endif /* SRC_UTIL_FUNCTION_TRAITS_H_ */
