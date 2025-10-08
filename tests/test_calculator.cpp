// tests/test_calculator.cpp
#include <gtest/gtest.h>
#include "mylib/calculator.hpp" // 包含我们要测试的库的头文件

// 使用 TEST 宏来定义一个测试用例
// 第一个参数是测试套件(TestSuite)的名称
// 第二个参数是测试用例(TestCase)的名称
TEST(CalculatorTest, ShouldAddTwoNumbers) {
    Calculator calc;
    // 使用断言来检查结果是否符合预期
    ASSERT_EQ(calc.add(2, 3), 5);
}