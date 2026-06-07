/**
 * @file string_utils.cpp
 * @brief VK 码 ↔ 键名映射工具实现
 *
 * Phase 1 仅提供基础映射，Phase 2 扩展为完整映射表。
 */

#include "utils/string_utils.h"

#include <unordered_map>

// 键名 → VK 码映射表
static const std::unordered_map<std::string, DWORD> KEY_NAME_TO_VK = {
    // 字母键
    {"A", 0x41}, {"B", 0x42}, {"C", 0x43}, {"D", 0x44},
    {"E", 0x45}, {"F", 0x46}, {"G", 0x47}, {"H", 0x48},
    {"I", 0x49}, {"J", 0x4A}, {"K", 0x4B}, {"L", 0x4C},
    {"M", 0x4D}, {"N", 0x4E}, {"O", 0x4F}, {"P", 0x50},
    {"Q", 0x51}, {"R", 0x52}, {"S", 0x53}, {"T", 0x54},
    {"U", 0x55}, {"V", 0x56}, {"W", 0x57}, {"X", 0x58},
    {"Y", 0x59}, {"Z", 0x5A},
    // 数字键
    {"0", 0x30}, {"1", 0x31}, {"2", 0x32}, {"3", 0x33},
    {"4", 0x34}, {"5", 0x35}, {"6", 0x36}, {"7", 0x37},
    {"8", 0x38}, {"9", 0x39},
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
    {"PageUp",   VK_PRIOR},
    {"PageDown", VK_NEXT},
    {"Insert",   VK_INSERT},
    // 符号键
    {"-", 0xBD}, {"=", 0xBB}, {";", 0xBA},
    {",", 0xBC}, {".", 0xBE}, {"/", 0xBF},
    {"[", 0xDB}, {"]", 0xDD},
    // 功能键
    {"F1",  VK_F1},  {"F2",  VK_F2},  {"F3",  VK_F3},  {"F4",  VK_F4},
    {"F5",  VK_F5},  {"F6",  VK_F6},  {"F7",  VK_F7},  {"F8",  VK_F8},
    {"F9",  VK_F9},  {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
};

DWORD key_name_to_vk(const std::string& keyName)
{
    auto it = KEY_NAME_TO_VK.find(keyName);
    if (it != KEY_NAME_TO_VK.end())
    {
        return it->second;
    }
    return 0;
}

std::string vk_to_key_name(DWORD vkCode)
{
    for (const auto& pair : KEY_NAME_TO_VK)
    {
        if (pair.second == vkCode)
        {
            return pair.first;
        }
    }
    return "";
}