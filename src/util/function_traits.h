#ifndef _FUNCTION_TRAITS_H_
#define _FUNCTION_TRAITS_H_

#include <functional>
#include <tuple>

namespace xkernel {

template <typename T>
struct function_traits;

// 函数类型的特化
template <typename Ret, typename... Args>
struct function_traits<Ret(Args...)> {
public:
    static constexpr size_t arity = sizeof...(Args);
    using function_type = Ret(Args...);
    using return_type = Ret;
    using stl_function_type = std::function<function_type>;
    using pointer = Ret(*)(Args...);

    template<size_t I>
    struct args {
        static_assert(I < arity, "index is out of rang, index must less than Args");
        using type  = typename std::tuple_element<I, std::tuple<Args...>>::type;
    };
};

// 函数指针的特化
template <typename Ret, typename... Args>
struct function_traits<Ret(*)(Args...)> : function_traits<Ret(Args...)> {};

// std::function 的特化
template <typename Ret, typename... Args>
struct function_traits<std::function<Ret(Args...)>> : function_traits<Ret(Args...)> {};

// 成员函数的特化
// 这个宏用于生成不同 cv 限定符的成员函数特化
#define FUNCTION_TRAITS(...)                                               \
    template <typename ReturnType, typename ClassType, typename... Args>   \
    struct function_traits<ReturnType (ClassType::*)(Args...) __VA_ARGS__> \
        : function_traits<ReturnType(Args...)> {};

FUNCTION_TRAITS()
FUNCTION_TRAITS(const)
FUNCTION_TRAITS(volatile)
FUNCTION_TRAITS(const volatile)

// 函数对象(仿函数)（包括 lambda 表达式）的特化，所有具有operator()的可调用类型
template <typename Callable>
struct function_traits : function_traits<decltype(&Callable::operator())> {};

}
#endif