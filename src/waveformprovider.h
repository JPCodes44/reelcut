#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>
#include <atomic>
#include <functional>

namespace waveform {
QImage render(const QString &path, double start, double duration, const QSize &size,
              const QString &color = "#90baff", const std::function<bool()> &cancelled = {});
}

// Native-audio peak envelopes, tiled at the current zoom and display density.
class WaveformProvider : public QQuickAsyncImageProvider {
public:
    WaveformProvider();
    ~WaveformProvider() override;
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &size) override;
    QImage cached(const QString &key) const;
    void store(const QString &key, const QImage &image);
    bool stopping() const { return m_stopping.load(); }
private:
    mutable QMutex m_mutex;
    QCache<QString, QImage> m_cache;
    QThreadPool m_pool;
    std::atomic<bool> m_stopping = false;
};
