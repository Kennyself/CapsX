/**
 * @file tray_icon.cpp
 * @brief System tray icon management implementation
 */

#include "ui/tray_icon.h"
#include "ui/tray_icon_drawer.h"
#include "core/state_machine.h"
#include "utils/logger.h"

// Tray icon callback message ID
#define WM_TRAYICON (WM_USER + 1)

// Menu command IDs
#define IDM_ENABLE   1001
#define IDM_DISABLE  1002
#define IDM_SETTINGS 1003
#define IDM_ABOUT    1004
#define IDM_EXIT     1005

// Global instance
TrayIcon g_trayIcon;

// Global exit flag (defined in main.cpp)
extern bool g_running;

bool TrayIcon::Create()
{
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = TrayWindowProc;
    wc.lpszClassName = L"CapsXTrayClass";
    if (!RegisterClassExW(&wc))
    {
        g_logger.LogError("RegisterClassExW failed, error: %lu", GetLastError());
        return false;
    }

    // Create message-only window (invisible, no broadcast)
    m_trayWindow = CreateWindowExW(
        0, L"CapsXTrayClass", L"CapsXTray",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        NULL, NULL, NULL
    );
    if (m_trayWindow == nullptr)
    {
        g_logger.LogError("CreateWindowExW failed, error: %lu", GetLastError());
        return false;
    }

    // Initialize tray icon data
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_trayWindow;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;

    // Create initial icon (enabled state)
    m_currentIcon = create_tray_icon(true);
    m_nid.hIcon = m_currentIcon;
    wcscpy_s(m_nid.szTip, L"CapsX - Enabled");

    // Add tray icon
    if (!Shell_NotifyIconW(NIM_ADD, &m_nid))
    {
        g_logger.LogError("Shell_NotifyIconW NIM_ADD failed, error: %lu", GetLastError());
        return false;
    }

    g_logger.LogInfo("Tray icon created");
    return true;
}

void TrayIcon::Remove()
{
    if (m_trayWindow != nullptr)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        if (m_currentIcon != nullptr)
        {
            DestroyIcon(m_currentIcon);
            m_currentIcon = nullptr;
        }
        DestroyWindow(m_trayWindow);
        m_trayWindow = nullptr;
        g_logger.LogInfo("Tray icon removed");
    }
}

void TrayIcon::UpdateIcon(bool active)
{
    m_active = active;

    // Destroy old icon
    if (m_currentIcon != nullptr)
    {
        DestroyIcon(m_currentIcon);
    }

    // Create new icon
    m_currentIcon = create_tray_icon(active);
    m_nid.hIcon = m_currentIcon;

    // Update tooltip text
    wcscpy_s(m_nid.szTip, active ? L"CapsX - Enabled" : L"CapsX - Disabled");

    // Update tray icon
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    g_logger.LogInfo("Tray icon updated: %s", active ? "enabled" : "disabled");
}

LRESULT CALLBACK TrayIcon::TrayWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TRAYICON)
    {
        switch (lParam)
        {
        case WM_LBUTTONUP:
        {
            // Left click: toggle enable/disable
            bool newActive = !g_trayIcon.m_active;
            g_stateMachine.SetEnabled(newActive);
            g_trayIcon.UpdateIcon(newActive);
            break;
        }

        case WM_RBUTTONUP:
        {
            // Right click: popup menu with Enable/Disable checkmarks
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            // Enable item: checked when active
            UINT enableFlags = MF_STRING;
            if (g_trayIcon.m_active)
            {
                enableFlags |= MF_CHECKED;
            }
            AppendMenuW(hMenu, enableFlags, IDM_ENABLE, L"Enable");

            // Disable item: checked when not active
            UINT disableFlags = MF_STRING;
            if (!g_trayIcon.m_active)
            {
                disableFlags |= MF_CHECKED;
            }
            AppendMenuW(hMenu, disableFlags, IDM_DISABLE, L"Disable");

            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings");
            AppendMenuW(hMenu, MF_STRING, IDM_ABOUT,    L"About CapsX");
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT,     L"Exit");

            // Why: SetForegroundWindow ensures menu closes properly on focus loss
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            break;
        }
        }
    }
    else if (uMsg == WM_COMMAND)
    {
        switch (wParam)
        {
        case IDM_ENABLE:
        {
            g_stateMachine.SetEnabled(true);
            g_trayIcon.UpdateIcon(true);
            break;
        }

        case IDM_DISABLE:
        {
            g_stateMachine.SetEnabled(false);
            g_trayIcon.UpdateIcon(false);
            break;
        }

        case IDM_EXIT:
        {
            g_running = false;
            PostQuitMessage(0);
            break;
        }

        case IDM_ABOUT:
        {
            MessageBoxW(NULL,
                L"CapsX v1.0.0\nTurn CapsLock into a powerful modifier key\n\nhttps://github.com/kennyself/CapsX",
                L"About CapsX",
                MB_OK | MB_ICONINFORMATION
            );
            break;
        }

        case IDM_SETTINGS:
        {
            MessageBoxW(NULL, L"Settings UI will be available in Phase 2", L"CapsX", MB_OK | MB_ICONINFORMATION);
            break;
        }
        }
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}