/**
 * @file string_utils.h
 * @brief VK 码 ↔ 键名映射工具
 *
 * 用于 JSON 配置文件中键名字符串与 Windows 虚拟键码的双向映射。
 * Phase 2 引入 JSON 配置时使用。
 */

#ifndef CAPSX_UTILS_STRING_UTILS_H_
#define CAPSX_UTILS_STRING_UTILS_H_

#include <string>
#include <Windows.h>

/**
 * @brief 将键名字符串转换为虚拟键码
 * @param keyName 键名字符串（如 "E"、"Space"、"Left"）
 * @return 虚拟键码，0 表示未知键名
 */
DWORD key_name_to_vk(const std::string& keyName);

/**
 * @brief 将虚拟键码转换为键名字符串
 * @param vkCode 虚拟键码
 * @return 键名字符串，"" 表示未知键码
 */
std::string vk_to_key_name(DWORD vkCode);

#endif // CAPSX_UTILS_STRING_UTILS_H_