cmake_minimum_required(VERSION 3.16)
project(AlarmPlatformServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 本项目仅支持 MSVC 工具链
if (NOT MSVC)
    message(FATAL_ERROR "本项目仅支持 MSVC 工具链：请在 CLion 的 CMake 配置文件中选用 Visual Studio 工具链")
endif ()

# ==== 本机依赖路径（按安装位置修改；与 client 用同一套 Qt）====
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")
set(VS_REDIST "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC")
set(UCRTBASED_DLL "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll")

# 源码为 UTF-8；/Zc:__cplusplus 为 Qt6+MSVC 标配；NOMINMAX 防 windows.h 的 min/max 宏污染 std::min/max
add_compile_options(/utf-8 /Zc:__cplusplus)
add_compile_definitions(NOMINMAX)

# 告警平台模拟器：Qt 控制台程序（无界面，收告警帧/心跳帧并留档入库）
find_package(Qt6 COMPONENTS Core Network Sql REQUIRED)

add_executable(AlarmPlatformServer main.cpp)
target_link_libraries(AlarmPlatformServer Qt6::Core Qt6::Network Qt6::Sql)

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
    # ==== 运行时部署：控制台程序只需 Qt 运行库、SQLite 驱动插件与 MSVC 运行库 ====

    foreach (LIB Qt6::Core Qt6::Network Qt6::Sql)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${LIB}>" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()

    # MSVC 运行库（Release + Debug 都拷，调试版 exe 起得来）与 ucrtbased.dll
    file(GLOB CRT_DLLS
            "${VS_REDIST}/*/x64/Microsoft.VC*.CRT/*.dll"
            "${VS_REDIST}/*/x64/Microsoft.VC*.DebugCRT/*.dll")
    deploy_to(${PROJECT_NAME} "" ${CRT_DLLS} "${UCRTBASED_DLL}")

    # QSQLITE 驱动插件（QSqlDatabase 运行时按需加载；控制台程序不加载平台插件，无需 qwindows）
    get_filename_component(QT_PLUGINS "${Qt6Core_DIR}/../../../plugins" ABSOLUTE)
    deploy_to(${PROJECT_NAME} plugins/sqldrivers
            "${QT_PLUGINS}/sqldrivers/qsqlite.dll"
            "${QT_PLUGINS}/sqldrivers/qsqlited.dll")
endif ()
