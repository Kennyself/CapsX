/**
 * @file state_machine.cpp
 * @brief CapsLock 状态机实现
 *
 * 策略：
 *   - DOWN：拦截 + 检测/撤销硬件意外 LED 切换
 *   - UP（短按）：放行 + 模拟切换（LED 仅切换一次）
 *   - UP（长按）：拦截（不需要切换）
 */

#include "core/state_machine.h"
#include "core/input_simulator.h"
#include "core/binding_manager.h"
#include "utils/logger.h"

LRESULT StateMachine::ProcessKeyEvent(WPARAM wParam, LPARAM lParam)
{
    if (!m_enabled)
    {
        return 0;
    }

    KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    // 忽略注入事件（SendInput/keybd_event 产生的），防止重入
    if (kb->flags & LLKHF_INJECTED)
    {
        return 0;
    }

    DWORD vkCode = kb->vkCode;

    // ─── CapsLock 按键处理 ───
    if (IsCapsLockKey(vkCode))
    {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (m_state == CapsLockState::Idle)
            {
                // 检测硬件/驱动是否意外切换了 CapsLock 状态
                // Why: 某些键盘硬件在物理 CapsLock DOWN 时会切换 LED，
                //      即使钩子拦截了 DOWN 事件。拦截 UP 后 OS 无法纠正，
                //      导致长按期间 LED 处于错误状态，产生闪烁。
                //      通过 GetAsyncKeyState 检测实际状态是否与期望状态不同，
                //      如果不同则立即模拟一次 CapsLock 切换来撤销，
                //      撤销在 < 5ms 内完成，闪烁不可感知。
                SHORT keyState = GetAsyncKeyState(VK_CAPITAL);
                bool actualCapsLockOn = (keyState & 0x0001) != 0;

                if (actualCapsLockOn != m_expectedCapsLockOn)
                {
                    // 硬件/驱动已意外切换 CapsLock，立即撤销
                    g_inputSimulator.SimulateCapsLockToggle();
                    g_logger.LogDebug("CapsLock DOWN: hardware toggle detected, undo applied (expected=%d, actual=%d)",
                        m_expectedCapsLockOn, actualCapsLockOn);
                }

                m_pressStartTime = std::chrono::steady_clock::now();
                m_comboKeyPressed = false;
                m_state = CapsLockState::Pressing;
                g_logger.LogDebug("CapsLock DOWN intercepted, Pressing");
            }
            return 1;  // 拦截 DOWN
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_pressStartTime
            ).count();

            bool isShortPress = (m_state != CapsLockState::Modifier)
                && !m_comboKeyPressed
                && elapsedMs < m_thresholdMs;

            if (isShortPress)
            {
                // 短按：模拟 CapsLock 切换，然后放行 UP
                // Why: 模拟事件先于物理 UP 被 OS 处理，
                //      LED 仅更新一次，无闪烁。
                //      放行 UP 让 OS 收到完整按键周期，
                //      避免 OS 认为按键仍处于按下状态。
                g_inputSimulator.SimulateCapsLockToggle();
                m_expectedCapsLockOn = !m_expectedCapsLockOn;
                g_logger.LogDebug("CapsLock UP: short press toggle + pass through (elapsed=%lldms)", elapsedMs);

                m_state = CapsLockState::Idle;
                return 0;  // 放行 UP
            }
            else
            {
                // 长按或组合键：不需要切换，拦截 UP
                // Why: CapsLock 从未被 OS 切换（DOWN 拦截 + 硬件撤销已完成），
                //      不需要恢复操作。拦截 UP 防止 OS 收到不匹配的 UP 事件
                //      触发 LED 状态纠正导致闪烁。
                g_logger.LogDebug("CapsLock UP: long/combo, suppress (elapsed=%lldms)", elapsedMs);

                m_state = CapsLockState::Idle;
                return 1;  // 拦截 UP
            }
        }
    }

    // ─── 修饰键放行 ───
    if (IsModifierKey(vkCode))
    {
        if (m_state == CapsLockState::Pressing &&
            (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
        {
            m_comboKeyPressed = true;
        }
        return 0;
    }

    // ─── CapsLock 持按期间，处理其他按键 ───
    if (m_state == CapsLockState::Pressing || m_state == CapsLockState::Modifier)
    {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (m_state == CapsLockState::Pressing)
            {
                m_comboKeyPressed = true;
                m_state = CapsLockState::Modifier;
                g_logger.LogDebug("Entering modifier mode");
            }

            const KeyBinding* binding = g_bindingManager.FindBinding(vkCode);
            if (binding != nullptr && binding->enabled)
            {
                g_inputSimulator.SimulateKey(static_cast<WORD>(binding->targetVk), binding->withShift);
                return 1;
            }
            return 1;
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            return 1;
        }
    }

    return 0;
}

void StateMachine::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
    {
        m_state = CapsLockState::Idle;
        m_comboKeyPressed = false;
        g_logger.LogInfo("CapsX disabled");
    }
    else
    {
        g_logger.LogInfo("CapsX enabled");
    }
}

CapsLockState StateMachine::GetState() const
{
    return m_state;
}

void StateMachine::SetThreshold(int thresholdMs)
{
    m_thresholdMs = thresholdMs;
}

void StateMachine::InitExpectedState(bool capsLockOn)
{
    m_expectedCapsLockOn = capsLockOn;
    g_logger.LogInfo("Expected CapsLock state initialized: %s", capsLockOn ? "ON" : "OFF");
}

bool StateMachine::IsModifierKey(DWORD vkCode) const
{
    switch (vkCode)
    {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

bool StateMachine::IsCapsLockKey(DWORD vkCode) const
{
    return vkCode == VK_CAPITAL;
}