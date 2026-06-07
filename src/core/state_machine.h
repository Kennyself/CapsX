/**
 * @file state_machine.h
 * @brief CapsLock 状态机
 *
 * 策略：
 *   - CapsLock DOWN：拦截（阻止 OS 自动切换），同时检测并撤销
 *     硬件/驱动可能产生的意外 LED 切换，消除长按起始闪烁
 *   - CapsLock UP（短按）：放行，在放行前模拟 CapsLock 切换，
 *     模拟事件先于物理 UP 被 OS 处理，LED 仅切换一次
 *   - CapsLock UP（长按/修饰）：拦截，不需要切换
 *
 * Why: 长按起始闪烁的根因：某些键盘硬件/驱动在物理 CapsLock DOWN
 *      时会切换 LED 状态，即使钩子拦截了 DOWN 事件。
 *      长按时 UP 也被拦截，OS 无法纠正这个意外 LED 切换，
 *      导致 LED 在长按期间处于错误状态，产生可见闪烁。
 *      修复方法：在 DOWN 时通过 GetAsyncKeyState 检测是否发生了
 *      意外切换，如果检测到则立即模拟一次 CapsLock 切换来撤销，
 *      撤销发生在 < 5ms 内，闪烁不可感知。
 */

#ifndef CAPSX_CORE_STATE_MACHINE_H_
#define CAPSX_CORE_STATE_MACHINE_H_

#include <Windows.h>
#include <chrono>

enum class CapsLockState
{
    Idle,
    Pressing,    // CapsLock 持按中，OS 未收到 DOWN，CapsLock 未被切换
    Modifier     // CapsLock 持按 + 其他键按下，CapsLock 未被切换
};

class StateMachine
{
public:
    LRESULT ProcessKeyEvent(WPARAM wParam, LPARAM lParam);
    void SetEnabled(bool enabled);
    CapsLockState GetState() const;
    void SetThreshold(int thresholdMs);

    /**
     * @brief 初始化期望的 CapsLock 状态
     * @param capsLockOn 当前 CapsLock 是否为 ON
     */
    void InitExpectedState(bool capsLockOn);

private:
    CapsLockState m_state = CapsLockState::Idle;    // 当前状态机状态
    bool m_enabled = true;                          // CapsX 是否启用
    int m_thresholdMs = 300;                        // 长按判定阈值（毫秒）
    bool m_expectedCapsLockOn = false;              // 期望的 CapsLock 开关状态

    std::chrono::steady_clock::time_point m_pressStartTime;  // CapsLock DOWN 时刻
    bool m_comboKeyPressed = false;                         // 是否有组合键按下

    bool IsModifierKey(DWORD vkCode) const;
    bool IsCapsLockKey(DWORD vkCode) const;
};

extern StateMachine g_stateMachine;

#endif // CAPSX_CORE_STATE_MACHINE_H_