/**
 * @file interception_hook.cpp
 * @brief Interception 驱动键盘拦截封装实现
 *
 * 使用 Interception API（kbdclass 之下的 filter driver）拦截键盘事件。
 * suppress = 不调用 interception_send = 拦截
 * send = 调用 interception_send = 放行/注入
 *
 * Why: Interception 拦截在 kbdclass 之下，OS 键盘驱动从未收到被 suppress 的事件，
 *      因此 CapsLock LED 不会意外切换，从根本上解决了 WH_KEYBOARD_LL 方案的 bug。
 */

#include "core/interception_hook.h"
#include "core/state_machine.h"
#include "utils/logger.h"

// Interception API 头文件（INTERCEPTION_STATIC 已在 CMakeLists.txt 中定义）
#include "interception.h"

InterceptionHook g_interceptionHook;

// CapsLock scan code
static const unsigned short SCAN_CODE_CAPSLOCK = 0x3A;

bool InterceptionHook::Install()
{
    if (m_context != nullptr)
    {
        g_logger.LogWarning("Interception hook already installed, skipping");
        return true;
    }

    m_context = interception_create_context();
    if (m_context == nullptr)
    {
        g_logger.LogError("Interception driver not available, context creation failed");
        return false;
    }

    // 设置键盘过滤器：拦截所有键盘 DOWN 和 UP 事件
    // Why: 必须拦截 DOWN 和 UP 才能在 CapsLock 场景下做出拦截/放行决策。
    //      TermSrv LED 同步事件也需要拦截，防止 RDP 会话干扰 CapsLock LED。
    interception_set_filter(m_context, interception_is_keyboard,
        INTERCEPTION_FILTER_KEY_DOWN | INTERCEPTION_FILTER_KEY_UP
        | INTERCEPTION_FILTER_KEY_E0 | INTERCEPTION_FILTER_KEY_E1
        | INTERCEPTION_FILTER_KEY_TERMSRV_SET_LED);

    // 启动接收线程
    m_running = true;
    m_thread = std::thread(&InterceptionHook::ReceiveLoop, this);

    g_logger.LogInfo("Interception hook installed, receive thread started");
    return true;
}

void InterceptionHook::Uninstall()
{
    if (m_context == nullptr)
    {
        return;
    }

    // 停止接收线程
    m_running = false;

    // 销毁 context 会使 interception_wait 立即返回，
    // 接收线程检测到 m_running == false 后自动退出
    interception_destroy_context(m_context);
    m_context = nullptr;

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    g_logger.LogInfo("Interception hook uninstalled");
}

bool InterceptionHook::IsDriverAvailable()
{
    InterceptionContext testContext = interception_create_context();
    if (testContext == nullptr)
    {
        return false;
    }
    interception_destroy_context(testContext);
    return true;
}

void InterceptionHook::InjectCapsLockToggle(InterceptionDevice device)
{
    // 向 OS 注入 CapsLock DOWN+UP，让 kbdclass 切换大小写并更新 LED
    // Why: 短按 CapsLock 时，物理事件被 suppress，OS 从未收到。
    //      注入 CapsLock DOWN+UP 让 kbdclass 正常处理一次切换，
    //      LED 由 kbdclass 通过 IOCTL_KEYBOARD_SET_INDICATORS 更新。
    InterceptionKeyStroke strokes[2] = {};

    // CapsLock DOWN
    strokes[0].code = SCAN_CODE_CAPSLOCK;
    strokes[0].state = INTERCEPTION_KEY_DOWN;

    // CapsLock UP
    strokes[1].code = SCAN_CODE_CAPSLOCK;
    strokes[1].state = INTERCEPTION_KEY_UP;

    interception_send(m_context, device, (InterceptionStroke*)strokes, 2);
    g_logger.LogInfo("CapsLock toggle injected via Interception");
}

void InterceptionHook::InjectKey(InterceptionDevice device, WORD vkCode, bool withShift)
{
    // VK → scan code 转换
    // Why: Interception 使用 scan code 而非 VK code，
    //      注入按键需要转换。扩展键需加 INTERCEPTION_KEY_E0 标志。
    ScanCodeInfo targetInfo = VkToScanCode(vkCode);
    ScanCodeInfo shiftInfo = VkToScanCode(VK_SHIFT);

    // 计算总 stroke 数量（最多 4：Shift DOWN + Key DOWN + Key UP + Shift UP）
    InterceptionKeyStroke strokes[4] = {};
    int count = 0;

    if (withShift)
    {
        // Shift DOWN
        strokes[count].code = shiftInfo.code;
        strokes[count].state = INTERCEPTION_KEY_DOWN;
        if (shiftInfo.extended)
        {
            strokes[count].state |= INTERCEPTION_KEY_E0;
        }
        count++;
    }

    // 目标键 DOWN
    strokes[count].code = targetInfo.code;
    strokes[count].state = INTERCEPTION_KEY_DOWN;
    if (targetInfo.extended)
    {
        strokes[count].state |= INTERCEPTION_KEY_E0;
    }
    count++;

    // 目标键 UP
    strokes[count].code = targetInfo.code;
    strokes[count].state = INTERCEPTION_KEY_UP;
    if (targetInfo.extended)
    {
        strokes[count].state |= INTERCEPTION_KEY_E0;
    }
    count++;

    if (withShift)
    {
        // Shift UP
        strokes[count].code = shiftInfo.code;
        strokes[count].state = INTERCEPTION_KEY_UP;
        if (shiftInfo.extended)
        {
            strokes[count].state |= INTERCEPTION_KEY_E0;
        }
        count++;
    }

    interception_send(m_context, device, (InterceptionStroke*)strokes, count);
    g_logger.LogInfo("Key injected via Interception: vk=%u, withShift=%d, count=%d",
        vkCode, withShift, count);
}

