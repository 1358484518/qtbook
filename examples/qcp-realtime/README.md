# QCustomPlot 千万级实时绘制 Demo (Qt5 / qmake)

在同一份 **1000 万点** 数据上对比三种绘制策略,证明「实时刷千万级」的关键是
**每帧只画 ~像素数量级的点**,而不是依赖某个库。

- **A 基线**：把全部 1000 万点交给 QCustomPlot 自带 min/max 自适应采样 → 正确，但每帧重扫 1000 万。
- **B 摘要层 + 实时追加**：增量维护「每 256 点一个 min/max 桶」的摘要，每帧追加 5 万新点并只查询 ~2×宽度 个点 → 每帧耗时 O(宽度)，与总量无关，且**保留尖峰**。
- **C 滚动窗口**：只画最近 5000 个原始点（示波器式）。

## 环境要求
- Qt **5.14 / 5.15**（widgets + printsupport 模块）
- 支持 C++17 的编译器

## 构建 & 运行
```bash
# 本目录已内置 qcustomplot.h / qcustomplot.cpp (QCustomPlot 2.1.1, GPL)
qmake            # Qt5 的 qmake，如 /usr/lib/qt5/bin/qmake 或 qmake -qt=5
make -j

# 有显示器：
./qcp_realtime_demo
# 无显示器（服务器）：
./qcp_realtime_demo -platform offscreen
```
运行后在当前目录生成 A/B/C 三张 PNG，并在终端打印每帧耗时与 FPS。

## 开启 GPU 加速（可选）
编辑 `qcp_realtime_demo.pro`，取消注释：
```pro
QT      += opengl
DEFINES += QCUSTOMPLOT_USE_OPENGL
```
并在代码里 `plot->setOpenGl(true);`。注意 Qt5 的模块名是 `opengl`（Qt6 改成了 `openglwidgets`）。

## 用到你的项目里
`MinMaxSummary` 就是「摘要层」的落地实现：
- `append(y)`：采集回调里每来一个点调用，O(1) 增量维护；
- `queryFull(width)`：定时器（如 60fps）里调用，返回当前全览所需的 ~2×width 个点，喂给 `graph->data()->set(...)` 后 `replot()`。

把预填 1000 万换成你的真实采集、把基准循环换成 `QTimer` 即可。
