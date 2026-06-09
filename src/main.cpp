/**
 * @file main.cpp
 * @brief CapsX 程序入口
 *
 * 初始化日志、Interception 驱动拦截、状态机和托盘图标，
 * 然后运行消息循环等待托盘事件。
 * Interception 接收线程在独立线程中运行。
 */

#include "core/interception_hook.h"
#include "core/state_machine.h"
#include "core/binding_manager.h"
#include "core/single_instance.h"
#include "ui/tray_icon.h"
#include "utils/logger.h"

#include <Windows.h>

// 全局退出标志
bool g_running = true;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    // 抑制未使用参数警告
    (void)hInstance; (void)hPrevInstance; (void)pCmdLine; (void)nCmdShow;

    // 初始化日志
    if (!g_logger.Init())
    {
        OutputDebugStringW(L"CapsX: Logger init failed\n");
    }

    // 单实例检测：拒绝重复启动
    // Why: Interception 驱动不支持多客户端同时拦截同一设备，
    //      多实例运行会导致按键事件处理冲突
    if (!g_singleInstance.TryAcquire())
    {
        MessageBoxW(nullptr, L"CapsX 已在运行，无法启动第二个实例。", L"CapsX", MB_OK | MB_ICONWARNING);
        g_logger.Shutdown();
        return 1;
    }

    g_logger.LogInfo("CapsX v1.0.0 starting");

    // 检测 Interception 驱动是否已安装
    // Why: Interception 驱动需要用户手动安装（管理员权限 + 重启），
    //      未安装时 CapsX 无法工作，必须提示用户
    if (!InterceptionHook::IsDriverAvailable())
    {
        // 获取 CapsX.exe 所在目录，拼接安装工具路径
        // Why: 让用户直接看到安装工具的位置，不用自己去找
        WCHAR exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        WCHAR installHint[MAX_PATH * 2] = {};
        swprintf_s(installHint,
            L"Interception 驱动未安装，CapsX 无法运行。\n\n"
            L"请按以下步骤安装驱动：\n\n"
            L"步骤一：打开管理员 CMD 或 PowerShell\n"
            L"  搜索 cmd → 右键「以管理员身份运行」\n\n"
            L"步骤二：运行安装命令\n"
            L"  cd /d \"%s\"\n"
            L"  install-interception.exe /install\n\n"
            L"步骤三：启用测试模式（Win10/11 必须）\n"
            L"  bcdedit /set testsigning on\n\n"
            L"步骤四：重启计算机\n"
            L"  shutdown /r /t 5\n\n"
            L"说明：\n"
            L"  • 安装工具 install-interception.exe 位于 CapsX 同目录下\n"
            L"  • 测试模式启用后桌面右下角会显示水印，属正常现象\n"
            L"  • 将 CapsX.exe 和 install-interception.exe 一起复制即可在新电脑使用",
            exePath);
        MessageBoxW(nullptr, installHint, L"CapsX — 驱动未安装", MB_OK | MB_ICONERROR);
        g_logger.LogError("Interception driver not available, exiting");
        g_singleInstance.Release();
        g_logger.Shutdown();
        return 1;
    }

    // 创建托盘图标
    if (!g_trayIcon.Create())
    {
        g_logger.LogError("Tray icon creation failed, exiting");
        g_singleInstance.Release();
        g_logger.Shutdown();
        return 1;
    }

    // 安装 Interception 拦截
    if (!g_interceptionHook.Install())
    {
        g_logger.LogError("Interception hook install failed, exiting");
        g_trayIcon.Remove();
        g_singleInstance.Release();
        g_logger.Shutdown();
        return 1;
    }

    g_logger.LogInfo("CapsX started, entering message loop");

    // 运行消息循环（主线程处理托盘图标回调）
    // Why: Interception 接收线程独立运行，主线程只需处理托盘事件
    MSG msg = {};
    while (g_running && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理：卸载拦截和移除托盘图标
    g_interceptionHook.Uninstall();
    g_trayIcon.Remove();
    g_singleInstance.Release();
    g_logger.LogInfo("CapsX exited normally");
    g_logger.Shutdown();

    return 0;
}