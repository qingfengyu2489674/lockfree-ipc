// GarbageNode.hpp
#pragma once

#include <utility>

class GarbageNode {
public:
    GarbageNode();

    GarbageNode(void* ptr, void (*deleter)(void*));

    ~GarbageNode();

    // 禁止拷贝和移动
    GarbageNode(const GarbageNode&) = delete;
    GarbageNode& operator=(const GarbageNode&) = delete;
    GarbageNode(GarbageNode&&) = delete;
    GarbageNode& operator=(GarbageNode&&) = delete;

public:
    GarbageNode* next = nullptr;

    void* garbage_ptr = nullptr;
    void (*deleter)(void*); 
};