# 模板资源

每个字符目录可放置 1--8 个 `.bin` 模板。加载器同时支持部署使用的二进制格式和便于审查、版本管理的基准文本格式。

文本格式为 `VIT1`、一行 `width height bboxX bboxY bboxWidth bboxHeight character backgroundGray`、随后 `width * height` 个 `.`（背景）或 `#`（前景）像素。当前资源是 32×32 基准字形，背景灰度为 20、前景灰度为 235；真实直播样本可直接以同名 `.bin` 替换或追加。

二进制格式：`VIT1`（4 bytes）、六个 little-endian `uint16`（宽、高、bbox x/y/宽/高）、字符（1 byte）、背景灰度（1 byte）及 `width * height` 灰度像素。
