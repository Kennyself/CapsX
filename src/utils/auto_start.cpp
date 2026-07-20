/**
 * @file auto_start.cpp
 * @brief 开机自启管理模块实现
 *
 * 通过 HKCU\...\Run 注册表项实现用户级开机自启。
 * Why: 不使用启动文件夹快捷方式，是因为注册表 Run 键更便于程序读写状态，
 *      也无需依赖 COM（IShellLink）创建/删除 .lnk。
 */

#include "utils/auto_start.h"
#include "utils/logger.h"

#include <cstdio>

// Run 键路径与值名
// Why: 值名固定为 CapsX，与应用名一致，便于用户在“启动应用”设置中识别
static const wchar_t* RUN_KEY_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE_NAME = L"CapsX";

bool AutoStart::GetExePath(wchar_t* outPath, DWORD cchSize) const
{
    DWORD written = GetModuleFileNameW(nullptr, outPath, cchSize);
    if (written == 0 || written >= cchSize)
    {
        g_logger.LogError("GetModuleFileNameW failed, error: %lu", GetLastError());
        return false;
    }
    return true;
}

bool AutoStart::OpenRunKey(REGSAM access, HKEY* outKey) const
{
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, access, outKey);
    if (result != ERROR_SUCCESS)
    {
        g_logger.LogError("RegOpenKeyExW Run key failed, error: %ld", result);
        return false;
    }
    return true;
}

bool AutoStart::IsEnabled() const
{
    HKEY hKey = nullptr;
    if (!OpenRunKey(KEY_READ, &hKey))
    {
        return false;
    }

    // 查询值是否存在即可；不强制校验路径是否仍指向当前 exe
    // Why: 用户可能移动了 exe，但仍希望 UI 显示“已启用”，再由 Enable 刷新路径
    DWORD valueType = 0;
    DWORD dataSize = 0;
    LONG result = RegQueryValueExW(hKey, RUN_VALUE_NAME, nullptr, &valueType, nullptr, &dataSize);
    RegCloseKey(hKey);

    return (result == ERROR_SUCCESS && valueType == REG_SZ);
}

bool AutoStart::Enable()
{
    wchar_t exePath[MAX_PATH] = {};
    if (!GetExePath(exePath, MAX_PATH))
    {
        return false;
    }

    // 路径加引号，避免路径含空格时被系统拆成多个参数
    wchar_t quotedPath[MAX_PATH + 3] = {};
    swprintf_s(quotedPath, L"\"%s\"", exePath);

    HKEY hKey = nullptr;
    if (!OpenRunKey(KEY_SET_VALUE, &hKey))
    {
        return false;
    }

    DWORD dataBytes = static_cast<DWORD>((wcslen(quotedPath) + 1) * sizeof(wchar_t));
    LONG result = RegSetValueExW(
        hKey,
        RUN_VALUE_NAME,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(quotedPath),
        dataBytes
    );
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS)
    {
        g_logger.LogError("RegSetValueExW CapsX Run value failed, error: %ld", result);
        return false;
    }

    g_logger.LogInfo("Auto-start enabled");
    return true;
}

bool AutoStart::Disable()
{
    HKEY hKey = nullptr;
    if (!OpenRunKey(KEY_SET_VALUE, &hKey))
    {
        return false;
    }

    LONG result = RegDeleteValueW(hKey, RUN_VALUE_NAME);
    RegCloseKey(hKey);

    // 值不存在视为已关闭成功
    // Why: 重复关闭或从未开启时，用户期望操作幂等成功
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
    {
        g_logger.LogError("RegDeleteValueW CapsX Run value failed, error: %ld", result);
        return false;
    }

    g_logger.LogInfo("Auto-start disabled");
    return true;
}

bool AutoStart::SetEnabled(bool enabled)
{
    if (enabled)
    {
        return Enable();
    }
    return Disable();
}

// 全局实例定义
AutoStart g_autoStart;
