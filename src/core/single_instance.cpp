/**
 * @file single_instance.cpp
 * @brief 单实例控制模块实现
 *
 * 使用命名互斥体检测是否有其他 CapsX 实例正在运行。
 * Why: 命名互斥体是 Windows 内核对象，进程退出（含崩溃）时
 *      OS 自动释放，不存在残留锁的问题。Global\\ 前缀确保
 *      在多会话环境下也能检测到其他终端中运行的实例。
 */

#include "core/single_instance.h"
#include "utils/logger.h"

// 互斥体名称使用 GUID 后缀，避免与其他应用冲突
// Why: Global\\ 前缀确保跨会话可见（如同一用户的不同 Terminal Services 会话）
static const wchar_t* MUTEX_NAME = L"Global\\CapsX_SingleInstance_{A7B3C9D2-E5F1-4A8B-6C0D-7E2F8A9B3C1D}";

bool SingleInstance::TryAcquire()
{
    m_mutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // 已有实例在运行，关闭当前获取的句柄并拒绝启动
        // Why: CreateMutexW 即使互斥体已存在也会返回有效句柄，
        //      但 GetLastError 标记为 ERROR_ALREADY_EXISTS。
        //      必须关闭此句柄，否则会泄漏内核对象引用。
        CloseHandle(m_mutex);
        m_mutex = nullptr;
        g_logger.LogInfo("Another CapsX instance is already running, exiting");
        return false;
    }

    g_logger.LogInfo("Single instance lock acquired");
    return true;
}

void SingleInstance::Release()
{
    if (m_mutex != nullptr)
    {
        CloseHandle(m_mutex);
        m_mutex = nullptr;
        g_logger.LogInfo("Single instance lock released");
    }
}

// 全局实例定义
SingleInstance g_singleInstance;