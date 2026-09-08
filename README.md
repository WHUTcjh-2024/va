# VAL Invite V1

Windows 本地邀请码识别工具的工程骨架。当前完成 P0.1：C++20/CMake 工程、Win32 主窗口、DPI 感知、F8/F9 坐标校准、JSON 配置持久化、模块边界与高精度计时。

界面现可选择目标窗口、拖拽框选 ROI 并预览，F10/F11 分别启动/停止。启动会校验窗口与 ROI，应用线程按配置提升优先级并尝试设置 CPU 亲和性；识别结果通过 `App::onRecognizedText` 进入 Recognizer → Decision → SendInput，失败会提示原因，STOP 后可重新 START Arm。

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

## 本地闭环 benchmark

不要通过 `file://` 打开页面。需要 Node.js 18+：

```powershell
node .\benchmark\server.js
```

浏览器打开 `http://127.0.0.1:8787/stream.html` 和 `http://127.0.0.1:8787/game.html`。在 stream 设置回合数（支持 1000）、Reveal 和 Timeout 后启动；在 game 页输入并 JOIN。邀请码仅由 stream 可视化展示，game 页和桌面程序均不读取 DOM 或 BroadcastChannel 答案。服务端严格按每轮第一次提交记录 `CORRECT`、`EARLY`、`WRONG`、`TIMEOUT`，并可下载含所有明细与 P50/P95/P99 统计的 CSV；动画从中心向两侧展开，轮次间含随机等待。
