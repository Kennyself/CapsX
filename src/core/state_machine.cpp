/**
 * @file state_machine.cpp
 * @brief CapsLock 状态机实现（Interception 驱动方案）
 *
 * 策略：
 *   - DOWN：suppress（OS 不收到，LED 不切换）
 *   - UP（短按）：suppress + 注入 CapsLock toggle
 *   - UP（长按/修饰）：suppress
 */

#include "core/state_machine.h"
#include "core/binding_manager.h"
#include "utils/logger.h"

StateMachine g_stateMachine;

ProcessDecision StateMachine::ProcessKeyEvent(WPARAM wParam, DWORD vkCode,
    InterceptionDevice device, bool isExtended)
{
    if (!m_enabled)
    {
        // 禁用状态：所有按键放行
        return {ProcessDecisionAction::PassThrough, 0, false, device};
    }

    // ─── CapsLock 按键处理 ───
    if (IsCapsLockKey(vkCode))
    {
        if (wParam == WM_KEYDOWN)
        {
            if (m_state == CapsLockState::Idle)
            {
                // CapsLock DOWN：suppress，开始计时
                // Why: Interception 在 kbdclass 之下拦截，
                //      OS 从未收到 DOWN，LED 不切换，无需撤销逻辑
                m_pressStartTime = std::chrono::steady_clock::now();
                m_comboKeyPressed = false;
                m_state = CapsLockState::Pressing;
                g_logger.LogInfo("CapsLock DOWN suppressed, Pressing");
            }
            return {ProcessDecisionAction::Suppress, 0, false, device};
        }
        else if (wParam == WM_KEYUP)
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_pressStartTime
            ).count();

            bool isShortPress = (m_state != CapsLockState::Modifier)
                && !m_comboKeyPressed
                && elapsedMs < m_thresholdMs;

            if (isShortPress)
            {
                // 短按：suppress 物理 UP，注入 CapsLock toggle
                // Why: 物理 UP 被 suppress，OS 不收到。
                //      注入 CapsLock DOWN+UP 让 kbdclass 正常切换一次大小写，
                //      LED 由 kbdclass 通过 IOCTL 更新。
                m_passedThroughKeys.clear();
                m_state = CapsLockState::Idle;
                g_logger.LogInfo("CapsLock UP: short press, inject toggle (elapsed=%lldms)", elapsedMs);
                return {ProcessDecisionAction::InjectCapsLockToggle, 0, false, device};
            }
            else
            {
                // 长按或组合键：suppress，不注入任何事件
                // Why: CapsLock 从未被 OS 切换（DOWN 已 suppress），
                //      不需要任何恢复操作。
                m_passedThroughKeys.clear();
                m_state = CapsLockState::Idle;
                g_logger.LogInfo("CapsLock UP: long/combo, suppress (elapsed=%lldms)", elapsedMs);
                return {ProcessDecisionAction::Suppress, 0, false, device};
            }
        }
    }

    // ─── 修饰键放行 ───
    if (IsModifierKey(vkCode))
    {
        if (m_state == CapsLockState::Pressing && wParam == WM_KEYDOWN)
        {
            m_comboKeyPressed = true;
        }
        return {ProcessDecisionAction::PassThrough, 0, false, device};
    }

    // ─── CapsLock 持按期间，处理其他按键 ───
    if (m_state == CapsLockState::Pressing || m_state == CapsLockState::Modifier)
    {
        if (wParam == WM_KEYDOWN)
        {
            if (m_state == CapsLockState::Pressing)
            {
                m_comboKeyPressed = true;
                m_state = CapsLockState::Modifier;
                g_logger.LogInfo("Entering modifier mode");
            }

            const KeyBinding* binding = g_bindingManager.FindBinding(vkCode);
            if (binding != nullptr && binding->enabled)
            {
                // 有绑定的按键：suppress + 注入目标键
                return {ProcessDecisionAction::InjectKey,
                    static_cast<WORD>(binding->targetVk), binding->withShift, device};
            }

            // 无绑定的按键：放行，记录以便 UP 也放行
            m_passedThroughKeys.insert(vkCode);
            g_logger.LogInfo("Key without binding passed through: vk=%u", vkCode);
            return {ProcessDecisionAction::PassThrough, 0, false, device};
        }
        else if (wParam == WM_KEYUP)
        {
            // 无绑定的按键 UP 需要放行，有绑定的按键 UP 继续拦截
            if (m_passedThroughKeys.count(vkCode))
            {
                m_passedThroughKeys.erase(vkCode);
                return {ProcessDecisionAction::PassThrough, 0, false, device};
            }
            return {ProcessDecisionAction::Suppress, 0, false, device};
        }
    }

    // ─── 非 CapsLock 持按状态：所有按键放行 ───
    return {ProcessDecisionAction::PassThrough, 0, false, device};
}

void StateMachine::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
    {
        m_state = CapsLockState::Idle;
        m_comboKeyPressed = false;
        m_passedThroughKeys.clear();
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