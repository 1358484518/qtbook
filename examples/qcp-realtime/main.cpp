// Real-time 10M-point demo for QCustomPlot on Qt 5 (qmake project).
//
// Idea: plotting 10,000,000 points in "real time" is not about the plotting
// library, but about only ever handing the widget ~pixel-count points per
// frame. We compare two strategies on the SAME 10M dataset, timing each replot
// with QElapsedTimer:
//
//   A) Baseline: give QCustomPlot all 10M points and let its built-in
//      adaptive (min/max-per-pixel) sampling reduce them on every replot.
//      -> Correct, but it must re-scan all 10M each frame.
//
//   B) Summary layer: keep an incrementally-maintained coarse min/max summary
//      (one bucket per N raw samples) plus the raw buffer. Each frame we also
//      APPEND new samples (simulating live acquisition) and then query only
//      ~2*width points to draw.
//      -> Per-frame cost is O(width), independent of the 10M total.
//
//   C) Zoomed-in scrolling window (last 5000 raw samples), drawn directly.
//
// This file uses no Qt6-only API, so it builds on Qt 5.14 / 5.15.
// Head-less run:  ./qcp_realtime_demo -platform offscreen

#include <QApplication>
#include <QElapsedTimer>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "qcustomplot.h"

// ---- Config ---------------------------------------------------------------
static const int    kTotal          = 10000000;  // pre-filled raw samples
static const int    kBucketN        = 256;       // raw samples per summary bucket
static const int    kPlotW          = 1600;      // plot area width (px)
static const int    kPlotH          = 600;
static const int    kFramesA        = 60;        // baseline frames
static const int    kFramesB        = 120;       // summary frames
static const int    kAppendPerFrame = 50000;     // live samples appended / frame

// Synthetic signal: slow sine + fast ripple + occasional sharp spikes.
static inline float sample(qint64 i)
{
    double s = 40.0 * std::sin(i * 2.0 * M_PI / 900000.0)
             + 8.0  * std::sin(i * 2.0 * M_PI / 3000.0);
    if ((i % 250000) == 0) s += 120.0;           // spike: must survive downsampling
    return static_cast<float>(s);
}

// ---- Incremental min/max summary over a growing raw buffer ----------------
struct Bucket { qint64 iMin, iMax; float vMin, vMax; };

class MinMaxSummary {
public:
    void reserveRaw(qint64 cap) { raw_.reserve(cap); }

    // O(1) amortised append; updates the partial current bucket.
    void append(float y) {
        const qint64 i = static_cast<qint64>(raw_.size());
        raw_.push_back(y);
        if (curCount_ == 0) { cur_.iMin = cur_.iMax = i; cur_.vMin = cur_.vMax = y; }
        else {
            if (y < cur_.vMin) { cur_.vMin = y; cur_.iMin = i; }
            if (y > cur_.vMax) { cur_.vMax = y; cur_.iMax = i; }
        }
        if (++curCount_ == kBucketN) { buckets_.push_back(cur_); curCount_ = 0; }
    }

    qint64 count() const { return static_cast<qint64>(raw_.size()); }

    // Full-range query reduced to ~2*width points using the summary buckets.
    // Keeps spikes (per-column global min & max at their true sample index).
    QVector<QCPGraphData> queryFull(int width) const {
        QVector<QCPGraphData> out;
        const qint64 n = count();
        if (n == 0) return out;

        const qint64 nComplete = static_cast<qint64>(buckets_.size());
        const bool   hasPartial = curCount_ > 0;
        const qint64 nBuckets   = nComplete + (hasPartial ? 1 : 0);

        if (n <= 4LL * width) {                  // few points: return raw as-is
            out.reserve(static_cast<int>(n));
            for (qint64 i = 0; i < n; ++i)
                out.append(QCPGraphData(double(i), double(raw_[static_cast<size_t>(i)])));
            return out;
        }

        out.reserve(2 * width);
        for (int col = 0; col < width; ++col) {
            const qint64 b0 = (nBuckets *  qint64(col)     ) / width;
            const qint64 b1 = (nBuckets * (qint64(col) + 1)) / width;
            if (b1 <= b0) continue;
            float vMin =  std::numeric_limits<float>::max();
            float vMax = -std::numeric_limits<float>::max();
            qint64 iMin = -1, iMax = -1;
            for (qint64 b = b0; b < b1; ++b) {
                const Bucket bk = (b < nComplete) ? buckets_[static_cast<int>(b)] : cur_;
                if (bk.vMin < vMin) { vMin = bk.vMin; iMin = bk.iMin; }
                if (bk.vMax > vMax) { vMax = bk.vMax; iMax = bk.iMax; }
            }
            if (iMin < 0) continue;
            if (iMin <= iMax) {                  // emit in ascending key order
                out.append(QCPGraphData(double(iMin), double(vMin)));
                if (iMax != iMin) out.append(QCPGraphData(double(iMax), double(vMax)));
            } else {
                out.append(QCPGraphData(double(iMax), double(vMax)));
                out.append(QCPGraphData(double(iMin), double(vMin)));
            }
        }
        return out;
    }

    const std::vector<float>& raw() const { return raw_; }

private:
    std::vector<float> raw_;
    QVector<Bucket>    buckets_;
    Bucket             cur_;
    int                curCount_ = 0;
};

