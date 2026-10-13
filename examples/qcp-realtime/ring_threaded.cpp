// Enhanced real-time demo (Qt5 / qmake):
//   * fixed-capacity RING BUFFER  -> constant memory, overwrites oldest
//   * incremental min/max SUMMARY ring -> old buckets auto-evicted
//   * ACQUISITION thread (QThread) continuously appends samples
//   * RENDER on the GUI thread via a 60 fps QTimer: snapshot -> downsample
//     (~2*width points) -> replot
//
// The visible window is always "the most recent kCap samples" (10,000,000),
// which scrolls as new data flows in - like a scope at 10M scale - while
// memory stays flat and per-frame cost stays O(width) regardless of how many
// total samples were acquired.
//
// No Qt6-only API: builds on Qt 5.14 / 5.15.
// Head-less run:  ./qcp_realtime_threaded -platform offscreen

#include <QApplication>
#include <QThread>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QVector>
#include <QtGlobal>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <vector>

#include "qcustomplot.h"

// ---- Config ---------------------------------------------------------------
static const qint64 kCap       = 10000000;  // ring capacity (constant memory)
static const int    kBucketN   = 256;       // raw samples per summary bucket
static const int    kPlotW     = 1600;
static const int    kPlotH     = 600;
static const int    kBatch     = 50000;     // samples per acquisition batch
static const int    kRunMs     = 3000;      // demo duration
static const int    kFrameMs   = 16;        // ~60 fps render tick

static inline float sample(qint64 i)
{
    double s = 40.0 * std::sin(i * 2.0 * M_PI / 900000.0)
             + 8.0  * std::sin(i * 2.0 * M_PI / 3000.0);
    if ((i % 250000) == 0) s += 120.0;      // spike must survive downsampling
    return static_cast<float>(s);
}

// ---- Thread-safe ring buffer + evicting min/max summary -------------------
struct Bucket { qint64 bStart, iMin, iMax; float vMin, vMax; };

class RingPlotBuffer {
public:
    RingPlotBuffer(qint64 cap, int n) : cap_(cap), N_(n), raw_(size_t(cap)) {}

    // Producer side (acquisition thread): append a batch under one lock.
    void appendBatch(const float* ys, int n) {
        QMutexLocker lock(&mtx_);
        for (int k = 0; k < n; ++k) appendLocked(ys[k]);
    }

    qint64 totalAppended() { QMutexLocker lock(&mtx_); return gEnd_; }
    size_t memoryBytes()   { QMutexLocker lock(&mtx_);
        return raw_.capacity()*sizeof(float) + buckets_.size()*sizeof(Bucket); }

    // Consumer side (GUI thread): downsample the whole current ring window to
    // ~2*width points. Copies out a small vector; lock held only briefly.
    QVector<QCPGraphData> queryFull(int width) {
        QMutexLocker lock(&mtx_);
        QVector<QCPGraphData> out;
        const qint64 gEnd   = gEnd_;
        const qint64 gStart = std::max<qint64>(0, gEnd - cap_);
        const qint64 span   = gEnd - gStart;
        if (span <= 0) return out;

        const int nDeque   = static_cast<int>(buckets_.size());
        const bool partial = curCount_ > 0;
        const int nB       = nDeque + (partial ? 1 : 0);

        if (span <= 4LL * width || nB == 0) {           // few points: read raw
            out.reserve(static_cast<int>(span));
            for (qint64 g = gStart; g < gEnd; ++g)
                out.append(QCPGraphData(double(g), double(raw_[size_t(g % cap_)])));
            return out;
        }

        auto bucketAt = [&](int b) -> const Bucket& {
            return (b < nDeque) ? buckets_[size_t(b)] : cur_;
        };

        out.reserve(2 * width);
        for (int col = 0; col < width; ++col) {
            const int b0 = int((qint64(nB) *  col     ) / width);
            const int b1 = int((qint64(nB) * (col + 1)) / width);
            if (b1 <= b0) continue;
            float vMin =  std::numeric_limits<float>::max();
            float vMax = -std::numeric_limits<float>::max();
            qint64 iMin = -1, iMax = -1;
            for (int b = b0; b < b1; ++b) {
                const Bucket& bk = bucketAt(b);
                if (bk.vMin < vMin) { vMin = bk.vMin; iMin = bk.iMin; }
                if (bk.vMax > vMax) { vMax = bk.vMax; iMax = bk.iMax; }
            }
            if (iMin < 0) continue;
            if (iMin <= iMax) {
                out.append(QCPGraphData(double(iMin), double(vMin)));
                if (iMax != iMin) out.append(QCPGraphData(double(iMax), double(vMax)));
            } else {
                out.append(QCPGraphData(double(iMax), double(vMax)));
                out.append(QCPGraphData(double(iMin), double(vMin)));
            }
        }
        return out;
    }

private:
    void appendLocked(float y) {
        const qint64 g = gEnd_;
        raw_[size_t(g % cap_)] = y;
        if (curCount_ == 0) { cur_ = {g, g, g, y, y}; }
        else {
            if (y < cur_.vMin) { cur_.vMin = y; cur_.iMin = g; }
            if (y > cur_.vMax) { cur_.vMax = y; cur_.iMax = g; }
        }
        if (++curCount_ == N_) { buckets_.push_back(cur_); curCount_ = 0; }
        ++gEnd_;

        // Evict summary buckets that fell entirely out of the ring window.
        const qint64 gStart = std::max<qint64>(0, gEnd_ - cap_);
        while (!buckets_.empty() && buckets_.front().bStart + N_ <= gStart)
            buckets_.pop_front();
    }

