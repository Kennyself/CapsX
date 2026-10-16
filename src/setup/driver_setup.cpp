/**
 * @file driver_setup.cpp
 * @brief 首次运行时自动安装 Interception 键盘驱动
 *
 * 安装器内置在 exe 资源里。普通用户双击后只需确认，
 * 程序自己提权、调用 install-interception /install，并安排重启。
 * 安全启动无法用程序关掉。不主动重启进固件，也不打开测试签名。
 * 只有驱动已经安装、电脑也重启过，却仍然连不上时，才提示用户自己关闭安全启动。
 */

#include "setup/driver_setup.h"
#include "setup/resource.h"
#include "utils/logger.h"

#include <Windows.h>
#include <shellapi.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{

// 安装后的推进阶段，记在 %APPDATA%\CapsX\setup_state.txt
enum class SetupStage
{
    None,
    NeedReboot
};

enum class SecureBootState
{
    On,
    Off,
    Unknown
};

/**
 * @brief 安装过程中的顶层提示条
 *
 * 驱动安装没有控制台窗口，用这个提示避免用户以为程序卡死。
 */
class StatusBanner
{
public:
    /**
     * @brief 在屏幕中央显示提示
     * @param text 提示文字
     */
    explicit StatusBanner(const wchar_t* text)
    {
        const int width = 480;
        const int height = 96;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
        m_window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"STATIC",
            text,
            WS_POPUP | WS_VISIBLE | WS_BORDER | SS_CENTER | SS_CENTERIMAGE,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (m_window != nullptr)
        {
            UpdateWindow(m_window);
        }
    }

    /**
     * @brief 关闭提示条
     */
    ~StatusBanner()
    {
        if (m_window != nullptr)
        {
            DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

private:
    HWND m_window = nullptr; // 提示条窗口，创建失败时为空
};

/**
 * @brief 判断命令行里是否有指定开关
 * @param cmdLine 进程命令行
 * @param name 开关文本，例如 L"--install-driver"
 * @return true 找到该开关
 */
bool has_switch(const wchar_t* cmdLine, const wchar_t* name)
{
    if (cmdLine == nullptr || name == nullptr)
    {
        return false;
    }
    return wcsstr(cmdLine, name) != nullptr;
}

/**
 * @brief 当前进程是否已经提权
 * @return true 具有管理员权限
 */
bool is_elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        return false;
    }

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

/**
 * @brief 弹出提示框
 * @param text 正文
 * @param type MessageBox 按钮和图标
 * @return MessageBox 的返回值
 */
int show_message(const wchar_t* text, UINT type)
{
    return MessageBoxW(nullptr, text, L"CapsX", type | MB_SETFOREGROUND | MB_TOPMOST);
}

/**
 * @brief 把控制台输出附到提示文字后面
 * @param text 说明
 * @param output 进程输出，过长时截断
 * @return 拼好的正文
 */
std::wstring with_output(const wchar_t* text, const std::wstring& output)
{
    std::wstring message = text;
    if (!output.empty())
    {
        message += L"\n\n";
        if (output.size() > 800)
        {
            message.append(output, 0, 800);
        }
        else
        {
            message += output;
        }
    }
    return message;
}

/**
 * @brief 判断文本里是否包含子串
 * @param text 全文
 * @param needle 要找的子串
 * @return true 包含
 */
bool contains_text(const std::wstring& text, const wchar_t* needle)
{
    return text.find(needle) != std::wstring::npos;
}

/**
 * @brief 把控制台字节按 OEM 代码页转成宽字符
 * @param bytes 进程标准输出
 * @return 可显示的文本
 */
std::wstring decode_console_text(const std::string& bytes)
{
    if (bytes.empty())
    {
        return L"";
    }

    const int byteCount = static_cast<int>(bytes.size());
    const int wideLength = MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), byteCount, nullptr, 0);
    if (wideLength <= 0)
    {
        return L"";
    }

    std::wstring text(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_OEMCP, 0, bytes.data(), byteCount, text.data(), wideLength);
    return text;
}

/**
 * @brief 取 64 位系统目录里的程序路径
 * @param fileName 文件名，例如 L"shutdown.exe"
 * @return 完整路径。32 位进程走 Sysnative，避免打开到 SysWOW64
 */