static double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size()/2];
}
static double maxOf(const std::vector<double>& v) {
    return v.empty() ? 0 : *std::max_element(v.begin(), v.end());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QCustomPlot *plot = new QCustomPlot();
    plot->setGeometry(0, 0, kPlotW, kPlotH);
    plot->setNotAntialiasedElements(QCP::aeAll);          // speed
    plot->addGraph();
    plot->graph(0)->setAdaptiveSampling(true);            // built-in min/max (default)
    plot->graph(0)->setPen(QPen(QColor(45,125,100), 1));  // 1px line
    plot->yAxis->setRange(-180, 180);

    std::printf("== Building %d-point dataset & incremental min/max summary ==\n", kTotal);
    QElapsedTimer build; build.start();
    MinMaxSummary summ;
    summ.reserveRaw(qint64(kTotal) + qint64(kFramesB) * kAppendPerFrame);
    for (qint64 i = 0; i < kTotal; ++i) summ.append(sample(i));
    std::printf("   built in %lld ms (raw floats: %.0f MB)\n",
                (long long)build.elapsed(), (kTotal * sizeof(float)) / 1e6);

    // ---------------- Benchmark A: built-in adaptive sampling on all 10M -----
    std::printf("\n== A) Baseline: QCustomPlot built-in adaptive sampling on ALL %d points ==\n", kTotal);
    {
        QVector<QCPGraphData> all; all.reserve(kTotal);
        for (qint64 i = 0; i < kTotal; ++i)
            all.append(QCPGraphData(double(i), double(summ.raw()[static_cast<size_t>(i)])));
        plot->graph(0)->data()->set(all, true);           // sorted
        plot->xAxis->setRange(0, kTotal);                 // full range visible

        std::vector<double> ms;
        for (int f = 0; f < kFramesA; ++f) {
            QElapsedTimer t; t.start();
            plot->replot(QCustomPlot::rpImmediateRefresh); // synchronous render
            ms.push_back(t.nsecsElapsed() / 1e6);
        }
        std::printf("   drawn points/frame: %d (full 10M rescanned each replot)\n", all.size());
        std::printf("   replot median: %.1f ms  max: %.1f ms  => ~%.1f FPS (median)\n",
                    median(ms), maxOf(ms), 1000.0 / std::max(1e-6, median(ms)));
        plot->savePng("A_builtin_full10M.png", kPlotW, kPlotH);
    }

    // ---------------- Benchmark B: summary layer + live append --------------
    std::printf("\n== B) Summary layer + live append (%d new pts/frame), full range visible ==\n",
                kAppendPerFrame);
    {
        std::vector<double> qms, rms, drawn;
        qint64 nextIdx = kTotal;
        for (int f = 0; f < kFramesB; ++f) {
            for (int k = 0; k < kAppendPerFrame; ++k) summ.append(sample(nextIdx++));

            QElapsedTimer tq; tq.start();
            QVector<QCPGraphData> ds = summ.queryFull(kPlotW);
            qms.push_back(tq.nsecsElapsed() / 1e6);
            drawn.push_back(ds.size());

            plot->graph(0)->data()->set(ds, true);
            plot->xAxis->setRange(0, summ.count());
            QElapsedTimer tr; tr.start();
            plot->replot(QCustomPlot::rpImmediateRefresh);
            rms.push_back(tr.nsecsElapsed() / 1e6);
        }
        const double perFrame = median(qms) + median(rms);
        std::printf("   total points now: %lld\n", (long long)summ.count());
        std::printf("   drawn points/frame (median): %.0f\n", median(drawn));
        std::printf("   downsample median: %.2f ms  (max %.2f)\n", median(qms), maxOf(qms));
        std::printf("   replot     median: %.2f ms  (max %.2f)\n", median(rms), maxOf(rms));
        std::printf("   per-frame  median: %.2f ms  => ~%.0f FPS\n",
                    perFrame, 1000.0 / std::max(1e-6, perFrame));
        plot->savePng("B_summary_full.png", kPlotW, kPlotH);
    }

    // ---------------- Zoomed-in raw path (scrolling window) -----------------
    std::printf("\n== C) Zoomed-in view: last 5000 raw samples (scrolling-window path) ==\n");
    {
        const qint64 n = summ.count();
        const qint64 w = 5000, i0 = std::max<qint64>(0, n - w);
        QVector<QCPGraphData> raw; raw.reserve(static_cast<int>(w));
        for (qint64 i = i0; i < n; ++i)
            raw.append(QCPGraphData(double(i), double(summ.raw()[static_cast<size_t>(i)])));
        plot->graph(0)->data()->set(raw, true);
        plot->xAxis->setRange(i0, n);
        std::vector<double> ms;
        for (int f = 0; f < 60; ++f) {
            QElapsedTimer t; t.start();
            plot->replot(QCustomPlot::rpImmediateRefresh);
            ms.push_back(t.nsecsElapsed() / 1e6);
        }
        std::printf("   drawn points/frame: %d  replot median: %.2f ms => ~%.0f FPS\n",
                    raw.size(), median(ms), 1000.0 / std::max(1e-6, median(ms)));
        plot->savePng("C_zoom_raw.png", kPlotW, kPlotH);
    }

    std::printf("\nDone. PNGs written next to the executable.\n");
    delete plot;
    return 0;
}
