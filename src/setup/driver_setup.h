/**
 * @file driver_setup.h
 * @brief 首次运行时自动安装 Interception 键盘驱动
 *
 * CapsX 依赖内核过滤驱动。新电脑不能只靠复制后立刻使用，
 * 但用户不必再手工打开管理员命令行。双击 CapsX.exe 后，
 * 这里负责提权、安装驱动，并在必要时重启。
 * 安全启动无法由程序关闭。只有安装并重启后驱动仍加载失败时，
 * 才提示用户自己到主板设置里关闭。
 *
 * 内置安装器来自 Interception v1.0.1（非商业许可见
 * src/third_party/interception/licenses）。SHA256:
 * E137863A79DA797F08E7A137280FF2A123809044A888FD75CE9C973198915ABE
 */

#ifndef CAPSX_SETUP_DRIVER_SETUP_H_
#define CAPSX_SETUP_DRIVER_SETUP_H_

// 驱动还不可用时，Prepare 让主程序退出或拉起提权进程
enum class SetupAction
{
    RelaunchInstall,       // 以管理员身份安装驱动
    ExitOk,                // 安装步骤已处理，正常退出
    ExitFail               // 用户取消或安装失败
};

class DriverSetup
{
public:
    /**
     * @brief 判断命令行是否为安装器自检
     * @param cmdLine 进程命令行，不含程序路径
     * @return true 表示只检查内置安装器，不安装驱动
     */
    static bool IsVerifySwitch(const wchar_t* cmdLine);

    /**
     * @brief 释放内置安装器并检查是否为有效的 exe
     * @return true 安装器存在且是 PE 文件
     */
    static bool VerifyEmbeddedInstaller();

    /**
     * @brief 清除“等待重启”标记
     *
     * 驱动已经能连上时调用，避免下次启动再提示安装。
     */
    static void ClearPendingState();

    /**
     * @brief 驱动不可用时执行安装或修复
     * @param cmdLine 进程命令行，不含程序路径
     * @return 主程序下一步动作
     */
    static SetupAction Prepare(const wchar_t* cmdLine);

    /**
     * @brief 用管理员权限重新启动 CapsX
     * @param argument 传给新进程的命令行开关
     * @return true 已弹出提权并启动新进程
     */
    static bool RelaunchElevated(const wchar_t* argument);
};

#endif // CAPSX_SETUP_DRIVER_SETUP_H_
