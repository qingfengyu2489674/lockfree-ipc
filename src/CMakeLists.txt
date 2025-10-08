# src/CMakeLists.txt

# 将 calculator.cpp 编译成一个名为 "mylib" 的静态库
add_library(mylib STATIC
    calculator.cpp
)

# 头文件目录（公开给依赖 mylib 的目标）
target_include_directories(mylib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
