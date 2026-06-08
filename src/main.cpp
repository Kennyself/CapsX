/**
 * @file main.cpp
 * @brief CapsX 程序入口
 *
 * 初始化日志、键盘钩子、状态机和托盘图标，
 * 然后运行消息循环等待钩子回调和托盘事件。
 */

#include "core/keyboard_hook.h"
#include "core/state_machine.h"
#include "core/input_simulator.h"
#include "core/binding_manager.h"
#include "core/single_instance.h"
#include "ui/tray_icon.h"
#include "utils/logger.h"

#include <Windows.h>

// 全局退出标志
bool g_running = true;

// 全局状态机实例（供 tray_icon.cpp 引用）
StateMachine g_stateMachine;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    // 抑制未使用参数警告
    (void)hInstance; (void)hPrevInstance; (void)pCmdLine; (void)nCmdShow;

    // 初始化日志（单实例检测需要记录日志）
    if (!g_logger.Init())
    {
        OutputDebugStringW(L"CapsX: Logger init failed\n");
    }

    // 单实例检测：拒绝重复启动
    // Why: 全局键盘钩子多实例同时运行会导致钩子冲突，
    //      必须在任何初始化之前检测，避免浪费资源
    if (!g_singleInstance.TryAcquire())
    {
        MessageBoxW(nullptr, L"CapsX 已在运行，无法启动第二个实例。", L"CapsX", MB_OK | MB_ICONWARNING);
        g_logger.Shutdown();
        return 1;
    }

    g_logger.LogInfo("CapsX v1.0.0 starting");

    // 初始化期望的 CapsLock 状态
    // Why: 需要跟踪 CapsLock 开关状态，以便在 CapsLock DOWN 时
    //      检测硬件/驱动是否意外切换了 CapsLock 状态
    SHORT initialCapsLockState = GetAsyncKeyState(VK_CAPITAL);
    bool initialCapsLockOn = (initialCapsLockState & 0x0001) != 0;
    g_stateMachine.InitExpectedState(initialCapsLockOn);

    // 创建托盘图标
    if (!g_trayIcon.Create())
    {
        g_logger.LogError("Tray icon creation failed, exiting");
        g_logger.Shutdown();
        return 1;
    }

    // 安装键盘钩子
    KeyboardHook keyboardHook;
    keyboardHook.SetCallback([](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT
    {
        (void)nCode;
        return g_stateMachine.ProcessKeyEvent(wParam, lParam);
    });

    if (!keyboardHook.Install())
    {
        g_logger.LogError("Keyboard hook install failed, exiting");
        g_trayIcon.Remove();
        g_logger.Shutdown();
        return 1;
    }

    g_logger.LogInfo("CapsX started, entering message loop");

    // 运行消息循环
    MSG msg = {};
    while (g_running && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理：卸载钩子和移除托盘图标
    keyboardHook.Uninstall();
    g_trayIcon.Remove();
    g_singleInstance.Release();
    g_logger.LogInfo("CapsX exited normally");
    g_logger.Shutdown();

    return 0;
}