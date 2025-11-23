#pragma once
#include <cstdint>
#include <atomic>
#include <type_traits>

#if !(defined(__GNUC__) || defined(__clang__))
# error "requires GCC/Clang for __atomic builtins

