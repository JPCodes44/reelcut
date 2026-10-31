#include "fisheyetext.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace FisheyeText {

QString fontFamily() { return "Liberation Serif"; }

namespace {

// Bilinear sample of a premultiplied ARGB image; transparent outside it.
QRgb sample(const QImage &image, double x, double y) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    double acc[4] = {0, 0, 0, 0};
    for (int dy = 0; dy <= 1; ++dy)
        for (int dx = 0; dx <= 1; ++dx) {
            const int px = x0 + dx, py = y0 + dy;
            if (px < 0 || py < 0 || px >= image.width() || py >= image.height())
                continue;
            const double w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            const QRgb c = reinterpret_cast<const QRgb *>(image.constScanLine(py))[px];
            acc[0] += qAlpha(c) * w;
            acc[1] += qRed(c) * w;
            acc[2] += qGreen(c) * w;
            acc[3] += qBlue(c) * w;
        }
    return qRgba(qRound(acc[1]), qRound(acc[2]), qRound(acc[3]), qRound(acc[0]));
}

// One pass of a horizontal or vertical box blur over an alpha plane.
QVector<float> boxBlur(const QVector<float> &in, int w, int h, int radius, bool horizontal) {
    QVector<float> out(in.size(), 0.0f);
    const int n = horizontal ? w : h, lines = horizontal ? h : w;
    for (int line = 0; line < lines; ++line) {
        auto at = [&](int i) { return horizontal ? in[line * w + i] : in[i * w + line]; };
        float sum = 0;
        for (int i = -radius; i <= radius; ++i)
            sum += i >= 0 && i < n ? at(i) : 0.0f;
        for (int i = 0; i < n; ++i) {
            (horizontal ? out[line * w + i] : out[i * w + line]) = sum / (2 * radius + 1);
            const int add = i + radius + 1, drop = i - radius;
            sum += (add < n ? at(add) : 0.0f) - (drop >= 0 ? at(drop) : 0.0f);
        }
    }
    return out;
}

}  // namespace

QImage render(const Overlay &overlay) {
    QImage frame(Project::Width, Project::Height, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    if (overlay.text.trimmed().isEmpty())
        return frame;

    QFont font(fontFamily());
    font.setPixelSize(overlay.size);
    const int flags = Qt::AlignHCenter | Qt::TextWordWrap;
    // Lines wrap at 94% of the frame, like the preview's Text.
    const QRect block = QFontMetrics(font).boundingRect(QRect(0, 0, qRound(Project::Width * 0.94), 100000), flags,
                                                        overlay.text);
    // A box behind the text widens the block by its padding on every side.
    const QColor fill = QColor::fromString(overlay.colorOf("fill"));
    const QColor outline = QColor::fromString(overlay.colorOf("outline"));
    const QColor box = QColor::fromString(overlay.colorOf("box"));
    const int pad = box.alpha() > 0 ? BoxPadding : 0;
    const QSize core(block.width() + 2 * pad, block.height() + 2 * pad);
    const int w = qCeil(core.width() * Padding), h = qCeil(core.height() * Padding);

    QImage flat(w, h, QImage::Format_ARGB32_Premultiplied);
    flat.fill(Qt::transparent);
    {
        QPainter p(&flat);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        const QRect boxRect((w - core.width()) / 2, (h - core.height()) / 2, core.width(), core.height());
        if (box.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(box);
            p.drawRoundedRect(boxRect, BoxRadius, BoxRadius);
        }
        const QRect textRect = boxRect.adjusted(pad, pad, -pad, -pad);
        p.setFont(font);
        // An outline: the text drawn around itself in a ring, then the fill on top.
        if (outline.alpha() > 0) {
            p.setPen(outline);
            for (int i = 0; i < 16; ++i) {
                const double a = i * 3.14159265 / 8;
                p.drawText(textRect.translated(qRound(OutlineWidth * std::cos(a)), qRound(OutlineWidth * std::sin(a))),
                           flags, overlay.text);
            }
        }
        p.setPen(fill);
        p.drawText(textRect, flags, overlay.text);
    }

    // The lens: each output pixel reads the flat text nearer the centre.
    const double k = MaxBulge * qBound(0.0, overlay.bulge, 1.0);
    QImage lens(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(lens.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double dx = (x + 0.5) / w * 2 - 1, dy = (y + 0.5) / h * 2 - 1;
            const double r2 = dx * dx + dy * dy;
            const double s = r2 < 1 ? 1 - k * (1 - r2) : 1;
            row[x] = sample(flat, (dx * s + 1) / 2 * w - 0.5, (dy * s + 1) / 2 * h - 0.5);
        }
    }

    // A soft shadow under it: the lens's alpha, blurred, offset and tinted.
    const QColor shadowColor = QColor::fromString(overlay.colorOf("shadow"));
    QImage shadow(w, h, QImage::Format_ARGB32_Premultiplied);
    shadow.fill(Qt::transparent);
    if (shadowColor.alpha() > 0) {
        QVector<float> alpha(w * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                alpha[y * w + x] = qAlpha(lens.pixel(x, y)) / 255.0f;
        for (int pass = 0; pass < 2; ++pass) {
            alpha = boxBlur(alpha, w, h, ShadowBlur, true);
            alpha = boxBlur(alpha, w, h, ShadowBlur, false);
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                shadow.setPixel(x, y, qPremultiply(qRgba(shadowColor.red(), shadowColor.green(), shadowColor.blue(),
                                                         qRound(alpha[y * w + x] * shadowColor.alpha()))));
    }

    const int left = qRound(overlay.x * Project::Width - w / 2.0);
    const int top = qRound(overlay.y * Project::Height - h / 2.0);
    QPainter p(&frame);
    p.drawImage(left + ShadowX, top + ShadowY, shadow);
    p.drawImage(left, top, lens);
    return frame;
}

}  // namespace FisheyeText
