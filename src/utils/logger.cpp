/**
 * @file logger.cpp
 * @brief 简易日志工具实现
 */

#include "utils/logger.h"

#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <ShlObj.h>
#include <direct.h>   // _mkdir

Logger g_logger;

bool Logger::Init()
{
    // 获取 %APPDATA% 路径
    WCHAR appDataPath[MAX_PATH] = {};
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath) != S_OK)
    {
        return false;
    }

    // 构造 CapsX 日志目录: %APPDATA%\CapsX
    WCHAR logDir[MAX_PATH] = {};
    swprintf_s(logDir, L"%s\\CapsX", appDataPath);

    // 创建目录（如果不存在）
    // Why: _mkdir 对宽字符路径不可用，使用 Win32 CreateDirectoryW
    CreateDirectoryW(logDir, NULL);

    // 构造日志文件路径
    WCHAR logPath[MAX_PATH] = {};
    swprintf_s(logPath, L"%s\\CapsX\\capsx.log", appDataPath);

    // 转换为窄字符串用于 fopen
    char narrowPath[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, logPath, -1, narrowPath, MAX_PATH, NULL, NULL);
    m_logPath = narrowPath;

    // 打开日志文件（追加模式）
    m_file = fopen(m_logPath.c_str(), "a");
    if (m_file == nullptr)
    {
        return false;
    }

    LogInfo("========== CapsX started ==========");
    return true;
}

void Logger::Shutdown()
{
    if (m_file != nullptr)
    {
        LogInfo("========== CapsX exited ==========");
        fclose(static_cast<FILE*>(m_file));
        m_file = nullptr;
    }
}

void Logger::LogInfo(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteLog("INFO", format, args);
    va_end(args);
}

void Logger::LogWarning(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteLog("WARN", format, args);
    va_end(args);
}

void Logger::LogError(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    WriteLog("ERROR", format, args);
    va_end(args);
}

void Logger::LogDebug(const char* format, ...)
{
#ifndef NDEBUG
    va_list args;
    va_start(args, format);
    WriteLog("DEBUG", format, args);
    va_end(args);
#else
    (void)format;
#endif
}

void Logger::WriteLog(const char* level, const char* format, va_list args)
{
    if (m_file == nullptr)
    {
        return;
    }

    // 获取当前时间
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf = {};
    localtime_s(&tmBuf, &timeT);

    // 写入时间戳和级别
    char timeStr[32] = {};
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &tmBuf);
    fprintf(static_cast<FILE*>(m_file), "[%s] [%s] ", timeStr, level);

    // 写入格式化消息
    vfprintf(static_cast<FILE*>(m_file), format, args);
    fprintf(static_cast<FILE*>(m_file), "\n");
    fflush(static_cast<FILE*>(m_file));
}