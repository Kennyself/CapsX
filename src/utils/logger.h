/**
 * @file logger.h
 * @brief 简易日志工具
 *
 * 提供分级日志输出（Info/Warning/Error/Debug），
 * 写入文件 %APPDATA%\CapsX\capsx.log。
 *
 * Why: 键盘钩子回调中不能阻塞，日志写入必须异步或快速，
 *      当前使用同步写入（日志量极小，不影响 < 10ms 回调要求）。
 */

#ifndef CAPSX_UTILS_LOGGER_H_
#define CAPSX_UTILS_LOGGER_H_

#include <string>

class Logger
{
public:
    /**
     * @brief 初始化日志系统
     * @return true 初始化成功，false 初始化失败
     *
     * 创建 %APPDATA%\CapsX 目录并打开日志文件。
     */
    bool Init();

    /**
     * @brief 关闭日志系统
     */
    void Shutdown();

    /**
     * @brief 输出 Info 级别日志
     * @param format 格式字符串
     * @param ... 可变参数
     */
    void LogInfo(const char* format, ...);

    /**
     * @brief 输出 Warning 级别日志
     * @param format 格式字符串
     * @param ... 可变参数
     */
    void LogWarning(const char* format, ...);

    /**
     * @brief 输出 Error 级别日志
     * @param format 格式字符串
     * @param ... 可变参数
     */
    void LogError(const char* format, ...);

    /**
     * @brief 输出 Debug 级别日志（仅 Debug 构建时输出）
     * @param format 格式字符串
     * @param ... 可变参数
     */
    void LogDebug(const char* format, ...);

private:
    void* m_file = nullptr;            // 日志文件句柄（FILE*）
    std::string m_logPath;             // 日志文件路径

    /**
     * @brief 写入一行日志
     * @param level 日志级别字符串
     * @param format 格式字符串
     * @param args 已格式化的 va_list
     */
    void WriteLog(const char* level, const char* format, va_list args);
};

// 全局日志实例
extern Logger g_logger;

#endif // CAPSX_UTILS_LOGGER_H_