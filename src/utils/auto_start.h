/**
 * @file auto_start.h
 * @brief 开机自启管理模块
 *
 * 通过写入/删除 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
 * 注册表项，控制 CapsX 是否在用户登录时自动启动。
 *
 * Why: 使用 HKCU（当前用户）而非 HKLM，无需管理员权限即可开关自启，
 *      也避免影响其他用户账户。
 */

#ifndef CAPSX_UTILS_AUTO_START_H_
#define CAPSX_UTILS_AUTO_START_H_

#include <Windows.h>

class AutoStart
{
public:
    /**
     * @brief 查询当前是否已启用开机自启
     * @return true 已注册自启，false 未注册或查询失败
     */
    bool IsEnabled() const;

    /**
     * @brief 启用开机自启
     * @return true 写入注册表成功，false 失败
     *
     * 将当前 exe 完整路径写入 Run 键，值为 "CapsX"。
     * 路径含空格时自动加引号，保证系统能正确解析。
     */
    bool Enable();

    /**
     * @brief 关闭开机自启
     * @return true 删除注册表项成功（或不存在），false 失败
     */
    bool Disable();

    /**
     * @brief 按目标状态设置开机自启
     * @param enabled true 启用，false 关闭
     * @return true 操作成功，false 失败
     */
    bool SetEnabled(bool enabled);

private:
    /**
     * @brief 获取当前进程 exe 的完整路径
     * @param outPath 输出缓冲区
     * @param cchSize 缓冲区字符数（含结尾 \\0）
     * @return true 获取成功，false 失败
     */
    bool GetExePath(wchar_t* outPath, DWORD cchSize) const;

    /**
     * @brief 打开 Run 注册表键
     * @param access 访问权限（KEY_READ / KEY_WRITE 等）
     * @param outKey 输出的键句柄，调用方负责 RegCloseKey
     * @return true 打开成功，false 失败
     */
    bool OpenRunKey(REGSAM access, HKEY* outKey) const;
};

// 全局实例
extern AutoStart g_autoStart;

#endif // CAPSX_UTILS_AUTO_START_H_