    qint64 cap_; int N_;
    std::vector<float> raw_;
    std::deque<Bucket> buckets_;
    Bucket cur_{0,0,0,0,0};
    int    curCount_ = 0;
    qint64 gEnd_ = 0;
    QMutex mtx_;
};

// ---- Acquisition thread ---------------------------------------------------
class Acquirer : public QThread {
public:
    Acquirer(RingPlotBuffer* buf, qint64 startG) : buf_(buf), g_(startG) {}
    void stop() { stop_.store(true); }
    void run() override {
        std::vector<float> batch(kBatch);
        while (!stop_.load()) {
            for (int k = 0; k < kBatch; ++k) batch[size_t(k)] = sample(g_++);
            buf_->appendBatch(batch.data(), kBatch);
            QThread::usleep(500);           // pace the producer
        }
    }
private:
    RingPlotBuffer*   buf_;
    qint64            g_;
    std::atomic<bool> stop_{false};
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
    plot->setNotAntialiasedElements(QCP::aeAll);
    plot->addGraph();
    plot->graph(0)->setAdaptiveSampling(true);
    plot->graph(0)->setPen(QPen(QColor(45,125,100), 1));
    plot->yAxis->setRange(-180, 180);

    RingPlotBuffer buf(kCap, kBucketN);

    // Pre-fill the ring so the window starts "full" at 10M.
    std::printf("== Pre-filling ring buffer with %lld points ==\n", (long long)kCap);
    {
        std::vector<float> b(kBatch);
        qint64 g = 0;
        while (g < kCap) {
            const int n = int(std::min<qint64>(kBatch, kCap - g));
            for (int k = 0; k < n; ++k) b[size_t(k)] = sample(g++);
            buf.appendBatch(b.data(), n);
        }
    }
    std::printf("   ring memory: %.1f MB (stays constant from now on)\n",
                buf.memoryBytes() / 1e6);

    // Acquisition thread starts appending beyond the pre-filled data.
    Acquirer acq(&buf, kCap);
    acq.start();

    // Render loop on the GUI thread (60 fps), decoupled from acquisition.
    std::vector<double> qms, rms, drawn;
    QElapsedTimer wall; wall.start();
    QTimer render;
    QObject::connect(&render, &QTimer::timeout, [&]() {
        QElapsedTimer tq; tq.start();
        QVector<QCPGraphData> ds = buf.queryFull(kPlotW);
        qms.push_back(tq.nsecsElapsed() / 1e6);
        if (ds.isEmpty()) return;
        drawn.push_back(ds.size());

        const double x0 = ds.first().key, x1 = ds.last().key;
        plot->graph(0)->data()->set(ds, true);
        plot->xAxis->setRange(x0, x1);
        QElapsedTimer tr; tr.start();
        plot->replot(QCustomPlot::rpImmediateRefresh);
        rms.push_back(tr.nsecsElapsed() / 1e6);

        if (rms.size() % 30 == 0)
            std::printf("   [%4lld ms] frames=%zu  total=%lld  ring=%.1fMB  "
                        "draw=%.0f  q=%.2fms r=%.2fms\n",
                        (long long)wall.elapsed(), rms.size(),
                        (long long)buf.totalAppended(), buf.memoryBytes()/1e6,
                        median(drawn), median(qms), median(rms));
    });
    render.start(kFrameMs);

    // Stop after the demo window, print stats, save a PNG, quit.
    QTimer::singleShot(kRunMs, [&]() {
        acq.stop(); acq.wait();
        const qint64 total = buf.totalAppended();
        const double perFrame = median(qms) + median(rms);
        std::printf("\n== Summary (acquisition + render decoupled) ==\n");
        std::printf("   wall time              : %lld ms\n", (long long)wall.elapsed());
        std::printf("   total samples acquired : %lld  (%.1f M/s)\n",
                    (long long)total, total / (wall.elapsed()/1000.0) / 1e6);
        std::printf("   ring window (constant) : %lld samples, %.1f MB\n",
                    (long long)kCap, buf.memoryBytes()/1e6);
        std::printf("   frames rendered        : %zu\n", rms.size());
        std::printf("   drawn points/frame     : %.0f (median)\n", median(drawn));
        std::printf("   downsample  median     : %.2f ms (max %.2f)\n", median(qms), maxOf(qms));
        std::printf("   replot      median     : %.2f ms (max %.2f)\n", median(rms), maxOf(rms));
        std::printf("   per-frame   median     : %.2f ms => ~%.0f FPS\n",
                    perFrame, 1000.0 / std::max(1e-6, perFrame));
        plot->savePng("ring_threaded.png", kPlotW, kPlotH);
        std::printf("   saved ring_threaded.png\n");
        app.quit();
    });

    return app.exec();
}
