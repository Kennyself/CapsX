/**
 * @file binding_manager.cpp
 * @brief 按键绑定查找管理实现
 *
 * Phase 1: 硬编码默认绑定数组
 */

#include "core/binding_manager.h"

BindingManager g_bindingManager;

// Phase 1: 硬编码默认绑定
// 对应需求文档 §3.2 光标移动 + §3.3 文本选择
static const KeyBinding DEFAULT_BINDINGS[] = {
    // 光标移动（CUR-01 ~ CUR-06）
    { 'E',      VK_UP,    false, true  },  // CapsLock + E → ↑
    { 'D',      VK_DOWN,  false, true  },  // CapsLock + D → ↓
    { 'S',      VK_LEFT,  false, true  },  // CapsLock + S → ←
    { 'F',      VK_RIGHT, false, true  },  // CapsLock + F → →
    { 'A',      VK_HOME,  false, true  },  // CapsLock + A → Home
    { 'G',      VK_END,   false, true  },  // CapsLock + G → End

    // 删除操作（CUR-07, CUR-08）
    { 'W',      VK_BACK,  false, true  },  // CapsLock + W → Backspace（向后删除）
    { 'R',      VK_DELETE, false, true  },  // CapsLock + R → Delete（向前删除）

    // 文本选择（SEL-01 ~ SEL-04）
    { 'I',      VK_UP,    true,  true  },  // CapsLock + I → Shift+↑（向上选中）
    { 'K',      VK_DOWN,  true,  true  },  // CapsLock + K → Shift+↓（向下选中）
    { 'J',      VK_LEFT,  true,  true  },  // CapsLock + J → Shift+←（向左选中）
    { 'L',      VK_RIGHT, true,  true  },  // CapsLock + L → Shift+→（向右选中）

    // 文本选择扩展（SEL-05, SEL-06）
    // CapsLock + Shift + A/G → Shift + Home/End
    // Why: 这些通过 Shift 修饰键组合实现，需要在钩子中额外处理
    //      当前暂不单独绑定，由状态机在 Shift 组合时自动处理
};

static const int BINDING_COUNT = sizeof(DEFAULT_BINDINGS) / sizeof(DEFAULT_BINDINGS[0]);

const KeyBinding* BindingManager::FindBinding(DWORD triggerVk) const
{
    for (int i = 0; i < BINDING_COUNT; i++)
    {
        if (DEFAULT_BINDINGS[i].triggerVk == triggerVk && DEFAULT_BINDINGS[i].enabled)
        {
            return &DEFAULT_BINDINGS[i];
        }
    }
    return nullptr;
}