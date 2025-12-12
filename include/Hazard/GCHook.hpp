#pragma once

// 在 HazardPointer 的头文件中定义
struct GCHook {
    GCHook* gc_next = nullptr; // 专门给回收器用的指针
    virtual ~GCHook() = default; // 虚析构，保证 delete 基类指针时能调用子类析构
};