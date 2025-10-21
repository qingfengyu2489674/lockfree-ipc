// GarbageNode.cpp
#include <utility>
#include "GarbageNode.hpp"

GarbageNode::GarbageNode()
    : next(nullptr), garbage_ptr(nullptr), deleter(nullptr) {}

GarbageNode::GarbageNode(void* ptr, void (*deleter)(void*)) 
    : garbage_ptr(ptr), deleter(deleter) {}

GarbageNode::~GarbageNode() {
    if (deleter && garbage_ptr) {
        deleter(garbage_ptr);
    }
}

