# QCustomPlot 千万级实时绘制 Demo (Qt5 / qmake)

证明「实时刷千万级」的关键是 **每帧只画 ~像素数量级的点**，而不是依赖某个库。
本目录含两个工程，共用内置的 `qcustomplot.h/.cpp`（QCustomPlot 2.1.1，GPL）。

## 环境要求
- Qt **5.14 / 5.15**（widgets + printsupport 模块）
- 支持 C++17 的编译器

---

## 工程一：基础对比 Demo —— `qcp_realtime_demo.pro`

在同一份 **1000 万点** 数据上对比三种策略：

- **A 基线**：全部 1000 万交给 QCustomPlot 自带 min/max 自适应采样 → 正确，但每帧重扫 1000 万。
- **B 摘要层 + 实时追加**：增量维护「每 256 点一个 min/max 桶」，每帧追加 5 万新点、只查询 ~2×宽度 个点 → 每帧 O(宽度)，与总量无关，**保留尖峰**。
- **C 滚动窗口**：只画最近 5000 个原始点。

```bash
qmake qcp_realtime_demo.pro && make -j
./qcp_realtime_demo                     # 有屏幕
./qcp_realtime_demo -platform offscreen # 无屏幕
```
实测（Qt5.15，软件渲染）：A ≈ 11ms/帧；**B ≈ 0.44ms/帧**；C ≈ 0.20ms/帧。

---

## 工程二：增强版（环形缓冲 + 双线程）—— `qcp_realtime_threaded.pro`

更接近真实项目的骨架：

- **固定容量环形缓冲**（1000 万）：内存恒定、覆盖最旧，不随采集无限增长；
- **自动淘汰的 min/max 摘要桶**：旧桶随环形覆盖自动 `pop_front`；
- **采集线程（QThread）** 持续高速灌数据，与 **GUI 线程 60fps 定时器**（查询 + 重绘）**解耦**，共享数据用 `QMutex` 保护，采集批量加锁降低竞争；
- 显示窗口始终是「最近 1000 万点」，随数据流动而滚动（10M 规模的示波器式刷新）。

```bash
qmake qcp_realtime_threaded.pro && make -j
./qcp_realtime_threaded                     # 有屏幕
./qcp_realtime_threaded -platform offscreen # 无屏幕
```
实测（Qt5.15，软件渲染，运行 ~3s）：
- 采集线程灌入 **1.04 亿** 样本（~36.6 M/s），**环形缓冲全程恒定 41.3MB / 1000 万点**；
- 渲染 178 帧，每帧绘制 3200 点，**每帧 ~0.54ms（≈1839 FPS 余量）**。

---

## 工程三：实时动态显示（弹窗滚动）—— `qcp_realtime_live.pro`

真正的**动态显示**：弹出一个窗口，波形随采集持续**滚动刷新**，一直运行到你关闭窗口。
标题栏实时显示「已采集点数 / 环形窗口内存 / FPS」。基于环形缓冲 + 双线程，
显示窗口始终是「最近 1000 万点」，随新数据从右侧进入、旧数据从左侧移出。

```bash
qmake qcp_realtime_live.pro && make -j
./qcp_realtime_live                      # 实时窗口（持续滚动，关窗即停）
# 无屏幕时抓帧（用于录制视频），生成 frame_XXXX.png：
./qcp_realtime_live --record 40 -platform offscreen
```

---

## 开启 GPU 加速（可选）
编辑对应 `.pro`，取消注释：
```pro
QT      += opengl
DEFINES += QCUSTOMPLOT_USE_OPENGL
```
并在代码里 `plot->setOpenGl(true);`。注意 Qt5 的模块名是 `opengl`（Qt6 改成了 `openglwidgets`）。
软件渲染下已 >1800fps，通常不必开。

## 接到你的项目里
- `MinMaxSummary` / `RingPlotBuffer::append*()`：放采集回调，O(1) 增量维护；
- `queryFull(width)`：放 `QTimer`（60fps）回调，返回 ~2×width 个点后 `graph->data()->set(...)` + `replot()`；
- 把预填/合成信号换成你的真实采集即可。增强版的 `Acquirer`(QThread) + `RingPlotBuffer`(QMutex) 就是现成的双线程骨架。

## float 精度提醒（仅自撸 OpenGL 时）
顶点传 GPU 用 float、而样本索引到千万级会丢精度；平移到大坐标前先减去视口基准偏移再传。QCustomPlot 内部用 double，无此问题。
