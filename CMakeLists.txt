cmake_minimum_required(VERSION 3.20)
project(CapsX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: UTF-8 source/execution charset, suppress CRT deprecation warnings
if (MSVC)
    add_compile_options(/utf-8 /W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# MinGW: 启用 Unicode 入口点（wWinMain），静态链接运行时库以消除 DLL 依赖
if (MINGW)
    add_link_options(-municode -static -static-libgcc -static-libstdc++)
endif()

# 源文件
add_executable(CapsX WIN32
    src/main.cpp
    src/core/keyboard_hook.cpp
    src/core/state_machine.cpp
    src/core/input_simulator.cpp
    src/core/binding_manager.cpp
    src/ui/tray_icon.cpp
    src/ui/tray_icon_drawer.cpp
    src/utils/logger.cpp
    src/utils/string_utils.cpp
)

target_include_directories(CapsX PRIVATE
    src/
    src/third_party
)

# Win32 链接
target_link_libraries(CapsX PRIVATE
    comctl32   # ListView、Spin 控件
    shell32    # Shell_NotifyIcon
)