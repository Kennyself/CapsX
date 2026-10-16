/**
 * @file main.cpp
 * @brief CapsX 程序入口
 *
 * 初始化日志、必要时自动安装 Interception 驱动、状态机和托盘图标，
 * 然后运行消息循环等待托盘事件。
 * Interception 接收线程在独立线程中运行。
 */

#include "core/interception_hook.h"
#include "core/state_machine.h"
#include "core/binding_manager.h"
#include "core/single_instance.h"
#include "setup/driver_setup.h"
#include "ui/tray_icon.h"
#include "utils/logger.h"

#include <Windows.h>

// 全局退出标志
bool g_running = true;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    // 抑制未使用参数警告
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    // 初始化日志
    if (!g_logger.Init())
    {
        OutputDebugStringW(L"CapsX: Logger init failed\n");
    }

    // 自检只确认安装器已经打进 exe，不碰驱动，也不占用单实例锁
    if (DriverSetup::IsVerifySwitch(pCmdLine))
    {
        const bool verified = DriverSetup::VerifyEmbeddedInstaller();
        g_logger.Shutdown();
        return verified ? 0 : 1;
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

    // 驱动未就绪时自动安装。复制一个 CapsX.exe 即可，不再要求手敲命令。
    // Why: 内核驱动无法在不重启的情况下生效。安全启动不能由程序关闭，
    //      所以只在重启后仍然加载失败时才提示用户自己处理。
    if (InterceptionHook::IsDriverAvailable())
    {
        DriverSetup::ClearPendingState();
    }
    else
    {
        const SetupAction action = DriverSetup::Prepare(pCmdLine);
        if (action == SetupAction::RelaunchInstall)
        {
            // 先放开单实例锁，提权后的新进程才能启动
            g_singleInstance.Release();
            if (!DriverSetup::RelaunchElevated(L"--install-driver"))
            {
                MessageBoxW(nullptr,
                    L"没有获得管理员权限，CapsX 没有修改系统设置。",
                    L"CapsX",
                    MB_OK | MB_ICONWARNING);
                g_logger.Shutdown();
                return 1;
            }

            g_logger.Shutdown();
            return 0;
        }

        g_singleInstance.Release();
        g_logger.Shutdown();
        return action == SetupAction::ExitOk ? 0 : 1;
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