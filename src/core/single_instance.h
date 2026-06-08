/**
 * @file single_instance.h
 * @brief 单实例控制模块
 *
 * 通过命名互斥体确保 CapsX 只有一个实例运行。
 * 重复启动时拒绝运行并提示用户。
 *
 * Why: 全局键盘钩子类应用多实例同时运行会导致钩子冲突、
 *      按键行为不可预测。命名互斥体是 Windows 内核对象，
 *      进程崩溃时 OS 自动释放，不会残留锁，
 *      比文件锁或窗口枚举更可靠。
 */

#ifndef CAPSX_CORE_SINGLE_INSTANCE_H_
#define CAPSX_CORE_SINGLE_INSTANCE_H_

#include <Windows.h>

class SingleInstance
{
public:
    /**
     * @brief 尝试获取单实例锁
     * @return true 获取成功（当前是唯一实例），false 已有实例在运行
     */
    bool TryAcquire();

    /**
     * @brief 释放单实例锁（程序退出时调用）
     */
    void Release();

private:
    HANDLE m_mutex = nullptr;    // 命名互斥体句柄
};

// 全局实例
extern SingleInstance g_singleInstance;

#endif // CAPSX_CORE_SINGLE_INSTANCE_H_