cmake_minimum_required(VERSION 3.20)
project(CapsX VERSION 1.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: UTF-8 source/execution charset, suppress CRT deprecation warnings
if (MSVC)
    add_compile_options(/utf-8 /W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# MinGW: 启用 Unicode 入口点（wWinMain），静态链接运行时库以消除 DLL 依赖。
# 源文件是 UTF-8，否则中文提示会按系统代码页解析错。
if (MINGW)
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
    add_link_options(-municode -static -static-libgcc -static-libstdc++)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
endif()

# Interception 静态链接定义
# Why: 定义 INTERCEPTION_STATIC 后，Interception API 函数直接编译进 CapsX.exe，
#      不需要分发 interception.dll，简化部署
add_compile_definitions(INTERCEPTION_STATIC)

# 官方安装器会作为 RCDATA 打进 CapsX.exe，构建前必须存在
set(INTERCEPTION_INSTALLER "${CMAKE_SOURCE_DIR}/src/third_party/interception/install-interception.exe")
if(NOT EXISTS "${INTERCEPTION_INSTALLER}")
    message(FATAL_ERROR "缺少 Interception 安装器: ${INTERCEPTION_INSTALLER}")
endif()

# 源文件
add_executable(CapsX WIN32
    src/main.cpp
    src/core/interception_hook.cpp
    src/core/state_machine.cpp
    src/core/binding_manager.cpp
    src/core/single_instance.cpp
    src/setup/driver_setup.cpp
    src/ui/tray_icon.cpp
    src/ui/tray_icon_drawer.cpp
    src/utils/logger.cpp
    src/utils/auto_start.cpp
    src/utils/string_utils.cpp
    # Interception 用户态库源码直接编译进 CapsX.exe
    # Why: INTERCEPTION_STATIC 模式下，API 函数编译为普通函数而非 DLL 导入，
    #      分发时只需 CapsX.exe 一个文件，不需要 interception.dll
    src/third_party/interception/library/interception.c
    # Windows 资源文件，包含 exe 文件图标
    res/app.rc
)

target_include_directories(CapsX PRIVATE
    src/
    src/third_party
    src/third_party/interception/library
)

# 安装器二进制变更后需要重新编译资源
set_source_files_properties(res/app.rc PROPERTIES
    OBJECT_DEPENDS "${INTERCEPTION_INSTALLER}")

# Win32 链接（不再需要 interception.lib，已通过源码编译）
target_link_libraries(CapsX PRIVATE
    comctl32   # ListView、Spin 控件
    shell32    # Shell_NotifyIcon
    advapi32   # 当前用户的开机自启注册表
)