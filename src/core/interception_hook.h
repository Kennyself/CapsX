/**
 * @file interception_hook.h
 * @brief Interception 驱动键盘拦截封装
 *
 * 使用 Interception 驱动（kbdclass 之下的 filter driver）拦截键盘事件，
 * 替代 WH_KEYBOARD_LL 钩子方案。
 *
 * Why: WH_KEYBOARD_LL 在 OS 键盘驱动之后才拦截按键，无法阻止固件级
 *      CapsLock LED 切换，导致短按后长按误切换 bug。
 *      Interception 驱动位于 kbdclass 之下，拦截 CapsLock DOWN 时
 *      OS 从未收到事件，LED 不会切换，从根本上解决问题。
 *
 * 线程模型：
 *   - 主线程：Windows 消息循环（托盘图标回调）
 *   - Interception 线程：阻塞式 receive 循环
 *     interception_wait → interception_receive → StateMachine → send/suppress
 */

#ifndef CAPSX_CORE_INTERCEPTION_HOOK_H_
#define CAPSX_CORE_INTERCEPTION_HOOK_H_

#include <Windows.h>
#include <thread>
#include <atomic>
#include <functional>

// Interception API 头文件
// Why: 直接 include 以获取完整类型定义，避免前置声明与 typedef 冲突
// INTERCEPTION_STATIC 已在 CMakeLists.txt 中定义，此处用条件宏避免重复
#ifndef INTERCEPTION_STATIC
#define INTERCEPTION_STATIC
#endif
#include "interception.h"

class InterceptionHook
{
public:
    /**
     * @brief 安装 Interception 拦截（创建 context + 启动接收线程）
     * @return true 安装成功，false 驱动未安装或创建失败
     */
    bool Install();

    /**
     * @brief 卸载 Interception 拦截（停止接收线程 + 销毁 context）
     */
    void Uninstall();

    /**
     * @brief 通过 Interception 注入 CapsLock 切换（短按行为）
     * @param device 目标键盘设备
     *
     * 在短按 CapsLock UP 时调用，向 OS 注入 CapsLock DOWN+UP，
     * 让 kbdclass 正常切换大小写并更新 LED。
     */
    void InjectCapsLockToggle(InterceptionDevice device);

    /**
     * @brief 通过 Interception 注入按键（组合键绑定转译）
     * @param device 目标键盘设备
     * @param vkCode 目标键的虚拟键码
     * @param withShift 是否附加 Shift
     */
    void InjectKey(InterceptionDevice device, WORD vkCode, bool withShift = false);

    /**
     * @brief 检查 Interception 驱动是否可用
     * @return true 驱动已安装可用，false 驱动未安装
     */
    static bool IsDriverAvailable();

private:
    InterceptionContext m_context = nullptr;            // Interception context
    std::thread m_thread;                                // 接收线程
    std::atomic<bool> m_running{false};                  // 接收线程运行标志

    // VK → scan code + E0 标志映射
    // Why: Interception 使用 scan code 而不是 VK code，
    //      注入按键时需要转换。扩展键（箭头、Home、End 等）
    //      需要加 INTERCEPTION_KEY_E0 标志。
    struct ScanCodeInfo
    {
        unsigned short code;      // scan code
        bool extended;           // 是否为扩展键（需 E0 标志）
    };
    ScanCodeInfo VkToScanCode(WORD vkCode) const;

    /**
     * @brief 接收线程主循环
     *
     * 阻塞等待键盘事件 → 读取 → 交由 StateMachine 处理 →
     * 根据返回决策：send（放行）或 suppress（不 send）
     */
    void ReceiveLoop();
};

// 全局实例
extern InterceptionHook g_interceptionHook;

#endif // CAPSX_CORE_INTERCEPTION_HOOK_H_