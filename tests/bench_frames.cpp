// Frame-time harness: opens a window, optionally pre-fills a heavy page
// (2000 long strokes), then drives drawing, panning and zooming through the
// real input path at one input event per frame and reports the frame deltas
// of each phase. Run by hand on the real display; not part of ctest.
//
//   WAYLAND_DISPLAY=... ./bench_frames 2> bench.log
//   BENCH_SAMPLES=0 ./bench_frames   # measure without window MSAA

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>
#include <QtGlobal>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <random>

#include "palette.h"
#include "page.h"
#include "stroke.h"

namespace {

constexpr int kIdleFrames = 90;
constexpr int kDrawFrames = 150;
constexpr int kPanFrames = 180;
constexpr int kZoomFrames = 180;
constexpr int kPrefillFrames = 10; // absorbs the one-off heavy build
constexpr int kHeavyStrokes = 2000;
constexpr int kHeavyPoints = 240;
constexpr qreal kBudgetMs = 1000.0 / 120;
constexpr qreal kMissedMs = 12; // a swapped frame is late once it misses a vsync

// Started before main runs, so first-frame times include Qt's startup.
QElapsedTimer processClock = [] {
    QElapsedTimer timer;
    timer.start();
    return timer;
}();

} // namespace

class Bench : public QObject
{
public:
    Bench(QQuickWindow *window, Page *page)
        : m_window(window), m_page(page)
    {
        connect(window, &QQuickWindow::frameSwapped, this, [this] { onFrame(); });
        m_clock.start();

        const auto idle = [this](int) { m_window->update(); };
        const auto draw = [this](int i) { drawTick(i); };
        const auto pan = [this](int) { wheel(QPoint(0, 120), Qt::NoModifier); };
        const auto zoom = [this](int i) {
            wheel(QPoint(0, i % 8 < 4 ? 120 : -120), Qt::ControlModifier);
        };
        const auto prefill = [this](int i) {
            if (i == 0) {
                m_page->zoomHome();
                prefillHeavy();
            }
        };

        m_phases = {
            {.name = "idle",       .frames = kIdleFrames,    .tick = idle},
            {.name = "draw",       .frames = kDrawFrames,    .tick = draw},
            {.name = "pan",        .frames = kPanFrames,     .tick = pan},
            {.name = "zoom",       .frames = kZoomFrames,    .tick = zoom},
            {.name = "prefill",    .frames = kPrefillFrames, .tick = prefill},
            {.name = "idle-heavy", .frames = kIdleFrames,    .tick = idle},
            {.name = "draw-heavy", .frames = kDrawFrames,    .tick = draw},
            {.name = "pan-heavy",  .frames = kPanFrames,     .tick = pan},
            {.name = "zoom-heavy", .frames = kZoomFrames,    .tick = zoom},
        };
    }

    void start()
    {
        m_last = m_clock.nsecsElapsed() / 1e6;
        m_phase = 0;
        m_frame = 0;
        m_started = true;
        m_page->setTool(Tools::Draw);
        auto *timer = new QTimer(this);
        timer->setInterval(8); // roughly one input event per 120 Hz frame
        connect(timer, &QTimer::timeout, this, [this] { tick(); });
        timer->start();
    }

private:
    struct Phase
    {
        const char *name;
        int frames;
        std::function<void(int)> tick;
        QList<qreal> deltas = {};
    };

    void tick()
    {
        m_phases[m_phase].tick(m_frame);
        m_frame++;
        if (m_frame >= m_phases[m_phase].frames) {
            report(m_phases[m_phase]);
            m_phase++;
            if (m_phase >= m_phases.size()) {
                QCoreApplication::quit();
                return;
            }
            m_frame = 0;
        }
    }

    void onFrame()
    {
        const qreal now = m_clock.nsecsElapsed() / 1e6;
        if (m_last < 0)
            std::fprintf(stderr, "bench: first frame %.1f ms after start\n", now);
        else if (m_started)
            m_phases[m_phase].deltas.append(now - m_last);
        m_last = now;
    }

