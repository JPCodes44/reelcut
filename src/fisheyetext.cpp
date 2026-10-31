#include "fisheyetext.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QtMath>

#include <cmath>

namespace FisheyeText {


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

    QFont font(overlay.fontOf());
    font.setPixelSize(overlay.size);
    font.setBold(overlay.boldOf());
    font.setItalic(overlay.italic);
    font.setLetterSpacing(QFont::AbsoluteSpacing, overlay.letterSpacing);
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

    // Sharpness: the lens enlarges the middle about 1.5 times, so the flat
    // text is drawn FlatScale times bigger than it shows, and the lens is
    // worked out at LensScale times the size and then smoothly shrunk (which
    // keeps the edges, where the lens shrinks the text, from going jagged).
    QImage flat(w * FlatScale, h * FlatScale, QImage::Format_ARGB32_Premultiplied);
    flat.fill(Qt::transparent);
    {
        QPainter p(&flat);
        p.scale(FlatScale, FlatScale);  // draw in 1x units, rasterized FlatScale times finer
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
        // (QPainter is scaled, so fractions of a pixel still land FlatScale times finer.)
        const double ring = overlay.outlineWidthOf();
        if (outline.alpha() > 0 && ring > 0) {
            p.setPen(outline);
            // Rings out to the full width, so a thick outline has no gaps.
            for (double r = ring; r > 0.01; r -= 1.5)
                for (int i = 0; i < 24; ++i) {
                    const double a = i * 3.14159265 / 12;
                    p.drawText(QRectF(textRect).translated(r * std::cos(a), r * std::sin(a)), flags, overlay.text);
                }
        }
        p.setPen(fill);
        p.drawText(textRect, flags, overlay.text);
    }

    // The lens: each output pixel reads the flat text nearer the centre.
    const double k = MaxBulge * qBound(0.0, overlay.bulge, 1.0);
    const int lw = w * LensScale, lh = h * LensScale;
    QImage big(lw, lh, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < lh; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(big.scanLine(y));
        for (int x = 0; x < lw; ++x) {
            const double dx = (x + 0.5) / lw * 2 - 1, dy = (y + 0.5) / lh * 2 - 1;
            const double r2 = dx * dx + dy * dy;
            const double s = r2 < 1 ? 1 - k * (1 - r2) : 1;
            row[x] = sample(flat, (dx * s + 1) / 2 * flat.width() - 0.5, (dy * s + 1) / 2 * flat.height() - 0.5);
        }
    }
    const QImage lens = big.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

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
    // The shadow falls down and a little right, as far as the overlay says.
    const double d = overlay.shadowDistanceOf();
    p.drawImage(left + qRound(d * ShadowX), top + qRound(d * ShadowY), shadow);
    p.drawImage(left, top, lens);
    return frame;
}

}  // namespace FisheyeText