std::wstring native_system_exe(const wchar_t* fileName)
{
    BOOL wow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64)
    {
        wchar_t windowsDir[MAX_PATH] = {};
        GetWindowsDirectoryW(windowsDir, MAX_PATH);
        return std::wstring(windowsDir) + L"\\Sysnative\\" + fileName;
    }

    wchar_t systemDir[MAX_PATH] = {};
    GetSystemDirectoryW(systemDir, MAX_PATH);
    return std::wstring(systemDir) + L"\\" + fileName;
}

/**
 * @brief 运行程序并收回输出
 * @param application 程序完整路径
 * @param parameters 参数，不含程序名
 * @param workDir 工作目录，空字符串表示不变
 * @param exitCode 进程退出码
 * @param output 标准输出和标准错误
 * @return true 进程在时限内结束
 */
bool run_process(const std::wstring& application, const std::wstring& parameters,
    const std::wstring& workDir, DWORD& exitCode, std::wstring& output)
{
    exitCode = 1;
    output.clear();

    SECURITY_ATTRIBUTES security = {};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
    {
        output = L"无法创建输出管道，错误码 " + std::to_wstring(GetLastError());
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;

    std::wstring command = L"\"" + application + L"\" " + parameters;
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    PROCESS_INFORMATION process = {};
    BOOL created = CreateProcessW(
        nullptr,
        commandBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workDir.empty() ? nullptr : workDir.c_str(),
        &startup,
        &process);
    CloseHandle(writePipe);

    if (!created)
    {
        CloseHandle(readPipe);
        output = L"无法启动进程，错误码 " + std::to_wstring(GetLastError());
        return false;
    }

    std::string bytes;
    const ULONGLONG startTick = GetTickCount64();
    bool timedOut = false;
    for (;;)
    {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            char chunk[512] = {};
            const DWORD toRead = available > sizeof(chunk) ? sizeof(chunk) : available;
            DWORD readCount = 0;
            if (!ReadFile(readPipe, chunk, toRead, &readCount, nullptr) || readCount == 0)
            {
                break;
            }
            bytes.append(chunk, chunk + readCount);
            if (bytes.size() > 65536)
            {
                break;
            }
            continue;
        }

        if (WaitForSingleObject(process.hProcess, 100) == WAIT_OBJECT_0)
        {
            break;
        }
        if (GetTickCount64() - startTick > 60000)
        {
            timedOut = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 1000);
            break;
        }
    }

    DWORD available = 0;
    while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0
        && bytes.size() <= 65536)
    {
        char chunk[512] = {};
        const DWORD toRead = available > sizeof(chunk) ? sizeof(chunk) : available;
        DWORD readCount = 0;
        if (!ReadFile(readPipe, chunk, toRead, &readCount, nullptr) || readCount == 0)
        {
            break;
        }
        bytes.append(chunk, chunk + readCount);
    }

    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readPipe);

    output = decode_console_text(bytes);
    if (timedOut)
    {
        output += L"\n进程超时。";
        return false;
    }
    return true;
}

/**
 * @brief 读取安全启动状态
 * @return On 已开启，Off 已关闭，Unknown 读不到
 */
SecureBootState query_secure_boot()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
        L"UEFISecureBootEnabled",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    if (status != ERROR_SUCCESS)
    {
        return SecureBootState::Unknown;
    }
    return value != 0 ? SecureBootState::On : SecureBootState::Off;
}

/**
 * @brief 判断官方安装器是否已经注册过键盘过滤驱动
 * @return true 服务显示名是 Interception 的 Keyboard Upper Filter Driver
 */
bool is_driver_registered()
{
    wchar_t displayName[128] = {};
    DWORD size = sizeof(displayName);
    const LSTATUS status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\keyboard",
        L"DisplayName",
        RRF_RT_REG_SZ,
        nullptr,
        displayName,
        &size);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }
    return wcscmp(displayName, L"Keyboard Upper Filter Driver") == 0;
}

/**
 * @brief 取得安装阶段记录文件路径，并确保目录存在
 * @return 完整路径。APPDATA 不可用时返回空字符串
 */
std::wstring stage_file_path()
{
    wchar_t appData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return L"";
    }

    const std::wstring dir = std::wstring(appData) + L"\\CapsX";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\setup_state.txt";
}

