# VAL Invite V1

完全离线的 Windows 本地邀请码识别工具。使用 WGC 捕获、内嵌 glyph atlas 和 SendInput，不依赖外部字体、模板、OCR 或网络。

界面只保留目标窗口、ROI、提交位置和运行状态。输入框与加入按钮通过可视化拖框定位，F10/F11 启动或停止。

## 构建

需要 Visual Studio 的 Desktop development with C++ 工作负载、Windows 10/11 SDK 与 CMake 3.25+。

本机链接器无法在中文构建目录创建 PDB，因此源码可以保留在当前目录，但构建目录请使用 ASCII 路径：

```powershell
cmd /d /c 'call "D:\VS2026\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake -S "D:\Desktop\抢码脚本" -B "%LOCALAPPDATA%\valinvite-build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release && cmake --build "%LOCALAPPDATA%\valinvite-build"'
```

生成文件位于 `%LOCALAPPDATA%\valinvite-build`。运行时只需 `VALInvite.exe`，用户配置保存在 `%LOCALAPPDATA%\VALInvite\config.json`。

当前便携目录结构：

```text
valinvite-build/
└── VALInvite.exe
```

完整 A-Z / 0-9 atlas 已编译进 EXE；仓库中的字体与 Python 工具仅用于开发期生成和回归。

## 当前操作

- `框选邀请码输入框`：拖框覆盖输入框，保存矩形中心为点击位置（必选）。
- `框选加入按钮`：拖框覆盖 Join 按钮并切换为点击提交；不框选时使用 Enter 提交。

ROI 以目标窗口 Client Area 的归一化比例保存，窗口尺寸变化后重新 START 即可同比例还原。

## 本地闭环 benchmark

不要通过 `file://` 打开页面。需要 Node.js 18+：

```powershell
node .\benchmark\server.js
```

浏览器打开 `http://127.0.0.1:8787/stream.html` 和 `http://127.0.0.1:8787/game.html`。在 stream 设置回合数（支持 1000）、Reveal 和 Timeout 后启动；在 game 页输入并 JOIN。邀请码仅由 stream 可视化展示，game 页和桌面程序均不读取 DOM 或 BroadcastChannel 答案。服务端严格按每轮第一次提交记录 `CORRECT`、`EARLY`、`WRONG`、`TIMEOUT`，并可下载含所有明细与 P50/P95/P99 统计的 CSV；动画从中心向两侧展开，轮次间含随机等待。
