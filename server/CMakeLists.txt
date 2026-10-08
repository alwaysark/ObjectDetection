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
    foreach (QT_LIB Core Network Sql)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::${QT_LIB}>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach ()
endif ()