/**
 * @brief 读取上次安装是否还在等待重启
 * @return NeedReboot 表示驱动已安装但还没确认能用。没有记录时为 None
 */
SetupStage read_stage()
{
    const std::wstring path = stage_file_path();
    if (path.empty())
    {
        return SetupStage::None;
    }

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return SetupStage::None;
    }
    return SetupStage::NeedReboot;
}

/**
 * @brief 记下“驱动已安装，等待重启”
 */
void write_stage()
{
    const std::wstring path = stage_file_path();
    if (path.empty())
    {
        return;
    }

    const char* text = "reboot";
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        g_logger.LogWarning("Unable to write setup state");
        return;
    }

    const DWORD size = static_cast<DWORD>(strlen(text));
    DWORD written = 0;
    WriteFile(file, text, size, &written, nullptr);
    CloseHandle(file);
}

/**
 * @brief 判断这次开机是否晚于阶段文件的写入时间
 * @param path 阶段记录文件
 * @return true 当前系统是在该文件写完之后启动的
 *
 * 安装后必须重启驱动才会加载。用开机时间和文件时间比较，
 * 避免已经重启过却再次要求重启。
 */
bool rebooted_since_file(const std::wstring& path)
{
    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    FILETIME writeTime = {};
    const BOOL gotTime = GetFileTime(file, nullptr, nullptr, &writeTime);
    CloseHandle(file);
    if (!gotTime)
    {
        return false;
    }

    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);

    ULARGE_INTEGER nowValue = {};
    nowValue.LowPart = now.dwLowDateTime;
    nowValue.HighPart = now.dwHighDateTime;

    const ULONGLONG uptime100ns = GetTickCount64() * 10000ULL;
    if (nowValue.QuadPart <= uptime100ns)
    {
        return false;
    }

    ULARGE_INTEGER boot = {};
    boot.QuadPart = nowValue.QuadPart - uptime100ns;

    ULARGE_INTEGER written = {};
    written.LowPart = writeTime.dwLowDateTime;
    written.HighPart = writeTime.dwHighDateTime;
    return boot.QuadPart > written.QuadPart;
}

/**
 * @brief 安装记录写出后，电脑是否已经重启过
 * @return true 已经重启。没有记录或还在同一次开机中则为 false
 */
bool rebooted_after_install()
{
    const std::wstring path = stage_file_path();
    if (path.empty())
    {
        return false;
    }
    return rebooted_since_file(path);
}

/**
 * @brief 删除安装阶段记录
 */
void clear_stage()
{
    const std::wstring path = stage_file_path();
    if (!path.empty())
    {
        DeleteFileW(path.c_str());
    }
}

/**
 * @brief 取路径所在目录
 * @param path 文件完整路径
 * @return 目录路径，没有分隔符时返回空字符串
 */
std::wstring parent_dir(const std::wstring& path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return L"";
    }
    return path.substr(0, pos);
}

/**
 * @brief 把内置安装器释放到临时目录
 * @param outPath 成功时写入 exe 路径
 * @return true 写出了完整文件
 */
bool extract_installer(std::wstring& outPath)
{
    outPath.clear();
    // RT_RCDATA 的值是 10。未定义 UNICODE 时，MinGW 的 RT_RCDATA 宏是窄字符指针，不能传给 FindResourceW。
    HRSRC resource = FindResourceW(
        nullptr,
        MAKEINTRESOURCEW(IDR_INTERCEPTION_INSTALLER),
        MAKEINTRESOURCEW(10));
    if (resource == nullptr)
    {
        return false;
    }

    HGLOBAL loaded = LoadResource(nullptr, resource);
    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (data == nullptr || size < 400000)
    {
        return false;
    }

    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempDir) == 0)
    {
        return false;
    }

    const std::wstring dir = std::wstring(tempDir) + L"CapsX";
    CreateDirectoryW(dir.c_str(), nullptr);

    for (int attempt = 0; attempt < 5; ++attempt)
    {
        std::wstring path = dir + L"\\install-interception.exe";
        if (attempt > 0)
        {
            path = dir + L"\\install-interception-" + std::to_wstring(attempt) + L".exe";
        }

        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        DWORD written = 0;
        const BOOL ok = WriteFile(file, data, size, &written, nullptr);
        CloseHandle(file);
        if (ok && written == size)
        {
            outPath = path;
            return true;
        }
        DeleteFileW(path.c_str());
    }
    return false;
}

