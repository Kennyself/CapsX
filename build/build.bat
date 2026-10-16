@echo off
setlocal EnableExtensions
chcp 65001 >nul
cd /d "%~dp0.."

rem 优先使用已在 PATH 里的 cmake；否则找 WinGet 安装的 WinLibs。
where cmake >nul 2>&1
if errorlevel 1 call :find_winlibs

where cmake >nul 2>&1
if errorlevel 1 (
    echo 找不到 cmake。请先安装 MinGW WinLibs，或把 mingw64\bin 加到 PATH。
    goto :failed
)
where g++ >nul 2>&1
if errorlevel 1 (
    echo 找不到 g++。请确认 MinGW 的 bin 目录已加入 PATH。
    goto :failed
)

if not exist "%~dp0CMakeCache.txt" (
    echo 首次配置 MinGW 工程...
    cmake -S "%~dp0.." -B "%~dp0." -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto :failed
)

echo 开始编译...
cmake --build "%~dp0."
if errorlevel 1 goto :failed

echo.
echo 编译完成: %~dp0CapsX.exe
pause
exit /b 0

:find_winlibs
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do (
    if exist "%%D\mingw64\bin\cmake.exe" (
        set "PATH=%%D\mingw64\bin;%PATH%"
        goto :eof
    )
)
goto :eof

:failed
echo.
echo 编译失败。如果提示 CapsX.exe 正被占用，请先从托盘退出 CapsX，再运行本脚本。
pause
exit /b 1