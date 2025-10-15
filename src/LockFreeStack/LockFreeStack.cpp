#include <iostream>
#include <cstdlib> // For malloc, free
#include <utility> // For std::forward

// ============================================================================
// --- 桩实现 (Stub Implementations) ---
// 为了让这个文件能独立编译，我们需要提供所有依赖的最小实现。
// ============================================================================


// ============================================================================
// --- 包含我们真正要测试的头文件 ---
// 这些应该是你项目中的实际头文件路径
// ============================================================================
#include "LockFreeStack/LockFreeStack.hpp"


// ============================================================================
// --- 显式模板实例化 (可选，但有助于捕获错误) ---
// ============================================================================

// 告诉编译器为 LockFreeStack<int> 生成所有代码
template class LockFreeStack<int>;

// ============================================================================
// --- Main 函数 (核心测试逻辑) ---
// ============================================================================

int main() {
    std::cout << "Starting LockFreeStack compilation and instantiation test..." << std::endl;

    // 定义要使用的具体类型
    using MyNode = StackNode<int>;
    // 对于栈，危险指针数量固定为 1
    constexpr size_t kHazardPointersForStack = 1;
    
    // 使用默认的 DefaultHeapPolicy (它内部会调用我们上面定义的桩实现)
    using MySlotManager    = HpSlotManager<MyNode, kHazardPointersForStack>;
    using MyRetiredManager = HpRetiredManager<MyNode>;
    
    // 实例化管理器
    MySlotManager slot_mgr;
    MyRetiredManager retired_mgr;

    // 实例化 LockFreeStack
    LockFreeStack<int> stack(slot_mgr, retired_mgr);
    std::cout << "Stack object successfully instantiated." << std::endl;

    // 测试基本功能
    stack.push(42);
    std::cout << "Pushed 42 onto the stack." << std::endl;
    
    int value = 0;
    if (stack.tryPop(value)) {
        std::cout << "Successfully popped: " << value << std::endl;
        if (value != 42) {
            std::cerr << "Error: Popped value mismatch!" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "Error: Failed to pop from a non-empty stack!" << std::endl;
        return 1;
    }

    if (!stack.isEmpty()) {
        std::cerr << "Error: Stack should be empty after popping!" << std::endl;
        return 1;
    }

    std::cout << "Compilation and basic functionality test successful." << std::endl;

    return 0;
}