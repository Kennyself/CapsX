/**
 * @file input_simulator.cpp
 * @brief SendInput API 封装实现
 */

#include "core/input_simulator.h"
#include "utils/logger.h"

InputSimulator g_inputSimulator;

void InputSimulator::SimulateKey(WORD vkCode, bool withShift)
{
    INPUT inputs[4] = {};
    int count = 0;

    // 如果需要 Shift，先发送 Shift DOWN
    if (withShift)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        count++;
    }

    // 发送目标键 DOWN
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vkCode;
    count++;

    // 发送目标键 UP
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vkCode;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    count++;

    // 如果需要 Shift，发送 Shift UP
    if (withShift)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    UINT sent = SendInput(count, inputs, sizeof(INPUT));
    if (sent != static_cast<UINT>(count))
    {
        g_logger.LogWarning("SendInput incomplete: expected %d, sent %u", count, sent);
    }
}

void InputSimulator::SimulateCapsLockToggle()
{
    // 使用两次独立的 SendInput 调用发送 CapsLock DOWN 和 UP
    // Why: 与参考项目 CapslockX 的 ToggleCapsLock 实现方式完全一致。
    //      参考项目使用 SendKeyDown + SendKeyUp 两次独立调用，
    //      而不是一次性发送 DOWN+UP 两个 INPUT 结构。
    //      之前尝试 keybd_event 和合并的 SendInput(2, ...) 均出现闪烁，
    //      回归参考项目的两次独立 SendInput 方案。

    INPUT inputDown = {};
    inputDown.type = INPUT_KEYBOARD;
    inputDown.ki.wVk = VK_CAPITAL;

    INPUT inputUp = {};
    inputUp.type = INPUT_KEYBOARD;
    inputUp.ki.wVk = VK_CAPITAL;
    inputUp.ki.dwFlags = KEYEVENTF_KEYUP;

    UINT sentDown = SendInput(1, &inputDown, sizeof(INPUT));
    UINT sentUp = SendInput(1, &inputUp, sizeof(INPUT));

    if (sentDown != 1 || sentUp != 1)
    {
        g_logger.LogWarning("CapsLock toggle SendInput incomplete: DOWN=%u, UP=%u", sentDown, sentUp);
    }
    else
    {
        g_logger.LogDebug("CapsLock toggle simulated via SendInput (separate calls)");
    }
}