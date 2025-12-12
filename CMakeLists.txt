# CMakeLists.txt (根目录)

# 1. 项目基本设置
cmake_minimum_required(VERSION 3.14)
project(CppProjectTemplate LANGUAGES CXX)


# 推荐设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
#  Sanitizer 配置区域 (添加到 CMakeLists.txt 顶部)
# =============================================================================

# 定义开关，默认关闭
option(ENABLE_TSAN "Enable Thread Sanitizer (检测线程竞争)" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer (检测内存越界/悬垂指针)" OFF)

if(ENABLE_TSAN AND ENABLE_ASAN)
    message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be enabled at the same time!")
endif()

# 1. Thread Sanitizer (TSan) - 专门查并发竞态
# 1. Thread Sanitizer (TSan) - 专门查并发竞态
if(ENABLE_TSAN)
    message(STATUS "Build with Thread Sanitizer...")
    # 注意：这里增加了 -fno-pie
    add_compile_options(-fsanitize=thread -g -O1 -fno-pie)
    # 注意：链接选项也要加 -no-pie (有些系统是 -static-libtsan，但通常 -no-pie 足够)
    add_link_options(-fsanitize=thread -no-pie)
endif()

# 2. Address Sanitizer (ASan) + UBSan - 查内存错误和未定义行为
if(ENABLE_ASAN)
    message(STATUS "Build with Address Sanitizer...")
    # -fno-omit-frame-pointer: 让堆栈回溯更清晰
    # -fsanitize=undefined: 顺便开启未定义行为检测
    add_compile_options(-fsanitize=address -fsanitize=undefined -g -O1 -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# =============================================================================

# 2. 启用测试
# 这一行告诉 CMake，这个项目包含测试，之后可以用 ctest 命令运行
enable_testing()

# 3. 引入本地 GoogleTest
# 假设你已经把 googletest 解压/克隆到 third_party/googletest
add_subdirectory(third_party/googletest)

# 4. 包含子目录
add_subdirectory(src)
add_subdirectory(tests)