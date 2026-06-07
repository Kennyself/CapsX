/**
 * @file keyboard_hook.cpp
 * @brief WH_KEYBOARD_LL 全局低级键盘钩子实现
 */

#include "core/keyboard_hook.h"
#include "utils/logger.h"

// 全局指针，供静态回调函数访问当前实例
// Why: LowLevelKeyboardProc 是静态函数，无法直接访问类成员，
//      通过全局指针间接访问回调函数和钩子句柄
static KeyboardHook* g_hookInstance = nullptr;

bool KeyboardHook::Install()
{
    if (m_hook != nullptr)
    {
        g_logger.LogWarning("Keyboard hook already installed, skipping");
        return true;
    }

    g_hookInstance = this;
    m_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandleW(NULL),
        0
    );

    if (m_hook == nullptr)
    {
        g_logger.LogError("SetWindowsHookEx failed, error: %lu", GetLastError());
        g_hookInstance = nullptr;
        return false;
    }

    g_logger.LogInfo("Keyboard hook installed");
    return true;
}

void KeyboardHook::Uninstall()
{
    if (m_hook != nullptr)
    {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        g_hookInstance = nullptr;
        g_logger.LogInfo("Keyboard hook uninstalled");
    }
}

bool KeyboardHook::IsInstalled() const
{
    return m_hook != nullptr;
}

void KeyboardHook::SetCallback(KeyCallback cb)
{
    m_callback = std::move(cb);
}

LRESULT CALLBACK KeyboardHook::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_hookInstance != nullptr)
    {
        const auto& callback = g_hookInstance->m_callback;
        if (callback)
        {
            LRESULT result = callback(nCode, wParam, lParam);
            if (result != 0)
            {
                return result;
            }
        }
    }

    // 放行：传递给下一个钩子或目标窗口
    HHOOK hook = (g_hookInstance != nullptr) ? g_hookInstance->m_hook : nullptr;
    return CallNextHookEx(hook, nCode, wParam, lParam);
}