/**
 * @file auto_start.h
 * @brief 当前用户的开机自启
 *
 * 用 HKCU\Software\Microsoft\Windows\CurrentVersion\Run 记录 CapsX.exe。
 * 只影响当前登录用户，不需要管理员权限。
 */

#ifndef CAPSX_UTILS_AUTO_START_H_
#define CAPSX_UTILS_AUTO_START_H_

class AutoStart
{
public:
    /**
     * @brief 查询当前用户是否已登记开机启动 CapsX
     * @return true 注册表里已有非空的 CapsX 启动项
     */
    static bool IsEnabled();

    /**
     * @brief 打开或关闭开机自启
     * @param enabled true 写入当前 exe 路径，false 删除启动项
     * @return true 注册表已按请求更新
     */
    static bool SetEnabled(bool enabled);
};

#endif // CAPSX_UTILS_AUTO_START_H_