/**
 * @brief 判断安装器输出是否表示安装成功
 * @param exitCode 进程退出码
 * @param output 安装器输出
 * @return true 可以继续重启
 */
bool installer_succeeded(DWORD exitCode, const std::wstring& output)
{
    const bool hasError = contains_text(output, L"Could not")
        || contains_text(output, L"not supported")
        || contains_text(output, L"Error ");
    if (hasError)
    {
        return false;
    }
    if (contains_text(output, L"successfully installed"))
    {
        return true;
    }
    return exitCode == 0;
}

/**
 * @brief 正常重启电脑
 * @param output 失败时的命令输出
 * @return true 重启命令已接受
 */
bool reboot_windows(std::wstring& output)
{
    const wchar_t* parameters = L"/r /t 5 /c \"CapsX 驱动安装完成，正在重启\"";
    DWORD exitCode = 1;
    const bool ran = run_process(native_system_exe(L"shutdown.exe"), parameters, L"", exitCode, output);
    g_logger.LogInfo("Reboot requested, exit %lu", exitCode);
    return ran && exitCode == 0;
}

/**
 * @brief 提示用户手动重启
 * @param output 自动重启失败时的输出，可以为空
 */
void show_manual_reboot(const std::wstring& output)
{
    const std::wstring message = with_output(L"自动重启失败，请手动重启电脑后再运行 CapsX.exe。", output);
    show_message(message.c_str(), MB_OK | MB_ICONWARNING);
}

/**
 * @brief 重启后驱动仍然连不上时的说明
 *
 * 安全启动开着时只说明这一件事。已经关闭时再提示内存完整性，
 * 避免把还能用的电脑带进测试签名或固件重启。
 */
void show_load_failed()
{
    const SecureBootState secureBoot = query_secure_boot();
    g_logger.LogWarning("Driver registered but unavailable, secure boot %d", static_cast<int>(secureBoot));
    if (secureBoot == SecureBootState::On)
    {
        show_message(
            L"键盘驱动已安装，但这台电脑的安全启动（Secure Boot）还开着，驱动无法加载。\n\n"
            L"请重启电脑，进入主板设置，关闭 Secure Boot 并保存。\n"
            L"回到 Windows 后再运行 CapsX.exe。",
            MB_OK | MB_ICONWARNING);
        return;
    }

    const wchar_t* text = secureBoot == SecureBootState::Unknown
        ? L"键盘驱动已安装，但 CapsX 仍然连不上驱动。\n\n"
          L"请依次检查：\n"
          L"1. 已经重启过电脑\n"
          L"2. 主板设置里的安全启动（Secure Boot）已经关闭\n"
          L"3. Windows 安全中心 → 设备安全性 → 内核隔离，关闭“内存完整性”后再重启\n"
          L"4. 右键 CapsX.exe，选择“以管理员身份运行”"
        : L"键盘驱动已安装，但 CapsX 仍然连不上驱动。\n\n"
          L"请依次检查：\n"
          L"1. 已经重启过电脑\n"
          L"2. Windows 安全中心 → 设备安全性 → 内核隔离，关闭“内存完整性”后再重启\n"
          L"3. 右键 CapsX.exe，选择“以管理员身份运行”";
    show_message(text, MB_OK | MB_ICONWARNING);
}

/**
 * @brief 安装驱动，然后安排一次正常重启
 * @return true 安装成功。用户可以稍后再重启
 */
bool install_driver()
{
    std::wstring installerPath;
    std::wstring output;
    DWORD exitCode = 1;
    bool ran = false;
    {
        StatusBanner banner(L"正在安装键盘驱动，请稍候…");
        if (!extract_installer(installerPath))
        {
            output = L"找不到内置的驱动安装器。请使用完整编译的 CapsX.exe。";
        }
        else
        {
            ran = run_process(installerPath, L"/install", parent_dir(installerPath), exitCode, output);
        }
    }

    if (!installerPath.empty())
    {
        DeleteFileW(installerPath.c_str());
    }

    g_logger.LogInfo("Driver installer finished, ran %d, exit %lu", ran ? 1 : 0, exitCode);
    if (!ran || !installer_succeeded(exitCode, output))
    {
        const std::wstring message = with_output(L"键盘驱动安装失败。", output);
        show_message(message.c_str(), MB_OK | MB_ICONERROR);
        return false;
    }

    write_stage();

    const int answer = show_message(
        L"键盘驱动已安装。\n"
        L"重启之后再运行 CapsX.exe 就能用。\n\n"
        L"是否现在重启？",
        MB_YESNO | MB_ICONINFORMATION);
    if (answer == IDYES)
    {
        std::wstring rebootOutput;
        if (!reboot_windows(rebootOutput))
        {
            show_manual_reboot(rebootOutput);
        }
    }
    return true;
}

