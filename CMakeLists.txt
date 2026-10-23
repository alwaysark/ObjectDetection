cmake_minimum_required(VERSION 3.16)
project(FireSmokeDetection)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 本项目仅支持 MSVC 工具链
if (NOT MSVC)
    message(FATAL_ERROR "本项目仅支持 MSVC 工具链：请在 CLion 的 CMake 配置文件中选用 Visual Studio 工具链")
endif ()

# ==== 本机依赖路径（按安装位置修改；均需 MSVC 版）====
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")
set(OpenCV_DIR "D:/Libraries/opencv-4.13.0-msvc/opencv/build")
set(VS_REDIST "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC")
set(UCRTBASED_DLL "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll")

# 源码为 UTF-8；/Zc:__cplusplus 为 Qt6+MSVC 标配；NOMINMAX 防 windows.h 的 min/max 宏污染 std::min/max
add_compile_options(/utf-8 /Zc:__cplusplus)
add_compile_definitions(NOMINMAX)

# Qt：界面 / 信号槽 / QThread / QSoundEffect 警报音
find_package(Qt6 COMPONENTS Core Gui Widgets Multimedia REQUIRED)

# OpenCV（官方 MSVC 预编译版，opencv_world 单库）：解码、前处理 blob、cv::dnn 推理、NMS、画框
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs dnn)

add_executable(FireSmokeDetection
        main.cpp
        MainWindow.cpp MainWindow.h
        Detection.cpp    Detection.h
        VideoSource.cpp  VideoSource.h
        YoloDetector.cpp YoloDetector.h)

target_link_libraries(FireSmokeDetection
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia
        ${OpenCV_LIBS})

target_include_directories(FireSmokeDetection PRIVATE ${OpenCV_INCLUDE_DIRS})

# ---- 部署辅助：把存在的文件拷到 exe 旁的 subdir 子目录（"" = exe 根）----
function(deploy_to target subdir)
    set(dst "$<TARGET_FILE_DIR:${target}>")
    if (NOT "${subdir}" STREQUAL "")
        string(APPEND dst "/${subdir}")
    endif ()
    foreach (f IN LISTS ARGN)
        if (EXISTS "${f}")
            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${dst}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${f}" "${dst}")
        endif ()
    endforeach ()
endfunction()

if (WIN32)
    # ==== 运行时部署：依赖全部拷到 exe 旁边，脱离开发环境双击即可运行 ====

    # Qt / OpenCV 运行库（Network 是 Multimedia 的依赖，缺了加载期起不来）
    foreach (LIB Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Network opencv_world)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${LIB}>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()

    # MSVC 运行库（Release + Debug 都拷，调试版 exe 起得来）与 ucrtbased.dll
    file(GLOB CRT_DLLS
            "${VS_REDIST}/*/x64/Microsoft.VC*.CRT/*.dll"
            "${VS_REDIST}/*/x64/Microsoft.VC*.DebugCRT/*.dll")
    deploy_to(${PROJECT_NAME} "" ${CRT_DLLS} "${UCRTBASED_DLL}")

    # Qt 插件：GUI 平台插件 + 音频后端（Debug 构建加载带 d 后缀的调试版）
    get_filename_component(QT_PLUGINS "${Qt6Core_DIR}/../../../plugins" ABSOLUTE)
    deploy_to(${PROJECT_NAME} plugins/platforms
            "${QT_PLUGINS}/platforms/qwindows.dll"
            "${QT_PLUGINS}/platforms/qwindowsd.dll")
    deploy_to(${PROJECT_NAME} plugins/multimedia
            "${QT_PLUGINS}/multimedia/windowsmediaplugin.dll"
            "${QT_PLUGINS}/multimedia/windowsmediaplugind.dll")

    # 资源目录 need/（模型、类别文件、警报音），程序按相对路径取用
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/need" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/need")
endif ()
