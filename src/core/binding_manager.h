/**
 * @file binding_manager.h
 * @brief 按键绑定查找管理
 *
 * Phase 1 使用硬编码默认绑定数组 + 线性查找。
 * Phase 2 将从 JSON 配置加载绑定，使用 unordered_map O(1) 查找。
 *
 * Why: 绑定数量 < 30，线性查找性能足够，硬编码方式最快落地。
 */

#ifndef CAPSX_CORE_BINDING_MANAGER_H_
#define CAPSX_CORE_BINDING_MANAGER_H_

#include <Windows.h>

/**
 * @brief 单个按键绑定
 */
struct KeyBinding
{
    DWORD triggerVk;        // 触发键的虚拟键码（如 'E' = 0x45）
    DWORD targetVk;         // 目标键的虚拟键码（如 VK_UP）
    bool  withShift;        // 是否附加 Shift（文本选择）
    bool  enabled;          // 是否启用（冲突项默认禁用）
};

class BindingManager
{
public:
    /**
     * @brief 查找绑定
     * @param triggerVk 触发键的虚拟键码
     * @return 找到的绑定指针，nullptr 表示无绑定
     *
     * Phase 1 使用线性查找，绑定数量 < 30，性能无影响。
     */
    const KeyBinding* FindBinding(DWORD triggerVk) const;
};

// 全局实例，供状态机使用
extern BindingManager g_bindingManager;

#endif // CAPSX_CORE_BINDING_MANAGER_H_