/**
 * @brief 驱动已注册但还连不上时，先补一次重启，重启后仍失败再说明原因
 * @return 对应的 SetupAction
 */
SetupAction repair_registered_driver()
{
    // 阶段文件还在、且这次开机早于安装记录：驱动还没机会加载，不能当成安全启动失败。
    if (read_stage() == SetupStage::NeedReboot && !rebooted_after_install())
    {
        const int rebootAnswer = show_message(
            L"键盘驱动已安装，但还不能使用。\n\n"
            L"请先重启电脑，然后再运行 CapsX.exe。\n\n"
            L"是否现在重启？",
            MB_YESNO | MB_ICONINFORMATION);
        if (rebootAnswer == IDYES)
        {
            std::wstring output;
            if (!reboot_windows(output))
            {
                show_manual_reboot(output);
            }
            return SetupAction::ExitOk;
        }
        return SetupAction::ExitFail;
    }

    show_load_failed();
    return SetupAction::ExitFail;
}

/**
 * @brief 第一次安装前的确认
 * @return 对应的 SetupAction
 */
SetupAction prompt_first_install()
{
    const wchar_t* text = L"CapsX 需要先安装键盘驱动，装完必须重启一次。\n\n"
        L"重启之后再打开 CapsX.exe 就能用，不用再执行命令。\n"
        L"这台电脑只需要做一次。\n\n"
        L"是否现在安装？";
    if (show_message(text, MB_YESNO | MB_ICONINFORMATION) != IDYES)
    {
        return SetupAction::ExitFail;
    }
    if (!is_elevated())
    {
        return SetupAction::RelaunchInstall;
    }
    return install_driver() ? SetupAction::ExitOk : SetupAction::ExitFail;
}

} // namespace

bool DriverSetup::IsVerifySwitch(const wchar_t* cmdLine)
{
    return has_switch(cmdLine, L"--verify-installer");
}

bool DriverSetup::VerifyEmbeddedInstaller()
{
    std::wstring path;
    if (!extract_installer(path))
    {
        g_logger.LogError("Embedded installer extract failed");
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        g_logger.LogError("Embedded installer open failed");
        DeleteFileW(path.c_str());
        return false;
    }

    char header[2] = {};
    DWORD readCount = 0;
    const BOOL readOk = ReadFile(file, header, sizeof(header), &readCount, nullptr);
    const DWORD size = GetFileSize(file, nullptr);
    CloseHandle(file);
    DeleteFileW(path.c_str());

    const bool valid = readOk && readCount == 2 && header[0] == 'M' && header[1] == 'Z' && size >= 400000
        && size != INVALID_FILE_SIZE;
    if (valid)
    {
        g_logger.LogInfo("Embedded installer verified, size %lu", size);
    }
    else
    {
        g_logger.LogError("Embedded installer invalid, size %lu", size);
    }
    return valid;
}

void DriverSetup::ClearPendingState()
{
    clear_stage();
}

SetupAction DriverSetup::Prepare(const wchar_t* cmdLine)
{
    if (has_switch(cmdLine, L"--install-driver"))
    {
        if (!is_elevated())
        {
            return SetupAction::RelaunchInstall;
        }
        return install_driver() ? SetupAction::ExitOk : SetupAction::ExitFail;
    }

    if (is_driver_registered())
    {
        return repair_registered_driver();
    }
    return prompt_first_install();
}

bool DriverSetup::RelaunchElevated(const wchar_t* argument)
{
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
    {
        return false;
    }

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = exePath;
    info.lpParameters = argument;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
    {
        g_logger.LogError("Elevated relaunch failed, error %lu", GetLastError());
        return false;
    }

    g_logger.LogInfo("Elevated relaunch started");
    return true;
}
