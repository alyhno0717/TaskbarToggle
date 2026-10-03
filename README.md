# TaskbarToggle v1.0.2

Windows 任务栏显示 / 隐藏切换工具，支持自定义快捷键和开机启动。当前版本面向 Windows 11 原生任务栏、单显示器环境；成品为 x64 程序。

## 下载与使用

[下载 TaskbarToggle-v1.0.2.exe](https://github.com/alyhno0717/TaskbarToggle/raw/refs/heads/main/TaskbarToggle-v1.0.2.exe)

运行 EXE 后，默认按 **Alt + Z** 切换任务栏。通过托盘菜单可打开设置、调整快捷键或启用开机启动。配置保存在 `%LOCALAPPDATA%\TaskbarToggle\config.ini`。

## 仓库内容

- `TaskbarToggle-v1.0.2.exe`：原始发布成品。
- `TaskbarToggle-v1.0.2源码/`：C++ 源码、图标、资源文件、编译脚本和测试文件。

保留本地发布包的目录结构及原始文件内容。

## 编译

准备 llvm-mingw x64 工具链，在源码目录执行（将工具链路径替换为实际路径）：

```powershell
cd '.\TaskbarToggle-v1.0.2源码'
.\build.ps1 -Toolchain 'C:\toolchains\llvm-mingw'
```

或在 x64 Native Tools / Developer PowerShell 中使用 MSVC：

```powershell
.\build.ps1 -Compiler MSVC
```

编译输出位于源码目录的 `dist\TaskbarToggle.exe`。源码目录同时包含 `test.ps1` 和 `test-core.ps1`；后者会短暂切换实际桌面任务栏并启动专用 Edge 测试窗口。
