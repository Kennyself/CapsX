/**
 * @file keyboard_hook.h
 * @brief WH_KEYBOARD_LL 全局低级键盘钩子封装
 *
 * 提供安装/卸载全局低级键盘钩子的接口，
 * 通过回调函数将按键事件分发给状态机处理。
 */

#ifndef CAPSX_CORE_KEYBOARD_HOOK_H_
#define CAPSX_CORE_KEYBOARD_HOOK_H_

#include <Windows.h>
#include <functional>

class KeyboardHook
{
public:
    /**
     * @brief 安装全局低级键盘钩子
     * @return true 安装成功，false 安装失败
     */
    bool Install();

    /**
     * @brief 卸载键盘钩子
     */
    void Uninstall();

    /**
     * @brief 检查钩子是否已安装且有效
     * @return true 钩子已安装，false 钩子未安装或已失效
     */
    bool IsInstalled() const;

    /**
     * @brief 设置按键事件回调函数
     * @param cb 回调函数，接收 nCode/wParam/lParam，返回 LRESULT
     *
     * 回调在钩子线程（即主线程消息循环）中执行。
     * 返回非零值表示拦截该按键事件，返回 CallNextHookEx 结果表示放行。
     */
    using KeyCallback = std::function<LRESULT(int, WPARAM, LPARAM)>;
    void SetCallback(KeyCallback cb);

private:
    HHOOK m_hook = nullptr;          // 钩子句柄
    KeyCallback m_callback;          // 键盘事件回调函数

    /**
     * @brief WH_KEYBOARD_LL 回调函数（静态，供 Windows 调用）
     * @param nCode 钩子代码（HC_ACTION 表示有按键事件）
     * @param wParam 按键事件类型（WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN/WM_SYSKEYUP）
     * @param lParam KBDLLHOOKSTRUCT 指针
     * @return LRESULT 拦截返回 1，放行返回 CallNextHookEx
     */
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
};

#endif // CAPSX_CORE_KEYBOARD_HOOK_H_