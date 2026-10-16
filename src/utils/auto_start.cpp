/**
 * @file auto_start.cpp
 * @brief 当前用户的开机自启实现
 */

#include "utils/auto_start.h"
#include "utils/logger.h"

#include <Windows.h>
#include <string>

namespace
{

const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* VALUE_NAME = L"CapsX";

/**
 * @brief 取得带引号的当前程序路径
 * @param quotedPath 输出路径，已包上引号，避免路径中的空格被拆开
 * @return true 取得了完整路径
 */
bool current_exe_command(std::wstring& quotedPath)
{
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (length == 0 || length >= 32768)
    {
        return false;
    }
    quotedPath = L"\"";
    quotedPath += path;
    quotedPath += L"\"";
    return true;
}

} // namespace

bool AutoStart::IsEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    const LONG query = RegQueryValueExW(key, VALUE_NAME, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return query == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t);
}

bool AutoStart::SetEnabled(bool enabled)
{
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key);
    if (opened != ERROR_SUCCESS)
    {
        g_logger.LogError("Open Run key failed, error %ld", opened);
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enabled)
    {
        std::wstring command;
        if (!current_exe_command(command))
        {
            RegCloseKey(key);
            g_logger.LogError("GetModuleFileNameW failed, error %lu", GetLastError());
            return false;
        }
        result = RegSetValueExW(
            key,
            VALUE_NAME,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        result = RegDeleteValueW(key, VALUE_NAME);
        if (result == ERROR_FILE_NOT_FOUND)
        {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
    {
        g_logger.LogError("Update autostart failed, error %ld", result);
        return false;
    }
    g_logger.LogInfo("Autostart %s", enabled ? "enabled" : "disabled");
    return true;
}
