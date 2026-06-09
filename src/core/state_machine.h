/**
 * @file state_machine.h
 * @brief CapsLock 状态机
 *
 * 策略（Interception 驱动方案）：
 *   - CapsLock DOWN：suppress（不 send），OS 从未收到，LED 不切换
 *   - CapsLock UP（短按）：suppress + 注入 CapsLock DOWN+UP，OS 切换大小写
 *   - CapsLock UP（长按/修饰）：suppress，不注入任何事件
 *
 * Why: Interception 驱动位于 kbdclass 之下，suppress CapsLock DOWN 时
 *      OS 从未收到事件，LED 不意外切换，从根本上消除了 WH_KEYBOARD_LL
 *      方案中硬件 LED 闪烁和短按后长按误切换的 bug。
 *      不再需要 GetAsyncKeyState 检测和 SimulateCapsLockToggle 撤销逻辑。
 */

#ifndef CAPSX_CORE_STATE_MACHINE_H_
#define CAPSX_CORE_STATE_MACHINE_H_

#include <Windows.h>
#include <chrono>
#include <set>

// Interception 设备类型前置声明
typedef int InterceptionDevice;

// 处理决策：Interception 钩子根据此决策决定 suppress / send / inject
enum class ProcessDecisionAction
{
    Suppress,             // 拦截：不调用 interception_send
    PassThrough,          // 放行：调用 interception_send 发送原始事件
    InjectCapsLockToggle, // 注入 CapsLock DOWN+UP（短按行为）
    InjectKey             // 注入绑定目标按键 DOWN+UP
};

struct ProcessDecision
{
    ProcessDecisionAction action = ProcessDecisionAction::Suppress;
    WORD targetVk = 0;            // InjectKey 时的目标键 VK code
    bool withShift = false;       // InjectKey 时是否附加 Shift
    InterceptionDevice device = 0; // 注入时的目标设备
};

enum class CapsLockState
{
    Idle,
    Pressing,    // CapsLock 持按中，OS 未收到 DOWN，CapsLock 未被切换
    Modifier     // CapsLock 持按 + 其他键按下，CapsLock 未被切换
};

class StateMachine
{
public:
    /**
     * @brief 处理按键事件，返回拦截/放行/注入决策
     * @param wParam 按键事件类型（WM_KEYDOWN / WM_KEYUP）
     * @param vkCode 虚拟键码
     * @param device Interception 键盘设备
     * @param isExtended 是否为扩展键（E0 前缀）
     * @return ProcessDecision 处理决策
     */
    ProcessDecision ProcessKeyEvent(WPARAM wParam, DWORD vkCode,
        InterceptionDevice device, bool isExtended);

    void SetEnabled(bool enabled);
    CapsLockState GetState() const;
    void SetThreshold(int thresholdMs);

private:
    CapsLockState m_state = CapsLockState::Idle;    // 当前状态机状态
    bool m_enabled = true;                          // CapsX 是否启用
    int m_thresholdMs = 150;                        // 长按判定阈值（毫秒）

    std::chrono::steady_clock::time_point m_pressStartTime;  // CapsLock DOWN 时刻
    bool m_comboKeyPressed = false;                         // 是否有组合键按下

    // 无绑定按键追踪集合
    // Why: 组合键模式下，有绑定的按键拦截并转译，无绑定的按键需要放行。
    //      放行 DOWN 后必须同步放行 UP，否则 OS 认为按键仍处于按下状态。
    //      通过此集合记录哪些键的 DOWN 已放行，确保对应 UP 也放行。
    std::set<DWORD> m_passedThroughKeys;

    bool IsModifierKey(DWORD vkCode) const;
    bool IsCapsLockKey(DWORD vkCode) const;
};

extern StateMachine g_stateMachine;

#endif // CAPSX_CORE_STATE_MACHINE_H_