InterceptionHook::ScanCodeInfo InterceptionHook::VkToScanCode(WORD vkCode) const
{
    ScanCodeInfo info = {};
    info.code = static_cast<unsigned short>(MapVirtualKey(vkCode, MAPVK_VK_TO_VSC));
    info.extended = false;

    // 扩展键需要 E0 标志
    // Why: 扩展键的 scan code 与普通键不同，需要 E0 前缀标识。
    //      MapVirtualKey 对扩展键返回的 scan code 本身不包含 E0 信息，
    //      需要手动判断并添加 INTERCEPTION_KEY_E0 标志。
    switch (vkCode)
    {
    case VK_RIGHT:
    case VK_LEFT:
    case VK_UP:
    case VK_DOWN:      // 方向键
    case VK_HOME:
    case VK_END:
    case VK_INSERT:
    case VK_DELETE:    // 编辑键
    case VK_PRIOR:
    case VK_NEXT:      // Page Up/Down
    case VK_RCONTROL:
    case VK_RMENU:     // 右 Ctrl/Alt
    case VK_PAUSE:
    case VK_SNAPSHOT:  // Pause/PrintScreen
    case VK_LWIN:
    case VK_RWIN:      // Win 键
    case VK_APPS:      // Context Menu
    case VK_NUMLOCK:   // NumLock（某些键盘）
    case VK_DIVIDE:    // Numpad /
        info.extended = true;
        break;
    default:
        break;
    }

    return info;
}

void InterceptionHook::ReceiveLoop()
{
    g_logger.LogInfo("Interception receive loop started");

    while (m_running)
    {
        // 阻塞等待键盘事件
        InterceptionDevice device = interception_wait_with_timeout(m_context, 100);

        if (!m_running)
        {
            break;
        }

        // interception_wait_with_timeout 返回 0 表示超时无事件
        if (interception_is_invalid(device))
        {
            continue;
        }

        // 仅处理键盘设备
        if (!interception_is_keyboard(device))
        {
            continue;
        }

        // 读取键盘事件
        InterceptionKeyStroke stroke = {};
        int received = interception_receive(m_context, device, (InterceptionStroke*)&stroke, 1);
        if (received <= 0)
        {
            continue;
        }

        // 过滤 TermSrv LED 同步事件
        // Why: Terminal Services 会发送合成事件来同步 LED 状态，
        //      这些事件带有 INTERCEPTION_KEY_TERMSRV_SET_LED 标志，
        //      会干扰 CapsLock LED 处理。必须拦截。
        if (stroke.state & INTERCEPTION_KEY_TERMSRV_SET_LED)
        {
            // 拦截 TermSrv LED 同步事件，不放行给 OS
            g_logger.LogInfo("TermSrv LED sync event suppressed: code=0x%02X, state=0x%02X",
                stroke.code, stroke.state);
            continue;
        }

        // 将 scan code 转换为 VK code，供 StateMachine 使用
        bool isKeyDown = (stroke.state & INTERCEPTION_KEY_UP) == 0;
        bool isExtended = (stroke.state & INTERCEPTION_KEY_E0) != 0;

        // scan code → VK code 转换
        UINT vkCode = MapVirtualKey(stroke.code, MAPVK_VSC_TO_VK_EX);
        if (isExtended && vkCode == 0)
        {
            // 扩展键可能需要不同的映射方式
            vkCode = MapVirtualKey(stroke.code | 0xE000, MAPVK_VSC_TO_VK_EX);
        }
        // CapsLock scan code 0x3A → VK_CAPITAL 0x14
        if (stroke.code == SCAN_CODE_CAPSLOCK && !isExtended)
        {
            vkCode = VK_CAPITAL;
        }

        // 交给 StateMachine 处理，获取决策
        // Why: StateMachine 返回处理决策而非简单的 0/1，
        //      因为 Interception 方案需要支持三种操作：
        //      suppress（不 send）、send（放行）、inject（注入新按键）
        auto decision = g_stateMachine.ProcessKeyEvent(
            isKeyDown ? WM_KEYDOWN : WM_KEYUP,
            vkCode, device, isExtended);

        // 根据决策执行操作
        switch (decision.action)
        {
        case ProcessDecisionAction::Suppress:
            // 拦截：不调用 interception_send，OS 不会收到此事件
            break;

        case ProcessDecisionAction::PassThrough:
            // 放行：将原始事件发送给 OS
            interception_send(m_context, device, (InterceptionStroke*)&stroke, 1);
            break;

        case ProcessDecisionAction::InjectCapsLockToggle:
            // 注入 CapsLock 切换（短按行为）
            InjectCapsLockToggle(device);
            break;

        case ProcessDecisionAction::InjectKey:
            // 注入绑定目标按键
            InjectKey(device, decision.targetVk, decision.withShift);
            // 放行 UP 事件不需要额外处理，因为 InjectKey 已包含完整的 DOWN+UP
            break;
        }
    }

    g_logger.LogInfo("Interception receive loop exited");
}