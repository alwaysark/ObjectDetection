cmake_minimum_required(VERSION 3.16)
project(AlarmServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 本项目仅支持 MSVC 工具链（顶层已检查，独立配置 server/ 时兜底再查一次）
if (NOT MSVC)
    message(FATAL_ERROR "本项目仅支持 MSVC 工具链")
endif ()

# ==== 本机依赖路径（与客户端同一套 Qt）====
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")
set(VS_REDIST "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC")
set(UCRTBASED_DLL "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll")

# 源码为 UTF-8；/Zc:__cplusplus 为 Qt6+MSVC 标配；NOMINMAX 防 windows.h 宏污染
add_compile_options(/utf-8 /Zc:__cplusplus)
add_compile_definitions(NOMINMAX)

# 报警统计平台：Qt Widgets 界面 + TCP 收报警（Network）+ SQLite 落库（Sql）
# + QCustomPlot 曲线（依赖 PrintSupport）。纯 Qt，无 OpenCV/onnxruntime 依赖
find_package(Qt6 COMPONENTS Widgets Network Sql PrintSupport REQUIRED)

add_executable(AlarmServer
        main.cpp
        ServerWindow.cpp ServerWindow.h
        AlarmDb.cpp      AlarmDb.h
        qcustomplot.cpp  qcustomplot.h)

target_link_libraries(AlarmServer
        Qt6::Widgets Qt6::Network Qt6::Sql Qt6::PrintSupport)

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
    # ==== 运行时部署：依赖全部拷到 exe 旁，脱离开发环境双击即可运行 ====

    foreach (LIB Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Sql Qt6::PrintSupport)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${LIB}>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()

    # MSVC 运行库（Release + Debug 都拷）与 ucrtbased.dll
    file(GLOB CRT_DLLS
            "${VS_REDIST}/*/x64/Microsoft.VC*.CRT/*.dll"
            "${VS_REDIST}/*/x64/Microsoft.VC*.DebugCRT/*.dll")
    deploy_to(${PROJECT_NAME} "" ${CRT_DLLS} "${UCRTBASED_DLL}")

    # Qt 插件：GUI 平台插件 + SQLite 驱动（Debug 构建加载带 d 后缀的调试版）
    get_filename_component(QT_PLUGINS "${Qt6Core_DIR}/../../../plugins" ABSOLUTE)
    deploy_to(${PROJECT_NAME} plugins/platforms
            "${QT_PLUGINS}/platforms/qwindows.dll"
            "${QT_PLUGINS}/platforms/qwindowsd.dll")
    deploy_to(${PROJECT_NAME} plugins/sqldrivers
            "${QT_PLUGINS}/sqldrivers/qsqlite.dll"
            "${QT_PLUGINS}/sqldrivers/qsqlited.dll")
endif ()
