cmake_minimum_required(VERSION 3.16)
project(FireSmokeDetection)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 本项目仅支持 MSVC 工具链（Qt 6.8.3 msvc2022_64 + 官方 OpenCV MSVC 预编译版）
if (NOT MSVC)
    message(FATAL_ERROR "本项目仅支持 MSVC 工具链：请在 CLion 的 CMake 配置文件中选用 Visual Studio 工具链")
endif ()

# 锁定 Qt 6.8.3 MSVC 套件（client/server 统一从这里继承）
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")

# 源码为 UTF-8；/Zc:__cplusplus 为 Qt6+MSVC 标配；NOMINMAX 防 windows.h 的 min/max 宏污染
add_compile_options(/utf-8 /Zc:__cplusplus)
add_compile_definitions(NOMINMAX)

add_subdirectory(client)
add_subdirectory(server)