    void report(const Phase &phase) const
    {
        QList<qreal> deltas = phase.deltas;
        if (deltas.size() < 20)
            return; // setup phases absorb one-off costs instead
        std::sort(deltas.begin(), deltas.end());
        const qreal sum = std::accumulate(deltas.cbegin(), deltas.cend(), 0.0);
        const int late = int(std::count_if(deltas.cbegin(), deltas.cend(),
                                           [](qreal d) { return d > kMissedMs; }));
        std::fprintf(stderr,
                     "bench: %-10s n=%4d avg=%6.2f p95=%6.2f max=%6.2f ms  late: %d\n",
                     phase.name, int(deltas.size()), sum / deltas.size(),
                     deltas[qRound(deltas.size() * 0.95)], deltas.last(), late);
    }

    void drawTick(int i)
    {
        const QPoint start(200, 500);
        if (i == 0) {
            QTest::mouseMove(m_window, start);
            QTest::mousePress(m_window, Qt::LeftButton, {}, start);
            return;
        }
        const QPoint p(200 + i * 12, int(500 + 60 * std::sin(i / 6.0)));
        QTest::mouseMove(m_window, p);
        if (i == kDrawFrames - 1)
            QTest::mouseRelease(m_window, Qt::LeftButton, {}, p);
    }

    void wheel(QPoint angleDelta, Qt::KeyboardModifiers mods)
    {
        const QPointF pos(m_window->width() / 2, m_window->height() / 2);
        QWheelEvent event(pos, pos, QPoint(), angleDelta, Qt::NoButton, mods,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window, &event);
    }

    void prefillHeavy()
    {
        QElapsedTimer build;
        build.start();
        std::mt19937 rng(7);
        std::uniform_real_distribution<qreal> pickX(-3000.0, 9000.0);
        std::uniform_real_distribution<qreal> pickY(-2000.0, 6000.0);
        for (int i = 0; i < kHeavyStrokes; ++i) {
            Stroke *stroke = new Stroke;
            stroke->setColor(i % 3 == 0 ? palette::ink
                             : i % 3 == 1 ? palette::red : palette::blue);
            stroke->setStrokeWidth(2.75);
            QPointF p(pickX(rng), pickY(rng));
            stroke->begin(p);
            for (int j = 1; j < kHeavyPoints; ++j) {
                p += QPointF(8.0, 6.0 * std::sin(j / 5.0 + i));
                stroke->addPoint(p);
            }
            stroke->end();
            m_page->addStroke(stroke);
        }
        std::fprintf(stderr, "bench: prefilled %d strokes x %d points in %lld ms\n",
                     kHeavyStrokes, kHeavyPoints, build.elapsed());
    }

    QQuickWindow *m_window;
    Page *m_page;
    QList<Phase> m_phases;
    QElapsedTimer m_clock;
    qreal m_last = -1;
    int m_phase = 0;
    int m_frame = 0;
    bool m_started = false;
};

int main(int argc, char *argv[])
{
    int samples = 4;
    if (qEnvironmentVariableIsSet("BENCH_SAMPLES"))
        samples = qEnvironmentVariableIntValue("BENCH_SAMPLES");
    QSurfaceFormat format;
    format.setSamples(samples);
    QSurfaceFormat::setDefaultFormat(format);

    QGuiApplication app(argc, argv);
    QQuickWindow window;
    window.resize(1600, 900);
    window.setColor(palette::page);
    auto *page = new Page(window.contentItem());
    page->setSize(QSizeF(window.width(), window.height()));
    QObject::connect(&window, &QQuickWindow::widthChanged, page,
                     [page, &window] { page->setWidth(window.width()); });
    QObject::connect(&window, &QQuickWindow::heightChanged, page,
                     [page, &window] { page->setHeight(window.height()); });
    window.show();

    Bench bench(&window, page);
    QTimer::singleShot(500, &bench, &Bench::start);
    return app.exec();
}
