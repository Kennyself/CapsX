/**
 * @file tray_icon_drawer.h
 * @brief 托盘图标动态绘制
 *
 * 使用 GDI 在内存中绘制 16x16 托盘图标，无需外部 ICO 文件。
 * 启用时蓝色圆形 + 白色 "CX"，禁用时灰色圆形。
 */

#ifndef CAPSX_UI_TRAY_ICON_DRAWER_H_
#define CAPSX_UI_TRAY_ICON_DRAWER_H_

#include <Windows.h>

/**
 * @brief 创建托盘图标
 * @param active true 启用状态（蓝色），false 禁用状态（灰色）
 * @return HICON 图标句柄，使用后需调用 DestroyIcon 释放
 */
HICON create_tray_icon(bool active);

#endif // CAPSX_UI_TRAY_ICON_DRAWER_H_