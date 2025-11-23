#include <iostream>
#include <thread>
#include <vector>

// 一个简单的原子整数类，使用x86内联汇编实现
class MyAtomicInt {
private:
    // 我们要原子操作的整数值
    // 使用 alignas(4) 确保其4字节对齐，这对于原子操作性能和正确性很重要
    alignas(4) int value;

public:
    // 构造函数
    explicit MyAtomicInt(int initial_value = 0) : value(initial_value) {}

    // 原子地加载值
    // 在x86上，对齐的32位整数的读取本身就是原子的。
    // 但为了确保内存可见性（防止编译器和CPU重排），
    // 真正的std::atomic<int>::load()会包含内存屏障。
    // 为简单起见，这里我们直接返回，但在下面我们会使用更强的操作。
    int load() const {
        return value;
    }

    // 原子地存储值
    void store(int desired) {
        // 使用 xchg 指令。它原子地交换寄存器和内存中的值。
        // 它天生就是一个带完全内存屏障的原子操作。
        __asm__ volatile (
            "xchgl %0, %1"
            : "+r" (desired), "+m" (value) // desired是可读写寄存器, value是可读写内存
            : // 无纯输入操作数
            : "memory" // 告诉编译器内存被修改
        );
    }

    // 原子地增加一个值，并返回增加前的值
    // 这是 fetch_add 的经典实现
    int fetch_add(int arg) {
        // 使用 lock xadd 指令。
        // 它原子地将 arg 和 value 相加，结果存入 value，
        // 并将 value 的原始值存入 arg。
        __asm__ volatile (
            "lock xaddl %0, %1"
            : "+r" (arg), "+m" (value)
            :
            : "memory"
        );
        return arg; // arg 现在持有 value 的原始值
    }
    
    // 实现 compare-and-swap (CAS)
    // 如果当前值等于 expected，则将其设置为 desired，并返回 true。
    // 否则，将 expected 更新为当前值，并返回 false。
    bool compare_exchange(int& expected, int desired) {
        char result;
        // 使用 lock cmpxchg 指令。
        // 它会隐式地使用 EAX 寄存器。
        // 1. 比较 EAX (我们放入 expected) 和 value 的值。
        // 2. 如果相等，将 desired 写入 value，并设置ZF标志位为1。
        // 3. 如果不相等，将 value 写入 EAX (即更新 expected)，并设置ZF为0。
        // "setz %0" 指令根据ZF标志位设置结果（如果ZF=1, result=1, 否则=0）。
        __asm__ volatile (
            "lock cmpxchgl %3, %1\n\t"
            "setz %0"
            : "=q" (result), "+m" (value), "+a" (expected) // a: EAX寄存器, q: 通用寄存器
            : "r" (desired) // r: 任意通用寄存器
            : "memory"
        );
        return (result != 0);
    }
    
    // 提供一个方便的前缀自增运算符
    int operator++() {
        // fetch_add(1) 返回旧值，所以新值是旧值+1
        return fetch_add(1) + 1;
    }
};

// --- 下面是测试代码 ---

// 我们自己的原子计数器
MyAtomicInt atomic_counter(0);

// 一个普通的、非线程安全的计数器
int normal_counter = 0;

// 每个线程执行的任务
void worker_task() {
    for (int i = 0; i < 1000000; ++i) {
        atomic_counter.fetch_add(1); // 使用我们的原子类
        normal_counter++;            // 使用普通int，会产生竞争条件
    }
}

int main() {
    std::cout << "Starting multithreaded counter test..." << std::endl;

    // 创建两个线程
    std::vector<std::thread> threads;
    threads.push_back(std::thread(worker_task));
    threads.push_back(std::thread(worker_task));

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 两个线程，每个加1,000,000次
    const int expected_value = 2000000;

    std::cout << "========================================" << std::endl;
    std::cout << "Expected final value: " << expected_value << std::endl;
    std::cout << "MyAtomicInt final value: " << atomic_counter.load() << std::endl;
    std::cout << "Normal int final value:  " << normal_counter << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (atomic_counter.load() == expected_value) {
        std::cout << "[SUCCESS] MyAtomicInt worked correctly!" << std::endl;
    } else {
        std::cout << "[FAILURE] MyAtomicInt has a bug!" << std::endl;
    }

    if (normal_counter == expected_value) {
        std::cout << "[INFO] Normal int got lucky, but this is usually not the case." << std::endl;
    } else {
        std::cout << "[INFO] Normal int shows a race condition, as expected." << std::endl;
    }

    return 0;
}