#include "waveformprovider.h"

#include "media.h"
#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>
#include <QPainter>
#include <QtEndian>
#include <cstring>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QUrlQuery>
#include <cmath>

QImage waveform::render(const QString &path, double start, double duration, const QSize &size,
                        const QString &color, const std::function<bool()> &cancelled) {
    if (!std::isfinite(start) || !std::isfinite(duration) || start < 0 || duration <= 0
        || size.isEmpty() || size.width() > 2048 || size.height() > 2048
        || !QFileInfo::exists(path) || (cancelled && cancelled()))
        return {};
    const QString ffmpeg = media::toolPath("ffmpeg");
    if (ffmpeg.isEmpty()) return {};
    const QColor tint(color);
    if (!tint.isValid()) return {};
    QProcess process;
    process.start(ffmpeg, {"-hide_banner", "-v", "error", "-nostdin", "-ss",
        QString::number(start, 'f', 9), "-i", path, "-map", "0:a:0", "-vn", "-t",
        QString::number(duration, 'f', 9), "-threads", "1", "-c:a", "pcm_f32le", "-f", "wav", "pipe:1"});
    // Read a streaming WAV header to learn the native sample rate and channels.
    // Only column extrema are retained, so even long files use bounded memory.
    QByteArray pending;
    int channels = 0, rate = 0;
    bool riff = false, data = false, invalid = false;
    struct Peak { float low = 0, high = 0; };
    QVector<Peak> peaks;
    quint64 frame = 0;
    auto consume = [&] {
        pending += process.readAllStandardOutput();
        if (!riff) {
            if (pending.size() < 12) return;
            if (!pending.startsWith("RIFF") || pending.mid(8, 4) != "WAVE") { invalid = true; return; }
            pending.remove(0, 12);
            riff = true;
        }
        while (!data) {
            if (pending.size() < 8) return;
            const quint32 length = qFromLittleEndian<quint32>(pending.constData() + 4);
            if (pending.startsWith("data")) {
                if (channels < 1 || channels > 64 || rate < 1) { invalid = true; return; }
                pending.remove(0, 8);
                peaks.resize(channels * size.width());
                data = true;
                break;
            }
            if (length > 1024 * 1024) { invalid = true; return; }
            if (pending.size() < 8 + qsizetype(length) + (length & 1)) return;
            if (pending.startsWith("fmt ") && length >= 16) {
                channels = qFromLittleEndian<quint16>(pending.constData() + 10);
                rate = qFromLittleEndian<quint32>(pending.constData() + 12);
                if (qFromLittleEndian<quint16>(pending.constData() + 22) != 32) { invalid = true; return; }
            }
            pending.remove(0, 8 + length + (length & 1));
        }
        const int bytesPerFrame = channels * int(sizeof(float));
        const qsizetype frames = pending.size() / bytesPerFrame;
        const double samples = duration * rate;
        for (qsizetype i = 0; i < frames; ++i, ++frame) {
            const double position = frame * double(size.width()) / samples;
            if (position >= size.width()) continue;
            const int first = int(position);
            // Above native sample density, give each sample its own time span.
            const int last = qMin(size.width() - 1,
                qMax(first, int(std::ceil((frame + 1) * double(size.width()) / samples)) - 1));
            for (int channel = 0; channel < channels; ++channel) {
                const quint32 bits = qFromLittleEndian<quint32>(pending.constData() + i * bytesPerFrame + channel * 4);
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                if (!std::isfinite(value)) continue;
                value = qBound(-1.0f, value, 1.0f);
                for (int x = first; x <= last; ++x) {
                    Peak &peak = peaks[channel * size.width() + x];
                    peak.low = qMin(peak.low, value);
                    peak.high = qMax(peak.high, value);
                }
            }
        }
        pending.remove(0, frames * bytesPerFrame);
    };
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const bool finished = process.waitForFinished(25);
        consume();
        if (invalid || (cancelled && cancelled()) || timer.elapsed() > 30000) {
            process.kill();
            process.waitForFinished();
            return {};
        }
        if (finished) break;
        if (process.state() == QProcess::NotRunning) return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !data) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(tint);
    const double band = double(size.height()) / channels;
    for (int channel = 0; channel < channels; ++channel) {
        const double center = (channel + 0.5) * band;
        const double amplitude = qMax(0.0, band / 2 - 1);
        for (int x = 0; x < size.width(); ++x) {
            const Peak &peak = peaks[channel * size.width() + x];
            if (peak.high != 0 || peak.low != 0)
                painter.drawLine(QPointF(x, center - peak.high * amplitude),
                                 QPointF(x, center - peak.low * amplitude));
        }
    }
    return image;
}

namespace {
class WaveformResponse : public QQuickImageResponse, public QRunnable {
public:
    WaveformResponse(WaveformProvider *provider, QString key, QString path, double start,
                     double duration, QSize size, QString color)
        : m_provider(provider), m_key(std::move(key)), m_path(std::move(path)), m_start(start),
          m_duration(duration), m_size(size), m_color(std::move(color)) { setAutoDelete(false); }
    void cancel() override { m_cancelled = true; }
    void run() override {
        auto cancelled = [this] { return m_cancelled.load() || m_provider->stopping(); };
        if (!cancelled()) {
            m_image = m_provider->cached(m_key);
            if (m_image.isNull()) m_image = waveform::render(m_path, m_start, m_duration, m_size, m_color, cancelled);
            if (!m_image.isNull() && !cancelled()) m_provider->store(m_key, m_image);
        }
        emit finished();
    }
    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
private:
    WaveformProvider *m_provider;
    QString m_key, m_path;
    double m_start, m_duration;
    QSize m_size;
    QString m_color;
    QImage m_image;
    std::atomic<bool> m_cancelled = false;
};
}

WaveformProvider::WaveformProvider() : m_cache(32 * 1024) { m_pool.setMaxThreadCount(2); }
WaveformProvider::~WaveformProvider() {
    m_stopping = true;
    // Queued responses still need finished() so the QML engine can release them.
    m_pool.waitForDone();
}
QImage WaveformProvider::cached(const QString &key) const {
    QMutexLocker lock(&m_mutex);
    const QImage *image = m_cache.object(key);
    return image ? *image : QImage();
}
void WaveformProvider::store(const QString &key, const QImage &image) {
    QMutexLocker lock(&m_mutex);
    m_cache.insert(key, new QImage(image), qMax<qsizetype>(1, image.sizeInBytes() / 1024));
}
QQuickImageResponse *WaveformProvider::requestImageResponse(const QString &id, const QSize &size) {
    const QUrlQuery query(id.mid(id.indexOf('?') + 1));
    const QString path = query.queryItemValue("path", QUrl::FullyDecoded);
    const QFileInfo file(path);
    const QSize pixels(qBound(1, size.width(), 2048), qBound(1, size.height(), 2048));
    const QString key = id + QString("@%1x%2:%3:%4").arg(pixels.width()).arg(pixels.height())
        .arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
    auto *response = new WaveformResponse(this, key, path, query.queryItemValue("start").toDouble(),
        query.queryItemValue("duration").toDouble(), pixels,
        query.queryItemValue("color", QUrl::FullyDecoded));
    m_pool.start(response);
    return response;
}
