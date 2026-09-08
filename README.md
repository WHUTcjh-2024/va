# VAL Invite V1

Windows 本地邀请码识别工具的工程骨架。当前完成 P0.1：C++20/CMake 工程、Win32 主窗口、DPI 感知、F8/F9 坐标校准、JSON 配置持久化、模块边界与高精度计时。

当前版本不会捕获直播窗口、识别邀请码或发送按键；这些能力将在 P0.2-P0.6 逐步接入。

## 构建

需要 Visual Studio 的 Desktop development with C++ 工作负载、Windows 10/11 SDK 与 CMake 3.25+。

本机链接器无法在中文构建目录创建 PDB，因此源码可以保留在当前目录，但构建目录请使用 ASCII 路径：

```powershell
cmd /d /c 'call "D:\VS2026\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S "D:\Desktop\抢码脚本" -B "%LOCALAPPDATA%\valinvite-build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build "%LOCALAPPDATA%\valinvite-build"'
```

生成文件：`%LOCALAPPDATA%\valinvite-build\VALInvite.exe`。

## 当前操作

- `F8`：记录鼠标当前位置为邀请码输入框坐标，并写回 `config.json`。
- `F9`：记录鼠标当前位置为 Join 按钮坐标，并写回 `config.json`。

`config.json` 是唯一配置源；识别阈值、ROI、输入坐标、提交方式和性能选项均已定义，后续模块直接复用。
