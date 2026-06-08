# CapsX 技术设计文档

> **版本**: v0.1.0
> **创建日期**: 2026-06-06
> **最后更新**: 2026-06-06
> **状态**: 草案
> **对应需求**: [requirements.md](requirements.md) v1.0.0

---

## 目录

1. [架构概览](#1-架构概览)
2. [核心模块设计](#2-核心模块设计)
3. [UI 模块设计](#3-ui-模块设计)
4. [配置系统设计](#4-配置系统设计)
5. [项目结构](#5-项目结构)
6. [构建与发布](#6-构建与发布)
7. [技术难点与对策](#7-技术难点与对策)
8. [测试策略](#8-测试策略)

---

## 1. 架构概览

### 1.1 整体架构

单 EXE 架构，所有模块编译为一个可执行文件。无需安装，无需运行时依赖。

```
┌──────────────────────────────────────────────────┐
│                    CapsX.exe                      │
│                                                   │
│  ┌──────────────┐                                │
│  │ SingleInstance│                                │
│  │ (命名互斥体) │                                │
│  └──────────────┘                                │
│                                                   │
│  ┌─────────────┐   ┌──────────────┐              │
│  │ KeyboardHook │──▶│ StateMachine │              │
│  │ (WH_KEYBOARD │   │ (CapsLock    │              │
│  │  _LL 钩子)   │   │  三态模型)   │              │
│  └─────────────┘   └──────┬───────┘              │
│                           │                       │
│                    ┌──────▼───────┐                │
│                    │ BindingMgr   │                │
│                    │ (按键查找)   │                │
│                    └──────┬───────┘                │
│                           │                       │
│                    ┌──────▼───────┐                │
│                    │ InputSimulator│               │
│                    │ (SendInput)  │                │
│                    └──────────────┘                │
│                                                   │
│  ┌──────────────┐   ┌──────────────┐              │
│  │ TrayIcon     │   │ ConfigManager│              │
│  │ (Shell_      │   │ (JSON 读写)  │              │
│  │  NotifyIcon) │   │              │              │
│  └──────────────┘   └──────────────┘              │
│                                                   │
│  ┌──────────────┐                                 │
│  │ SettingsDlg  │  (Phase 2+)                    │
│  │ (Win32 对话框)│                                │
│  └──────────────┘                                 │
└──────────────────────────────────────────────────┘
```

### 1.2 为什么选择单 EXE

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| **单 EXE（选择）** | 分发简单（拷贝即用）；WH_KEYBOARD_LL 回调在同一进程内，无需跨进程通信 | 所有模块耦合在一个编译单元 | ✅ 选择 |
| EXE + Hook DLL | 钩子模块可独立更新 | WH_KEYBOARD_LL 回调本身就在安装线程内，DLL 方式无额外优势；增加分发复杂度 | ❌ |
| EXE + UI 子进程 | UI 崩溃不影响钩子 | 进程间通信增加延迟和复杂度；体积增大 | ❌ |

> **关键点**: WH_KEYBOARD_LL 是低级钩子，Windows 规定其回调在安装钩子的线程上下文中执行，而非在目标进程内。因此不需要将钩子回调放入独立 DLL——这与 WH_KEYBOARD（高级钩子）的行为完全不同。

### 1.3 进程模型

CapsX 运行为单一后台进程：

- **无窗口消息循环**（Phase 1）：仅运行隐藏消息循环以接收钩子回调
- **托盘消息循环**（Phase 1+）：Shell_NotifyIcon 需要窗口消息循环
- **设置对话框**（Phase 2+）：模态对话框，仅在用户打开时创建

---

## 2. 核心模块设计

### 2.1 KeyboardHook — 全局低级键盘钩子

#### 2.1.1 API 选型

| API | 说明 | 选择 |
|-----|------|------|
| `SetWindowsHookEx(WH_KEYBOARD_LL, ...)` | 全局低级键盘钩子，回调在安装线程内执行 | ✅ |
| `SetWindowsHookEx(WH_KEYBOARD, ...)` | 高级键盘钩子，回调需在 DLL 中（注入目标进程） | ❌ 回调在目标进程上下文，有安全/兼容问题 |
| `Raw Input` | 只能接收按键通知，**无法拦截**按键 | ❌ 不能满足拦截 CapsLock 的需求 |

#### 2.1.2 钩子回调流程

```
按键事件进入 WH_KEYBOARD_LL 回调
│
├─ 检查 LLKHF_INJECTED 标志
│   ├─ 是 → 跳过（防止 SendInput 模拟的按键被重新拦截）
│   └─ 否 → 继续处理
│
├─ 检查是否为 CapsLock (vkCode == VK_CAPITAL)
│   ├─ 是 → 转交 StateMachine 处理
│   │        返回 1（拦截，不传递给后续钩子/目标窗口）
│   └─ 否 → 检查 CapsLock 是否处于按住状态
│           ├─ 是 → 转交 StateMachine 检查是否为绑定键
│           │        ├─ 是 → 执行绑定动作，返回 1（拦截）
│           │        └─ 否 → 放行，返回 CallNextHookEx
│           └─ 否 → 放行，返回 CallNextHookEx
```

#### 2.1.3 关键实现细节

```cpp
// 钩子安装
HHOOK g_hook = SetWindowsHookEx(
    WH_KEYBOARD_LL,
    LowLevelKeyboardProc,    // 回调函数
    GetModuleHandle(NULL),   // 当前 EXE 的模块句柄
    0                        // 全局钩子（dwThreadId = 0）
);

// 防止 SendInput 重入
LRESULT LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

        // 忽略 SendInput 模拟的按键（LLKHF_INJECTED 标志）
        if (kb->flags & LLKHF_INJECTED)
        {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        // ... 核心处理逻辑
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}
```

#### 2.1.4 钩子失效检测与恢复

| 问题 | 策略 |
|------|------|
| 钩子被其他程序卸载 | 每隔 3s 检查 `g_hook` 是否有效；失效时重新调用 `SetWindowsHookEx` |
| 钩子回调超时（Windows 限制 300ms） | 确保回调逻辑 < 10ms，绝不阻塞 |
| UAC 权限不足 | 以管理员权限运行；启动时检测并提示 |

### 2.2 CapsLockStateMachine — CapsLock 三态模型

#### 2.2.1 状态定义

```
                    CapsLock DOWN
                    ┌───────────────┐
                    │               │
              ┌─────▼─────┐         │
              │  Idle     │         │
              │ (未按下)  │         │
              └─────┬─────┘         │
                    │               │
                    │ CapsLock DOWN │
                    │               │
              ┌─────▼─────┐    ┌────▼────┐
              │ Pressing  │────│ Modifier │
              │ (按住中)  │    │ (修饰键) │
              └─────┬─────┘    └─────┬───┘
                    │               │
       ┌────────────┤               │ CapsLock UP
       │            │ CapsLock UP   │
       │ < 300ms    │ >= 300ms      │
       │            │               │
 ┌─────▼─────┐ ┌───▼──────┐  ┌─────▼──────┐
 │ ShortPress │ │ LongPress│  │ ModifierUP │
 │ (短按释放) │ │ (长按释放)│  │ (修饰键释放)│
 │ → 切换     │ │ → 不切换 │  │ → 不切换   │
 └───────────┘ └──────────┘  └────────────┘
```

#### 2.2.2 状态转换规则

| 当前状态 | 事件 | 动作 | 目标状态 |
|---------|------|------|---------|
| Idle | CapsLock DOWN | 开始计时（steady_clock） | Pressing |
| Pressing | CapsLock UP (< 300ms) | SendInput 模拟 CapsLock 切换 | Idle |
| Pressing | CapsLock UP (>= 300ms) | 无动作 | Idle |
| Pressing | 其他键 DOWN | 进入修饰键模式 | Modifier |
| Modifier | 其他键 DOWN | 查找绑定 → SendInput 模拟目标按键 | Modifier |
| Modifier | CapsLock UP | 无动作 | Idle |
| Modifier | 修饰键 (Shift/Ctrl/Alt/Win) DOWN | **放行，不拦截** | Modifier |

#### 2.2.3 计时方案

使用 `std::chrono::steady_clock`（而非 Win32 `GetTickCount`）：

- `steady_clock` 是单调递增的，不受系统时间调整影响
- 精度在 Windows 上为 1ns 级别（实际分辨率取决于硬件，通常 ~100ns）
- C++ 标准库，无额外依赖

```cpp
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::milliseconds;

TimePoint m_capsLockDownTime;  // CapsLock 按下时刻
Duration m_threshold{300};     // 长按阈值，可配置

// CapsLock UP 时判断
Duration elapsed = std::chrono::duration_cast<Duration>(
    Clock::now() - m_capsLockDownTime
);
if (elapsed < m_threshold)
{
    // 短按 → 模拟 CapsLock 切换
    simulate_caps_lock_toggle();
}
```

#### 2.2.4 修饰键放行策略

在 Modifier 状态下，Shift/Ctrl/Alt/Win 的 DOWN/UP 事件必须放行（不拦截）：

- 原因：其他程序通过 `GetAsyncKeyState()` 检测这些修饰键的状态
- 如果拦截，`GetAsyncKeyState` 会返回"未按下"，导致组合键失效
- 放行方式：对这些键的 `nCode` 调用 `CallNextHookEx` 返回

```cpp
static const DWORD MODIFIER_VKS[] = {
    VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN,
    VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL,
    VK_LMENU, VK_RMENU
};

bool is_modifier_key(DWORD vkCode)
{
    for (auto vk : MODIFIER_VKS)
    {
        if (vkCode == vk)
        {
            return true;
        }
    }
    return false;
}
```

### 2.3 InputSimulator — 模拟输入

#### 2.3.1 API 选型

| API | 说明 | 选择 |
|-----|------|------|
| `SendInput` | Win32 推荐的现代输入模拟 API，一次调用可发送多个输入事件 | ✅ |
| `keybd_event` | 已弃用的旧 API，单次调用只能发送一个事件 | ❌ MSDN 明确建议使用 SendInput |

#### 2.3.2 SendInput 封装

```cpp
// 模拟单键按下+释放（一对事件）
void simulate_key(WORD vkCode, bool withShift = false)
{
    INPUT inputs[4] = {};  // 最多 4 个事件（Shift DOWN + Key DOWN + Key UP + Shift UP）
    int count = 0;

    if (withShift)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        count++;
    }

    // 按下
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vkCode;
    count++;

    // 释放
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vkCode;
    inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
    count++;

    if (withShift)
    {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    SendInput(count, inputs, sizeof(INPUT));
}
```

#### 2.3.3 CapsLock 切换的模拟

短按 CapsLock 时，需要模拟一次 CapsLock 切换。但模拟的按键会被 LLKHF_INJECTED 标记，所以钩子回调会自动忽略它：

```cpp
void simulate_caps_lock_toggle()
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CAPITAL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CAPITAL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}
```

> **关键**: 这里不需要额外添加 `KEYEVENTF_EXTENDEDKEY` 标志，因为 CapsLock 不是扩展键。同时因为 `LLKHF_INJECTED` 的过滤机制，模拟的 CapsLock 事件不会触发钩子回调再次进入状态机。

#### 2.3.4 CapsLock 指示灯同步

CapsLock 切换后，键盘上的 LED 指示灯需要同步：

| 方案 | 说明 | 结论 |
|------|------|------|
| SendInput 模拟切换 | 系统自动更新指示灯状态 | ✅ 系统处理，无需额外代码 |
| `keybd_event` + `SetKeyboardState` | 手动更新键盘状态 | ❌ 仅影响线程键盘状态，不更新 LED |

SendInput 模拟的 CapsLock 切换会让系统自动更新指示灯，无需额外处理。

### 2.4 BindingManager — 按键绑定查找

#### 2.4.1 绑定数据结构

```cpp
struct KeyBinding
{
    DWORD triggerVk;        // 触发键的虚拟键码（如 'E' = 0x45）
    DWORD targetVk;         // 目标键的虚拟键码（如 VK_UP）
    bool  withShift;        // 是否附加 Shift（文本选择）
    bool  enabled;          // 是否启用（冲突项默认禁用）

    // 用于查找的复合键
    uint64_t CompositeKey() const
    {
        return (uint64_t)triggerVk | ((uint64_t)withShift << 32);
    }
};
```

#### 2.4.2 查找方式

Phase 1 使用硬编码数组 + 线性查找（绑定数量 < 30，性能无影响）：

```cpp
// Phase 1: 硬编码默认绑定
static const KeyBinding DEFAULT_BINDINGS[] = {
    { 'E',      VK_UP,    false, true  },  // CapsLock + E → ↑
    { 'D',      VK_DOWN,  false, true  },  // CapsLock + D → ↓
    { 'S',      VK_LEFT,  false, true  },  // CapsLock + S → ←
    { 'F',      VK_RIGHT, false, true  },  // CapsLock + F → →
    { 'A',      VK_HOME,  false, true  },  // CapsLock + A → Home
    { 'G',      VK_END,   false, true  },  // CapsLock + G → End
    { 'I',      VK_UP,    true,  true  },  // CapsLock + I → Shift+↑ (选中)
    { 'K',      VK_DOWN,  true,  true  },  // CapsLock + K → Shift+↓
    { 'J',      VK_LEFT,  true,  true  },  // CapsLock + J → Shift+←
    { 'L',      VK_RIGHT, true,  true  },  // CapsLock + L → Shift+→
    // ... 更多绑定
};
```

Phase 2 从 JSON 配置加载绑定，存储为 `std::unordered_map<uint64_t, KeyBinding>` 以 O(1) 查找。

#### 2.4.3 Shift 组合键的处理

文本选择功能（SEL-01~04）使用 `CapsLock + I/K/J/L`，模拟 `Shift + 方向键`。

在 CapsLock 按住期间，用户按下 `I`：
1. 钩子拦截 `I` 的 DOWN 事件
2. BindingManager 查到绑定：`I → Shift + ↑`
3. InputSimulator 发送 `Shift DOWN + ↑ DOWN`
4. 用户松开 `I` → 钩子拦截 `I` 的 UP 事件
5. InputSimulator 发送 `↑ UP + Shift UP`

> **注意**: 模拟的 Shift 事件带有 `LLKHF_INJECTED` 标志，不会被钩子拦截，但 `GetAsyncKeyState(VK_SHIFT)` 能检测到它。这意味着其他程序可以正确感知 Shift 的按下状态。

---

## 3. UI 模块设计

### 3.1 TrayIcon — 系统托盘

#### 3.1.1 API

使用 Win32 `Shell_NotifyIcon` API，这是 Windows 系统托盘的标准接口。

```cpp
// 托盘图标结构
NOTIFYICONDATAW nid = {};
nid.cbSize = sizeof(NOTIFYICONDATAW);
nid.hWnd = g_trayWindow;          // 隐藏窗口，接收托盘消息
nid.uID = 1;
nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
nid.uCallbackMessage = WM_TRAYICON;  // 自定义消息 ID
nid.hIcon = create_tray_icon(active);  // 动态绘制图标
wcsncpy(nid.szTip, L"CapsX - 已启用", 128);

Shell_NotifyIconW(NIM_ADD, &nid);
```

#### 3.1.2 消息窗口

托盘图标需要一个窗口来接收回调消息。创建一个不可见的窗口：

```cpp
// 注册窗口类
WNDCLASSEXW wc = {};
wc.cbSize = sizeof(WNDCLASSEXW);
wc.lpfnWndProc = TrayWindowProc;
wc.lpszClassName = L"CapsXTrayClass";
RegisterClassExW(&wc);

// 创建隐藏窗口
g_trayWindow = CreateWindowExW(
    0, L"CapsXTrayClass", L"CapsXTray",
    0, 0, 0, 0, 0,
    HWND_MESSAGE,  // Message-only window，不可见，不接收广播
    NULL, NULL, NULL
);
```

> **为什么用 HWND_MESSAGE**: 消息窗口不会出现在任务栏或窗口列表中，不会被 `FindWindow` 找到，且不会收到系统广播消息，最适合后台工具。

#### 3.1.3 托盘图标动态绘制

使用 GDI 在内存中绘制图标，无需外部 ICO 文件：

```cpp
HICON create_tray_icon(bool active)
{
    // 创建 16x16 内存 DC + Bitmap
    HDC memDC = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateCompatibleBitmap(memDC, 16, 16);
    SelectObject(memDC, bmp);

    // 绘制圆形背景
    HBRUSH brush = CreateSolidBrush(
        active ? RGB(46, 89, 163)    // #2e59a3 蓝色（启用）
               : RGB(128, 128, 128)  // 灰色（暂停）
    );
    SelectObject(memDC, brush);
    Ellipse(memDC, 0, 0, 16, 16);

    // 绘制 "CX" 文字
    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkMode(memDC, TRANSPARENT);
    HFONT font = CreateFontW(10, 0, 0, 0, FW_BLACK, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(memDC, font);
    DrawTextW(memDC, L"CX", 2, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 转为 HICON
    HICON icon = CreateIconIndirect(&ii);  // ICONINFO 结构

    // 清理 GDI 对象
    DeleteObject(font);
    DeleteObject(brush);
    DeleteObject(bmp);
    DeleteDC(memDC);

    return icon;
}
```

#### 3.1.4 右键菜单

```cpp
// WM_TRAYICON 处理
case WM_RBUTTONUP:
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_ENABLE,  L"启用/禁用");
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"设置");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT,    L"关于 CapsX");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,     L"退出");
    SetForegroundWindow(g_trayWindow);  // 确保菜单能正常关闭
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, g_trayWindow, NULL);
    DestroyMenu(hMenu);
    break;
}
```

#### 3.1.5 左键单击 — 启用/禁用

```cpp
case WM_LBUTTONUP:
{
    g_enabled = !g_enabled;
    update_tray_icon();  // 更新图标颜色和提示文字
    break;
}
```

禁用时，钩子回调仍然存在，但对所有按键事件直接 `CallNextHookEx` 放行。

### 3.2 SettingsDlg — 设置对话框（Phase 2+）

使用 Win32 `CreateDialogParam` / `DialogBoxParam` 创建模态对话框。

#### 3.2.1 对话框布局

设置界面需求（SET-01~07）较简单，可用标准 Win32 控件实现：

| SET 需求 | Win32 控件 | 说明 |
|---------|-----------|------|
| SET-01 快捷键列表 | `ListView`（LVS_REPORT） | 列表展示绑定，双击编辑 |
| SET-02 镜像阈值 | `Edit` + `UpDown`（Spin 控件） | 数值输入，范围 100~1000ms |
| SET-03 开机自启 | `CheckBox` | 注册表 `HKCU\...\Run` |
| SET-04 排除应用 | `ListBox` + `Edit` + Button | 添加/删除进程名 |
| SET-05 导入/导出 | `Button` | 打开/保存文件对话框 |
| SET-06 语言 | `ComboBox` | zh-CN / en-US |
| SET-07 自定义绑定 | `Button` → 弹出键位捕获对话框 | 子对话框 |

#### 3.2.2 对话框资源定义

在 `.rc` 资源文件中用 `DIALOGEX` 定义：

```rc
IDD_SETTINGS DIALOGEX 0, 0, 320, 240
STYLE DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "CapsX 设置"
FONT 9, "Segoe UI"
BEGIN
    CONTROL "", IDC_BINDING_LIST, "SysListView32",
            LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            7, 7, 306, 120
    CONTROL "", IDC_THRESHOLD_EDIT, "Edit",
            ES_NUMBER | WS_BORDER,
            7, 140, 50, 14
    CONTROL "", IDC_THRESHOLD_SPIN, "msctls_updown32",
            UDS_AUTOBUDDY | UDS_SETBUDDYINT,
            57, 140, 12, 14
    LTEXT "ms (长按阈值)", IDC_THRESHOLD_LABEL, 72, 140, 80, 14
    AUTOCHECKBOX "开机自动启动", IDC_AUTOSTART, 7, 165, 100, 14
    PUSHBUTTON "导入配置", IDC_IMPORT, 220, 215, 50, 14
    PUSHBUTTON "导出配置", IDC_EXPORT, 275, 215, 50, 14
    PUSHBUTTON "确定", IDOK, 260, 215, 50, 14
END
```

---

## 4. 配置系统设计

### 4.1 JSON 库选型

| 库 | 特点 | 体积 | 选择 |
|----|------|------|------|
| **nlohmann/json** | header-only，现代 C++ API，直觉式用法 | ~500KB（单头文件） | ✅ |
| rapidjson | 高性能，SSE/SSE4 优化，内存分配器可定制 | ~200KB（头文件） | ❌ API 较繁琐 |
| simdjson | 解析速度最快（SIMD），但仅解析，不支持修改和序列化 | ~300KB | ❌ 不满足写配置需求 |

选择 nlohmann/json 的理由：
- `json config = json::parse(fileContent); config["general"]["longPressThresholdMs"] = 300;` — 代码最直观
- header-only 引入，无需编译额外源文件
- 修改和序列化功能完善，满足导入/导出需求
- 性能对配置文件（< 10KB）完全足够

#### 4.1.1 引入方式

```bash
# 下载 single-header 版本到项目中
curl -L https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp \
     -o src/third_party/nlohmann/json.hpp
```

在 CMakeLists.txt 中：
```cmake
target_include_directories(CapsX PRIVATE src/third_party)
```

### 4.2 配置文件路径

```
%APPDATA%\CapsX\settings.json    ← 主配置文件
%APPDATA%\CapsX\bindings.json    ← 按键绑定（Phase 2，独立文件便于导入/导出）
```

Phase 1 不创建配置文件，所有绑定硬编码在源码中。

### 4.3 配置文件结构

对应需求文档 §6.1：

```jsonc
{
  "version": "1.0.0",
  "general": {
    "enabled": true,
    "startWithWindows": true,
    "longPressThresholdMs": 300,
    "language": "zh-CN"
  },
  "excludedApps": ["vmware.exe", "mstsc.exe", "steam.exe"],
  "keybindings": {
    "cursor": {
      "up":    { "key": "E", "target": "Up"    },
      "down":  { "key": "D", "target": "Down"  },
      "left":  { "key": "S", "target": "Left"  },
      "right": { "key": "F", "target": "Right" },
      "home":  { "key": "A", "target": "Home"  },
      "end":   { "key": "G", "target": "End"   }
    },
    "selection": {
      "up":    { "key": "I", "target": "Up",    "withShift": true },
      "down":  { "key": "K", "target": "Down",  "withShift": true },
      "left":  { "key": "J", "target": "Left",  "withShift": true },
      "right": { "key": "L", "target": "Right", "withShift": true }
    },
    "functionKeys": {
      "F1":  { "key": "1"  },
      "F2":  { "key": "2"  },
      // ...
      "F12": { "key": "="  }
    }
  }
}
```

### 4.4 配置加载流程

```
启动 CapsX
│
├─ 检查 %APPDATA%\CapsX\settings.json 是否存在
│   ├─ 不存在 → 使用硬编码默认值，创建默认配置文件
│   └─ 存在 → 解析 JSON
│       ├─ 解析成功 → 合并默认值（缺失字段用默认值填充）
│       └─ 解析失败 → 日志警告，使用硬编码默认值
│
└─ 将配置应用到 StateMachine 和 BindingManager
```

### 4.5 VK 码映射表

JSON 配置中使用**键名字符串**（如 `"E"`、`"Space"`、`"Left"`），需要映射到 Windows 虚拟键码：

```cpp
// 键名 → VK 码映射
static const std::unordered_map<std::string, DWORD> KEY_NAME_TO_VK = {
    // 字母键
    {"A", 0x41}, {"B", 0x42}, ..., {"Z", 0x5A},
    // 数字键
    {"0", 0x30}, {"1", 0x31}, ..., {"9", 0x39},
    // 特殊键
    {"Space",    VK_SPACE},
    {"Enter",    VK_RETURN},
    {"Backspace", VK_BACK},
    {"Delete",   VK_DELETE},
    {"Escape",   VK_ESCAPE},
    {"Tab",      VK_TAB},
    {"Left",     VK_LEFT},
    {"Right",    VK_RIGHT},
    {"Up",       VK_UP},
    {"Down",     VK_DOWN},
    {"Home",     VK_HOME},
    {"End",      VK_END},
    // 符号键
    {"-", 0xBD}, {"=", 0xBB}, {";", 0xBA},
    // 功能键
    {"F1", VK_F1}, ..., {"F12", VK_F12},
};
```

---

## 5. 项目结构

### 5.1 目录结构

```
CapsX/
├── CMakeLists.txt                  # 构建配置
├── src/
│   ├── main.cpp                    # 入口：初始化钩子+消息循环
│   ├── core/
│   │   ├── keyboard_hook.h/cpp     # WH_KEYBOARD_LL 钩子封装
│   │   ├── state_machine.h/cpp     # CapsLock 三态状态机
│   │   ├── input_simulator.h/cpp   # SendInput 封装
│   │   ├── binding_manager.h/cpp   # 按键绑定查找（Phase 1 硬编码，Phase 2 JSON）
│   │   ├── single_instance.h/cpp   # 单实例控制（命名互斥体）
│   │   └── config_manager.h/cpp    # JSON 配置读写
│   ├── ui/
│   │   ├── tray_icon.h/cpp         # Shell_NotifyIcon + 隐藏窗口
│   │   ├── tray_icon_drawer.h/cpp  # GDI 动态绘制托盘图标
│   │   ├── settings_dialog.h/cpp   # Win32 对话框（Phase 2+）
│   │   └── resource.h              # 控件 ID 定义
│   │   └── resource.rc             # 对话框/菜单资源定义
│   ├── utils/
│   │   ├── logger.h/cpp            # 简易日志（文件输出）
│   │   ├── string_utils.h/cpp      # VK 码 ↔ 键名映射
│   │   └── auto_start.h/cpp        # 注册表开机自启
│   └── third_party/
│       └── nlohmann/
│           └── json.hpp             # JSON 解析库（header-only）
├── tests/
│   ├── test_state_machine.cpp       # 状态机单元测试
│   ├── test_binding_manager.cpp     # 绑定查找单元测试
│   ├── test_config_manager.cpp      # 配置加载/保存测试
│   └── CMakeLists.txt               # 测试构建配置
├── docs/
│   ├── requirements.md              # 需求规格说明书
│   └── technical-design.md          # 本文档
└── .github/
    └── workflows/
        └── build.yml                # CI：构建 + 测试
```

### 5.2 模块依赖关系

```
main.cpp
  ├──→ single_instance         # 单实例检测（命名互斥体）
  ├──→ keyboard_hook
  │      └→ state_machine
  │      │   └→ binding_manager
  │      │   │   └→ config_manager ──→ nlohmann/json
  │      │   └→ input_simulator
  │      └→ logger
  ├──→ tray_icon
  │      └→ tray_icon_drawer
  ├──→ settings_dialog (Phase 2+)
  │      └→ config_manager
  │      └→ string_utils
  └→ auto_start (Phase 2+)
```

### 5.3 头文件规范

每个模块的 `.h` 文件只暴露必要的接口，内部实现细节放在 `.cpp` 中：

```cpp
// keyboard_hook.h — 对外接口
class KeyboardHook
{
public:
    bool Install();    // 安装钩子
    void Uninstall();  // 卸载钩子
    bool IsInstalled() const;

    // 设置回调
    using KeyCallback = std::function<LRESULT(int, WPARAM, LPARAM)>;
    void SetCallback(KeyCallback cb);

private:
    HHOOK m_hook = nullptr;          // 钩子句柄
    KeyCallback m_callback;          // 键盘事件回调函数
    // ... 内部细节在 .cpp 中
};
```

---

## 6. 构建与发布

### 6.1 构建系统

使用 **CMake** 作为构建系统：

| 方案 | 说明 | 选择 |
|------|------|------|
| **CMake** | 跨 IDE 支持（VS/CLion/VSC+Make），CI 集成方便 | ✅ |
| Visual Studio .vcxproj | 只能在 VS 中使用 | ❌ 绑定单一 IDE |

```cmake
cmake_minimum_required(VERSION 3.20)
project(CapsX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
add_executable(CapsX WIN32
    src/main.cpp
    src/core/keyboard_hook.cpp
    src/core/state_machine.cpp
    src/core/input_simulator.cpp
    src/core/binding_manager.cpp
    src/core/single_instance.cpp
    src/ui/tray_icon.cpp
    src/ui/tray_icon_drawer.cpp
    src/utils/logger.cpp
    src/utils/string_utils.cpp
    # Phase 2+
    # src/core/config_manager.cpp
    # src/ui/settings_dialog.cpp
    # src/utils/auto_start.cpp
)

target_include_directories(CapsX PRIVATE
    src/
    src/third_party
)

# Win32 链接
target_link_libraries(CapsX PRIVATE
    comctl32   # ListView、Spin 控件
    shell32    # Shell_NotifyIcon
)

# 测试
enable_testing()
add_subdirectory(tests)
```

### 6.2 编译环境

#### 6.2.1 当前开发环境

| 项目 | 版本/路径 |
|------|----------|
| 操作系统 | Windows 11 Pro x64 |
| 编译器 | MinGW GCC 16.1.0 (`x86_64-w64-mingw32`) |
| 编译器路径 | `C:\Users\Kenny\tools\mingw64\bin\` |
| CMake | 3.28.3 |
| CMake 路径 | `C:\Users\Kenny\tools\cmake-3.28.3-windows-x86_64\bin\` |
| Make 工具 | mingw32-make（随 MinGW 一起安装） |
| C++ 标准 | C++17 |
| 链接选项 | `-municode -static -static-libgcc -static-libstdc++`（MinGW 专用） |

#### 6.2.2 编译命令

编译工具不在系统 PATH 中，需使用完整路径或在 PowerShell 中临时添加 PATH：

**方式一：直接使用完整路径（在项目根目录运行）**

```powershell
cd F:\09_Projects\CapsX

# 配置（首次或新增源文件后需重新执行）
& "C:\Users\Kenny\tools\cmake-3.28.3-windows-x86_64\bin\cmake.exe" -S . -B build -G "MinGW Makefiles"

# 编译
& "C:\Users\Kenny\tools\cmake-3.28.3-windows-x86_64\bin\cmake.exe" --build build
```

**方式二：临时添加 PATH（推荐，后续命令可直接用 cmake）（在项目根目录运行）**

```powershell
cd F:\09_Projects\CapsX
$env:PATH = "C:\Users\Kenny\tools\mingw64\bin;C:\Users\Kenny\tools\cmake-3.28.3-windows-x86_64\bin;" + $env:PATH

# 配置
cmake -S . -B build -G "MinGW Makefiles"

# 编译
cmake --build build
```

**方式三：直接使用 mingw32-make（需已执行过 CMake 配置）（在项目根目录运行）**

```powershell
cd F:\09_Projects\CapsX
C:\Users\Kenny\tools\mingw64\bin\mingw32-make.exe -C build
```

> **注意**: 首次构建或新增源文件后，必须先执行 CMake 配置步骤重新生成构建文件，再执行编译。

#### 6.2.3 编译器与平台兼容性

| 编译器 | 支持状态 | 说明 |
|--------|---------|------|
| MinGW GCC 16.1.0+ | ✅ 当前使用 | `-municode` 启用 wWinMain 入口，`-static` 消除 DLL 依赖 |
| MSVC 19.30+（VS 2022 17.0+） | ✅ 兼容 | `/utf-8` 源码字符集，`_CRT_SECURE_NO_WARNINGS` 抑制警告 |
| 目标平台 | Windows 10 22H2+ / Windows 11 x64 | |
| 编译配置 | Release: `-O2 -DNDEBUG`; Debug: `-O0 -g` | |

### 6.3 发布方案

| 方案 | 体积 | 说明 | 选择 |
|------|------|------|------|
| **单 .exe 直接分发** | ~1~3MB | 编译为 Release，直接分发 CapsX.exe | ✅ |
| MSI 安装包 | ~2~5MB | 提供安装/卸载流程，开机自启注册 | Phase 2+ 考虑 |
| ZIP 包 | ~1~3MB | 便携版，解压即用 | ✅ 同时提供 |

Phase 1 直接编译为单 .exe + ZIP 便携包。

### 6.4 预期体积

| 来源 | 估算 |
|------|------|
| CapsX.exe（Release 编译） | ~500KB ~ 1MB |
| nlohmann/json.hpp（编译后增量） | ~200KB |
| 总计 | ~1 ~ 2MB |

对比 .NET 自包含发布（163MB），体积优势显著。

---

## 7. 技术难点与对策

### 7.1 SendInput 重入问题

| 风险 | 描述 | 对策 |
|------|------|------|
| 钩子拦截自己模拟的按键 | SendInput 产生的按键事件也会进入钩子回调，形成循环 | 检查 `LLKHF_INJECTED` 标志，跳过所有注入事件 |

### 7.2 CapsLock 状态一致性

| 风险 | 描述 | 对策 |
|------|------|------|
| 模拟切换与实际状态不一致 | 程序启动时 CapsLock 状态未知，可能导致切换方向错误 | 用 `GetKeyState(VK_CAPITAL)` 检查当前状态，模拟切换后再次验证 |
| 其他程序切换了 CapsLock | 外部程序（如输入法）可能切换 CapsLock 状态 | 钩子拦截所有 CapsLock DOWN/UP，外部切换也必须通过钩子 → 不存在此问题 |

### 7.3 钩子被卸载/绕过

| 风险 | 描述 | 对策 |
|------|------|------|
| 其他程序调用 UnhookWindowsHookEx | 某些安全软件或键盘工具可能卸载 CapsX 的钩子 | 定时检查 `g_hook` 有效性，失效时重新注册 |
| 钩子超时被 Windows 移除 | WH_KEYBOARD_LL 回调超过 300ms 会被系统强制移除 | 确保回调 < 10ms，SendInput 调用本身 < 1ms |

### 7.4 UAC 权限

| 飅险 | 描述 | 对策 |
|------|------|------|
| 目标窗口以管理员权限运行 | 低权限进程的钩子无法拦截高权限窗口的按键 | CapsX 需要以管理员权限运行 |
| 用户拒绝管理员权限 | 钩子对管理员窗口失效 | 检测到不足权限时，托盘图标显示警告状态 |

### 7.5 游戏和全屏应用兼容性

| 风险 | 描述 | 对策 |
|------|------|------|
| 游戏使用 Raw Input/DirectInput | WH_KEYBOARD_LL 对使用 Raw Input 的游戏无效 | 检测全屏独占应用 → 自动暂停 CapsX |
| 游戏中误触发 | CapsLock 组合键可能与游戏按键冲突 | 排除应用列表 + 全屏检测 |

全屏检测方案：

```cpp
bool is_fullscreen_app()
{
    // 获取前台窗口
    HWND foreground = GetForegroundWindow();
    if (!foreground)
    {
        return false;
    }

    // 获取窗口矩形和屏幕矩形
    RECT windowRect, screenRect;
    GetWindowRect(foreground, &windowRect);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &screenRect, 0);

    // 检查窗口是否覆盖整个屏幕
    return (windowRect.left <= screenRect.left &&
            windowRect.top <= screenRect.top &&
            windowRect.right >= screenRect.right &&
            windowRect.bottom >= screenRect.bottom);
}
```

---

## 8. 测试策略

### 8.1 单元测试框架

使用 **Catch2**（header-only C++ 测试框架）：

| 方案 | 说明 | 选择 |
|------|------|------|
| **Catch2** | header-only，API 直觉式，单文件引入 | ✅ |
| Google Test | 功能强大，但需要编译 gtest 库 | ❌ 增加构建复杂度 |

```cmake
# tests/CMakeLists.txt
add_executable(CapsX_tests
    test_state_machine.cpp
    test_binding_manager.cpp
)
target_include_directories(CapsX_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/third_party
)
target_link_libraries(CapsX_tests PRIVATE CapsX)
add_test(NAME CapsX_tests COMMAND CapsX_tests)
```

### 8.2 测试范围

| 模块 | 测试方法 | 测试内容 |
|------|---------|---------|
| StateMachine | 单元测试 | 短按/长按/组合键的状态转换；边界阈值（299ms vs 300ms vs 301ms）；修饰键放行逻辑 |
| BindingManager | 单元测试 | 按键查找（存在/不存在/冲突/禁用）；VK 码映射 |
| ConfigManager | 单元测试 | JSON 解析正确性；缺失字段默认值填充；损坏文件容错 |
| InputSimulator | 手动测试 | SendInput 模拟准确性需在实际环境中验证 |
| KeyboardHook | 手动/集成测试 | 钩子安装/卸载；LLKHF_INJECTED 过滤；管理员权限 |
| TrayIcon | 手动测试 | 图标显示；左键切换；右键菜单；图标颜色状态 |

### 8.3 状态机测试示例

```cpp
TEST_CASE("StateMachine: 短按 (< 300ms) 切换 CapsLock")
{
    StateMachine sm;
    sm.SetThreshold(300ms);

    sm.OnCapsLockDown();           // CapsLock 按下
    sm.OnCapsLockUp(250ms);        // 250ms 后松开 → 短按

    REQUIRE(sm.ShouldToggleCapsLock() == true);
    REQUIRE(sm.IsInModifierMode() == false);
}

TEST_CASE("StateMachine: 长按 (>= 300ms) 不切换")
{
    StateMachine sm;
    sm.SetThreshold(300ms);

    sm.OnCapsLockDown();
    sm.OnCapsLockUp(500ms);        // 500ms 后松开 → 长按

    REQUIRE(sm.ShouldToggleCapsLock() == false);
}

TEST_CASE("StateMachine: 组合键进入修饰键模式")
{
    StateMachine sm;

    sm.OnCapsLockDown();
    sm.OnOtherKeyDown('E');        // CapsLock 按住期间按下 E

    REQUIRE(sm.IsInModifierMode() == true);
    REQUIRE(sm.ShouldToggleCapsLock() == false);  // CapsLock UP 后也不切换
}
```

---

> **下一步**: 基于 Phase 1 硬编码方案开始编码 — keyboard_hook + state_machine + input_simulator + tray_icon