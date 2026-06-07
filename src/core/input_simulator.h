/**
 * @file input_simulator.h
 * @brief SendInput API 封装
 *
 * 提供模拟按键输入的功能，用于：
 *   - 短按 CapsLock 时模拟一次 CapsLock 切换
 *   - 组合键模式下模拟方向键、功能键等目标按键
 *
 * CapsLock 切换使用两次独立的 SendInput 调用（DOWN 和 UP 分别发送），
 * 与参考项目 CapslockX 的 ToggleCapsLock 实现方式一致。
 * 所有模拟输入均带有 LLKHF_INJECTED 标志，
 * 钩子回调检测此标志后自动忽略，防止重入。
 */

#ifndef CAPSX_CORE_INPUT_SIMULATOR_H_
#define CAPSX_CORE_INPUT_SIMULATOR_H_

#include <Windows.h>

class InputSimulator
{
public:
    /**
     * @brief 模拟单键按下+释放
     * @param vkCode 目标键的虚拟键码
     * @param withShift 是否同时模拟 Shift 键（用于文本选择等）
     */
    void SimulateKey(WORD vkCode, bool withShift = false);

    /**
     * @brief 模拟一次 CapsLock 切换（短按行为）
     *
     * 使用两次独立的 SendInput 调用发送 CapsLock DOWN 和 UP，
     * 与参考项目 CapslockX 的实现方式一致。
     * Why: 之前尝试 keybd_event 和 PostThreadMessage 延迟调用，
     *      均未解决闪烁问题。回归参考项目的 SendInput 方案，
     *      在钩子回调中直接调用，与参考项目行为完全一致。
     */
    void SimulateCapsLockToggle();
};

// 全局实例，供状态机使用
extern InputSimulator g_inputSimulator;

#endif // CAPSX_CORE_INPUT_SIMULATOR_H_