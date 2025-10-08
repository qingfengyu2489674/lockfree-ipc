# C++ Project Template

这是一个现代化的、开箱即用的 C++ 项目模板。
构建系统使用 **CMake**；单元测试使用 **GoogleTest**，**已内置在 `third_party/googletest/`，无需联网下载**。

## ✨ 特性

* **现代 CMake**：采用 `target_*` 风格，结构清晰、可维护。
* **离线可构建**：GoogleTest 已固定版本并随仓库分发，不依赖 `FetchContent`。
* **清晰的目录结构**：接口 (`include`)、实现 (`src`)、测试 (`tests`)、第三方依赖 (`third_party`) 完全分离。
* **跨平台**：Linux / macOS / Windows（MSVC / MinGW / WSL）均可。
* **易扩展**：便于新增库、可执行文件和测试。

## 📂 目录结构

```
.
├── CMakeLists.txt          # 根 CMakeLists（add_subdirectory(third_party/googletest)）
├── include/                # 头文件 (.hpp)
│   └── mylib/
│       └── calculator.hpp
├── src/                    # 源文件 (.cpp)
│   ├── CMakeLists.txt
│   └── calculator.cpp
├── tests/                  # 测试代码 (test_*.cpp)
│   ├── CMakeLists.txt
│   └── test_calculator.cpp
└── third_party/
    └── googletest/         # 本地固定版本的 GoogleTest（目录内应包含官方 CMakeLists.txt）
```

> **注意**：`third_party/googletest/` 目录**必须直接包含**官方的 `CMakeLists.txt`。如果是用 zip 解压，请确保没有多嵌套一层目录。

## 🚀 快速开始

### 环境要求

* C++ 编译器（GCC / Clang / MSVC 等）
* CMake (≥ 3.14)
* Git

### 构建步骤（Out-of-Source Build）

在项目根目录执行：

```bash
mkdir build
cd build
cmake ..
cmake --build .
ctest --verbose
```

* 首次构建也**不需要网络**，因为 GoogleTest 已内置在 `third_party/googletest/`。
* 测试通过时可见 `100% tests passed`。

## 🧩 关键 CMake 片段（摘要）

**根目录 `CMakeLists.txt`（核心差异：移除 FetchContent，改为本地子目录）**

```cmake
cmake_minimum_required(VERSION 3.14)
project(CppProjectTemplate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# 使用本地 vendored 的 googletest（无需联网）
add_subdirectory(third_party/googletest)

add_subdirectory(src)
add_subdirectory(tests)
```

**`src/CMakeLists.txt`（示例）**

```cmake
add_library(mylib STATIC
    calculator.cpp
)

target_include_directories(mylib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
```

**`tests/CMakeLists.txt`（示例）**

```cmake
add_executable(run_tests
    test_calculator.cpp
    # 可在此追加更多测试文件，如 test_advanced_math.cpp
)

target_link_libraries(run_tests PRIVATE
    mylib
    gtest_main
)

target_include_directories(run_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(run_tests)
```

## 📝 如何扩展项目

### 场景：添加 `AdvancedMath` 类

1. 新增头文件：`include/mylib/advanced_math.hpp`

```cpp
#pragma once
class AdvancedMath {
public:
    long long multiply(int a, int b);
};
```

2. 新增源文件：`src/advanced_math.cpp`

```cpp
#include "mylib/advanced_math.hpp"
long long AdvancedMath::multiply(int a, int b) {
    return static_cast<long long>(a) * b;
}
```

3. 在 `src/CMakeLists.txt` 中注册：

```cmake
add_library(mylib STATIC
    calculator.cpp
    advanced_math.cpp   # 新增
)

target_include_directories(mylib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
```

4. 添加测试：`tests/test_advanced_math.cpp`

```cpp
#include <gtest/gtest.h>
#include "mylib/advanced_math.hpp"

TEST(AdvancedMathTest, Multiply) {
    AdvancedMath m;
    EXPECT_EQ(m.multiply(5, 10), 50);
    EXPECT_EQ(m.multiply(-5, 10), -50);
}
```

5. 在 `tests/CMakeLists.txt` 中加入到可执行文件：

```cmake
add_executable(run_tests
    test_calculator.cpp
    test_advanced_math.cpp   # 新增
)
```

然后重新构建并运行测试：

```bash
cd build
cmake --build .
ctest --verbose
```

## 🔄 升级 / 更换 GoogleTest 版本

1. 删除旧目录：

```bash
rm -rf third_party/googletest
```

2. 将新版本以**相同目录名**放入 `third_party/googletest/`（确保其中直接包含官方 `CMakeLists.txt`）。
3. 重新构建：

```bash
rm -rf build
mkdir build && cd build
cmake ..
cmake --build .
ctest --verbose
```

## 🧰 常见问题（FAQ）

* **CMake 报错 “does not contain a CMakeLists.txt”**
  请确认 `third_party/googletest/` **直接**含有官方 `CMakeLists.txt`。若多包一层目录（如 `third_party/googletest/googletest-1.14.0/…`），请把内层提到上一层或重命名为 `third_party/googletest/`。

* **`cmake ..` 提示源目录不含 CMakeLists.txt**
  确认当前目录是 `build/`，且 `..` 指向的是项目根目录（根目录下应有 `CMakeLists.txt`）。

* **测试数量为 0**
  多为构建失败或 `run_tests` 未生成导致。先确保构建成功；再确认 `tests/CMakeLists.txt` 已包含你的测试源并链接 `gtest_main`。

---

> 提示：若希望“clone 即可构建”，请**将 `third_party/googletest/` 目录提交进仓库**（非 submodule、非 FetchContent）。这样他人直接 `git clone` 后即可离线构建与测试。
