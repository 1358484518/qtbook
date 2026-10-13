// LIVE dynamic display (Qt5 / qmake):
//   A real window that updates continuously - a scrolling 10M-point scope.
//   * fixed-capacity RING BUFFER (constant memory, overwrites oldest)
//   * incremental min/max SUMMARY ring (old buckets auto-evicted)
//   * ACQUISITION thread (QThread) keeps pushing samples
//   * 60 fps QTimer on the GUI thread: snapshot -> downsample -> replot
//
// Run normally to watch it live (window keeps scrolling until you close it):
//     ./qcp_realtime_live
// Head-less frame capture (used to record a demo video), writes frame_XXXX.png:
//     ./qcp_realtime_live --record 40 -platform offscreen
//
// No Qt6-only API: builds on Qt 5.14 / 5.15.

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
static const qint64 kCap      = 10000000;   // ring capacity (constant memory)
static const int    kBucketN  = 256;        // raw samples per summary bucket
static const int    kPlotW    = 1280;
static const int    kPlotH    = 520;
static const int    kBatch    = 20000;      // samples per acquisition batch
static const int    kPaceUs   = 4000;       // pace producer (~5 M/s) for smooth scroll
static const int    kFrameMs  = 16;         // ~60 fps render tick

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

    void appendBatch(const float* ys, int n) {
        QMutexLocker lock(&mtx_);
        for (int k = 0; k < n; ++k) appendLocked(ys[k]);
    }
    qint64 totalAppended() { QMutexLocker lock(&mtx_); return gEnd_; }
    size_t memoryBytes()   { QMutexLocker lock(&mtx_);
        return raw_.capacity()*sizeof(float) + buckets_.size()*sizeof(Bucket); }

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

        if (span <= 4LL * width || nB == 0) {
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

class Acquirer : public QThread {
public:
    Acquirer(RingPlotBuffer* buf, qint64 startG) : buf_(buf), g_(startG) {}
    void stop() { stop_.store(true); }
    void run() override {
        std::vector<float> batch(kBatch);
        while (!stop_.load()) {
            for (int k = 0; k < kBatch; ++k) batch[size_t(k)] = sample(g_++);
            buf_->appendBatch(batch.data(), kBatch);
            QThread::usleep(kPaceUs);
        }
    }
private:
    RingPlotBuffer*   buf_;
    qint64            g_;
    std::atomic<bool> stop_{false};
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    int recordFrames = 0;                               // --record N : capture mode
    const QStringList args = app.arguments();
    const int ri = args.indexOf("--record");
    if (ri >= 0 && ri + 1 < args.size()) recordFrames = args[ri + 1].toInt();

    QCustomPlot *plot = new QCustomPlot();
    plot->resize(kPlotW, kPlotH);
    plot->setWindowTitle(QStringLiteral("QCustomPlot 实时动态显示 (环形缓冲 1000万点 + 双线程)"));
    plot->setNotAntialiasedElements(QCP::aeAll);
    plot->addGraph();
    plot->graph(0)->setAdaptiveSampling(true);
    plot->graph(0)->setPen(QPen(QColor(45,125,100), 1));
    plot->yAxis->setRange(-180, 180);
    plot->yAxis->setLabel("value");
    plot->xAxis->setLabel("sample index (scrolling)");
    if (recordFrames <= 0) plot->show();                // live window

    RingPlotBuffer buf(kCap, kBucketN);
    {   // pre-fill so the window starts full at 10M
        std::vector<float> b(kBatch);
        qint64 g = 0;
        while (g < kCap) {
            const int n = int(std::min<qint64>(kBatch, kCap - g));
            for (int k = 0; k < n; ++k) b[size_t(k)] = sample(g++);
            buf.appendBatch(b.data(), n);
        }
    }

    Acquirer acq(&buf, kCap);
    acq.start();
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [&]() {
        acq.stop(); acq.wait();
    });

    QElapsedTimer fpsClock; fpsClock.start();
    int    frameCount = 0, recorded = 0;
    double emaFps = 0.0;
    QElapsedTimer last; last.start();

    QTimer render;
    QObject::connect(&render, &QTimer::timeout, [&]() {
        QVector<QCPGraphData> ds = buf.queryFull(kPlotW);
        if (ds.isEmpty()) return;
        const double x0 = ds.first().key, x1 = ds.last().key;
        plot->graph(0)->data()->set(ds, true);
        plot->xAxis->setRange(x0, x1);
        plot->replot(QCustomPlot::rpQueuedReplot);      // coalesced live replot

        // live FPS estimate
        const double dt = last.restart() / 1000.0;
        if (dt > 0) emaFps = emaFps == 0 ? 1.0/dt : emaFps*0.9 + (1.0/dt)*0.1;
        if (++frameCount % 15 == 0)
            plot->setWindowTitle(QString(
                "QCustomPlot 实时动态显示  |  已采集 %1 M 点  |  环形窗口 10M (%2 MB)  |  %3 FPS")
                .arg(buf.totalAppended()/1e6, 0, 'f', 1)
                .arg(buf.memoryBytes()/1e6, 0, 'f', 1)
                .arg(emaFps, 0, 'f', 0));

        if (recordFrames > 0) {                          // offscreen capture mode
            plot->savePng(QString("frame_%1.png").arg(recorded, 4, 10, QChar('0')),
                          kPlotW, kPlotH);
            if (++recorded >= recordFrames) {
                std::printf("captured %d frames\n", recorded);
                app.quit();
            }
        }
    });
    render.start(kFrameMs);

    return app.exec();
}
