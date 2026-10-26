cmake_minimum_required(VERSION 3.16)
project(FireSmokeDetectionCs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 本项目仅支持 MSVC 工具链
if (NOT MSVC)
    message(FATAL_ERROR "本项目仅支持 MSVC 工具链：请在 CLion 的 CMake 配置文件中选用 Visual Studio 工具链")
endif ()

# ==== 本机依赖路径（按安装位置修改；均需 MSVC 版，Client/Server 共用）====
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")
set(OpenCV_DIR "D:/Libraries/opencv-4.13.0-msvc/opencv/build")
set(VS_REDIST "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC")
set(UCRTBASED_DLL "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll")

# 源码为 UTF-8；/Zc:__cplusplus 为 Qt6+MSVC 标配；NOMINMAX 防 windows.h 的 min/max 宏污染 std::min/max
add_compile_options(/utf-8 /Zc:__cplusplus)
add_compile_definitions(NOMINMAX)

# 客户端：单机检测 + 报警外发（Qt + OpenCV + cv::dnn 推理）
add_subdirectory(Client)
# 服务端：报警统计平台（纯 Qt，无 OpenCV 依赖）
add_subdirectory(Server)
