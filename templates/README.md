# 模板资源

每个字符目录可放置 1--8 个 `.bin` 模板。加载器同时支持部署使用的二进制格式和便于审查、版本管理的基准文本格式。

文本格式为 `VIT1`、一行 `width height bboxX bboxY bboxWidth bboxHeight character backgroundGray`、随后 `width * height` 个 `.`（背景）或 `#`（前景）像素。当前资源是 32×32 基准字形，背景灰度为 20、前景灰度为 235；真实直播样本可直接以同名 `.bin` 替换或追加。

二进制格式：`VIT1`（4 bytes）、六个 little-endian `uint16`（宽、高、bbox x/y/宽/高）、字符（1 byte）、背景灰度（1 byte）及 `width * height` 灰度像素。

## 从真实截图采集模板（开发期工具）

识别器依赖与真实直播字形一致、且分辨率接近实际 ROI 的模板。基准字形只能验证算法，不能证明真实抖音中的识别效果。每看到一个新的真实邀请码，用 `tools/add_template.py` 采集一次：

```powershell
python tools/add_template.py screenshot.png MAM639 114 175 588 73
```

`screenshot.png` 是一张真实抖音直播截图，`MAM639` 是画面里显示的正确邀请码，`x y w h` 是六码文字的紧框（尽量只框住六码文字、四周少量留白）。工具按与 `Recognizer::normalizeSlot()` 完全相同的六等分方式把 ROI 切成 6 个 slot，各自最近邻缩放到 32×32 灰度，自动估计背景灰与笔画包围盒，并写出二进制 VIT1：`templates/<char>/NN.bin`（每字符目录最多 8 个；默认取空位编号，可用 `--index NN` 指定，`-o` 可指定输出根目录）。

同一字符在不同帧/不同直播出现的多个样本会追加为不同文件，识别器对每字符取该字符模板集合中的最佳匹配。请让真实样本与用户最终框选的 ROI 保持相近分辨率与紧框程度，模板的分辨率若与运行时 ROI 内容相差过大，最近邻缩放会丢失细笔画、降低正确率。

## 真实截图离线回归

构建 `VALInviteOffline` 后，可直接用截图、正确邀请码和 ROI 调用真实 C++ `Recognizer`：

```powershell
python tools/test_sample.py screenshot.png MAM639 390 370 70 18
```

Python 只负责 PNG 解码和按运行时公式导出临时灰度 ROI；字符匹配、格式约束、bbox、边缘、背景、置信度与提交 gate 均由生产 C++ 实现。测试使用默认阈值，并输出六码每个 slot 的 best score、second score、margin、bbox 及所有 gate；同一静态图同时模拟连续两帧，只有识别结果等于 ground truth 且两帧提交 gate 通过才输出 `PASS`。
