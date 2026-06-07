/**
 * @file tray_icon.h
 * @brief 系统托盘图标管理
 *
 * 使用 Shell_NotifyIcon 创建系统托盘图标，
 * 左键单击启用/禁用 CapsX，右键菜单提供退出等操作。
 *
 * Why: 使用 HWND_MESSAGE 消息窗口接收托盘回调，
 *      不可见、不接收系统广播、不会被 FindWindow 找到。
 */

#ifndef CAPSX_UI_TRAY_ICON_H_
#define CAPSX_UI_TRAY_ICON_H_

#include <Windows.h>

class TrayIcon
{
public:
    /**
     * @brief 创建托盘图标和消息窗口
     * @return true 创建成功，false 创建失败
     */
    bool Create();

    /**
     * @brief 移除托盘图标
     */
    void Remove();

    /**
     * @brief 更新托盘图标（状态变更时调用）
     * @param active true 启用状态，false 禁用状态
     */
    void UpdateIcon(bool active);

private:
    HWND m_trayWindow = nullptr;        // 消息窗口句柄
    HICON m_currentIcon = nullptr;      // 当前托盘图标
    NOTIFYICONDATAW m_nid = {};         // 托盘图标数据
    bool m_active = true;               // 当前启用状态

    /**
     * @brief 消息窗口回调函数
     * @param hWnd 窗口句柄
     * @param uMsg 消息 ID
     * @param wParam 消息参数
     * @param lParam 消息参数
     * @return LRESULT 消息处理结果
     */
    static LRESULT CALLBACK TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// 全局实例
extern TrayIcon g_trayIcon;

#endif // CAPSX_UI_TRAY_ICON_H_