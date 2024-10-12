#include <gtest/gtest.h>
#include "function_traits.h"
#include <functional>

using namespace xkernel;

// 测试普通函数
int testFunction(double d, char c) { return 0; }

// 测试函数对象
struct Functor {
    float operator()(int i, double d) const { return 0.0f; }
};

// 测试成员函数
class TestClass {
public:
    void memberFunction(char c, int i) {}
    int constMemberFunction(float f) const { return 0; }
};

// 测试套件
TEST(FunctionTraitsTest, NormalFunction) {
    using Traits = function_traits<decltype(testFunction)>;
    
    EXPECT_EQ(Traits::arity, 2);
    // std::is_same_v 是 C++17 引入的类型特征，用于在编译时检查两个类型是否相同
    EXPECT_TRUE((std::is_same_v<Traits::return_type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, double>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, char>));
}

TEST(FunctionTraitsTest, FunctionPointer) {
    using FuncPtr = int(*)(double, char);
    using Traits = function_traits<FuncPtr>;
    
    EXPECT_EQ(Traits::arity, 2);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, double>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, char>));
}

TEST(FunctionTraitsTest, StdFunction) {
    using StdFunc = std::function<int(double, char)>;
    using Traits = function_traits<StdFunc>;
    
    EXPECT_EQ(Traits::arity, 2);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, double>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, char>));
}

TEST(FunctionTraitsTest, MemberFunction) {
    using Traits = function_traits<decltype(&TestClass::memberFunction)>;
    
    EXPECT_EQ(Traits::arity, 2);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, void>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, char>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, int>));
}

TEST(FunctionTraitsTest, ConstMemberFunction) {
    using Traits = function_traits<decltype(&TestClass::constMemberFunction)>;
    
    EXPECT_EQ(Traits::arity, 1);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, float>));
}

TEST(FunctionTraitsTest, Functor) {
    using Traits = function_traits<Functor>;
    
    EXPECT_EQ(Traits::arity, 2);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, float>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, double>));
}

TEST(FunctionTraitsTest, Lambda) {
    auto lambda = [](int i, double d) -> char { return 'a'; };
    using Traits = function_traits<decltype(lambda)>;
    
    EXPECT_EQ(Traits::arity, 2);
    EXPECT_TRUE((std::is_same_v<Traits::return_type, char>));
    EXPECT_TRUE((std::is_same_v<Traits::args<0>::type, int>));
    EXPECT_TRUE((std::is_same_v<Traits::args<1>::type, double>));
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
