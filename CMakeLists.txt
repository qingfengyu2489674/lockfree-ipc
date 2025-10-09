# CMakeLists.txt (根目录)

# 1. 项目基本设置
cmake_minimum_required(VERSION 3.14)
project(CppProjectTemplate LANGUAGES CXX)


# 推荐设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. 启用测试
# 这一行告诉 CMake，这个项目包含测试，之后可以用 ctest 命令运行
enable_testing()

# 3. 引入本地 GoogleTest
# 假设你已经把 googletest 解压/克隆到 third_party/googletest
add_subdirectory(third_party/googletest)

# 4. 包含子目录
add_subdirectory(src)
add_subdirectory(tests)