cmake_minimum_required(VERSION 3.16)
project(AlarmPlatformServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# 与 client 用同一套 Qt（路径在顶层 CMakeLists 统一指定）
set(Qt6_DIR "D:/ProgramFiles/Qt/6.8.3/msvc2022_64/lib/cmake/Qt6" CACHE PATH "" FORCE)
list(APPEND CMAKE_PREFIX_PATH "D:/ProgramFiles/Qt/6.8.3/msvc2022_64")

find_package(Qt6 COMPONENTS Core Network Sql REQUIRED)

add_executable(AlarmPlatformServer main.cpp)
target_link_libraries(AlarmPlatformServer Qt6::Core Qt6::Network Qt6::Sql)

if (WIN32)
    # Qt 运行库 DLL
    foreach (QT_LIB Core Network Sql)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::${QT_LIB}>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()

    # 运行库 DLL（脱离开发环境运行必需；Debug 还要调试版 CRT）
    set(QT_PLUGINS_DIR "${Qt6Core_DIR}/../../../plugins")
    file(GLOB CRT_DLLS
            "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT/*.dll")
    foreach (CRT_DLL ${CRT_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CRT_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()
    file(GLOB CRTD_DLLS
            "D:/ProgramFiles/VisualStudio/MicrosoftVisualStudio18/Community/VC/Redist/MSVC/*/x64/Microsoft.VC*.DebugCRT/*.dll")
    foreach (CRT_DLL ${CRTD_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CRT_DLL}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()
    if (EXISTS "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "D:/Windows Kits/10/bin/10.0.28000.0/x64/ucrt/ucrtbased.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endif ()

    # SQLite 数据库驱动插件（QSQLITE 运行时按需加载）+ 平台插件（qwindows）
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/sqldrivers/")
    foreach (SQL_NAME qsqlite.dll qsqlited.dll)
        if (EXISTS "${QT_PLUGINS_DIR}/sqldrivers/${SQL_NAME}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_PLUGINS_DIR}/sqldrivers/${SQL_NAME}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/sqldrivers/")
        endif ()
    endforeach ()
    foreach (QW_NAME qwindows.dll qwindowsd.dll)
        if (EXISTS "${QT_PLUGINS_DIR}/platforms/${QW_NAME}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_PLUGINS_DIR}/platforms/${QW_NAME}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        endif ()
    endforeach ()
endif ()
