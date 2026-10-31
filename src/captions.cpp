#include "captions.h"

#include <QColor>
#include <QStringList>

#include <cmath>

#include "exportgraph.h"

namespace {
// Text box padding, in pixels at 1080x1920. The preview
// (src/CaptionLayer.qml) draws it the same size.
constexpr int TextBoxPadding = 18;

QString hex2(int v) { return QString("%1").arg(v, 2, 16, QChar('0')).toUpper(); }

// An override tag pair for ASS color n (1 fill, 3 outline/box, 4 shadow):
// \nc&HBBGGRR& and \na&HAA&, where alpha counts up to transparent.
QString colorTag(int n, const QColor &c) {
    return QString("\\%1c&H%2%3%4&\\%1a&H%5&")
        .arg(n).arg(hex2(c.blue()), hex2(c.green()), hex2(c.red()), hex2(255 - c.alpha()));
}

// ASS colors are &HAABBGGRR, alpha 00 opaque.
QString assColor(const QColor &color, int alpha = 0) {
    return QString("&H%1%2%3%4")
        .arg(alpha, 2, 16, QChar('0'))
        .arg(color.blue(), 2, 16, QChar('0'))
        .arg(color.green(), 2, 16, QChar('0'))
        .arg(color.red(), 2, 16, QChar('0'))
        .toUpper();
}

// Inline override colors are &HBBGGRR&, without alpha.
QString tagColor(const QColor &color) {
    return QString("&H%1%2%3&")
        .arg(color.blue(), 2, 16, QChar('0'))
        .arg(color.green(), 2, 16, QChar('0'))
        .arg(color.red(), 2, 16, QChar('0'))
        .toUpper();
}

// Braces start override tags and a backslash starts escapes, so neither may
// pass through from what the user typed.
QString escape(QString text) {
    text.replace('\\', QChar(0x29F5));  // reverse solidus operator looks the same
    text.replace('{', '(').replace('}', ')');
    text.replace('\n', "\\N");
    return text;
}

QString position(double x, double y) {
    return QString("\\an5\\pos(%1,%2)").arg(qRound(x * Project::Width)).arg(qRound(y * Project::Height));
}

QString dialogue(double start, double end, const QString &style, const QString &text) {
    return QString("Dialogue: 0,%1,%2,%3,,0,0,0,,%4\n").arg(assTime(start), assTime(end), style, text);
}
}  // namespace

QString CaptionLine::text() const {
    QStringList parts;
    for (const Word &word : words)
        parts.append(word.text);
    return parts.join(' ');
}

QVector<CaptionLine> groupLines(const QVector<Word> &words, int maxWords, int maxChars, double maxGap) {
    QVector<CaptionLine> lines;
    CaptionLine line;
    int chars = 0;
    for (const Word &word : words) {
        const bool full = line.words.size() >= maxWords
                          || (!line.words.isEmpty() && chars + 1 + word.text.size() > maxChars);
        const bool paused = !line.words.isEmpty() && word.start - line.end > maxGap;
        if (full || paused) {
            lines.append(line);
            line = {};
            chars = 0;
        }
        if (line.words.isEmpty())
            line.start = word.start;
        line.words.append(word);
        line.end = word.end;
        chars += (chars ? 1 : 0) + word.text.size();
    }
    if (!line.words.isEmpty())
        lines.append(line);

    // Each line stays up until the next one starts, unless there's a real
    // pause, so captions don't flicker between words.
    for (int i = 0; i + 1 < lines.size(); ++i)
        if (lines[i + 1].start - lines[i].end <= maxGap)
            lines[i].end = lines[i + 1].start;
    return lines;
}

QString assTime(double seconds) {
    const qint64 cs = qMax<qint64>(0, std::llround(seconds * 100.0));
    return QString("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg((cs / 6000) % 60, 2, 10, QChar('0'))
        .arg((cs / 100) % 60, 2, 10, QChar('0'))
        .arg(cs % 100, 2, 10, QChar('0'));
}

QString captionFont() {
    return QStringLiteral("Noto Sans");
}

QString assDocument(const Project &project, const QString &accent) {
    const QString white = assColor(Qt::white), black = assColor(Qt::black);
    const QString shade = assColor(Qt::black, 0x60);
    const QString font = captionFont();

    // Style: Name, Fontname, Fontsize, Primary, Secondary, Outline, Back,
    // Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle,
    // BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
    auto style = [&](const QString &name, int size, int border, double outline, const QString &back) {
        return QString("Style: %1,%2,%3,%4,%4,%5,%6,-1,0,0,0,100,100,0,0,%7,%8,0,5,60,60,0,1\n")
            .arg(name, font).arg(size).arg(white, black, back).arg(border).arg(outline);
    };

    QString ass;
    ass += "[Script Info]\nScriptType: v4.00+\nWrapStyle: 0\nScaledBorderAndShadow: yes\n";
    ass += QString("PlayResX: %1\nPlayResY: %2\n\n").arg(Project::Width).arg(Project::Height);
    ass += "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
           "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, "
           "Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n";
    ass += style("Caption", 84, 1, 7, shade);
    // BorderStyle 3 draws an opaque box behind the text in the outline color.
    ass += QString("Style: Box,%1,76,%2,%2,%3,%3,-1,0,0,0,100,100,0,0,3,14,0,5,60,60,0,1\n")
               .arg(font, white, assColor(QColor(17, 17, 17), 0x20));
    ass += style("Hook", 96, 1, 8, shade);
    // Text with a box behind it: BorderStyle 3, the box in the outline color.
    ass += QString("Style: HookBox,%1,96,%2,%2,%3,%4,-1,0,0,0,100,100,0,0,3,%5,0,5,60,60,0,1\n")
               .arg(font, white, black, shade).arg(TextBoxPadding);
    ass += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

    // Captions take the style's size times the project's caption scale.
    const auto sized = [&](int size) { return QString("\\fs%1").arg(qRound(size * project.captionScale)); };
    const QString pos = position(project.captionX, project.captionY) + sized(84);
    const QString boxPos = position(project.captionX, project.captionY) + sized(76);
    const QString accentTag = QString("\\c%1").arg(tagColor(QColor(accent)));
    const QString resetTag = QString("\\c%1").arg(tagColor(Qt::white));
    for (const CaptionLine &line : groupLines(project.words)) {
        const QString &captionStyle = project.captionStyle;
        if (captionStyle == "highlight") {
            // The whole line stays up and the spoken word lights up in the accent.
            for (int i = 0; i < line.words.size(); ++i) {
                const double start = i == 0 ? line.start : line.words[i].start;
                const double end = i + 1 < line.words.size() ? line.words[i + 1].start : line.end;
                QStringList parts;
                for (int j = 0; j < line.words.size(); ++j) {
                    const QString word = escape(line.words[j].text.toUpper());
                    parts.append(j == i ? QString("{%1}%2{%3}").arg(accentTag, word, resetTag) : word);
                }
                ass += dialogue(start, end, "Caption", "{" + pos + "}" + parts.join(' '));
            }
        } else if (captionStyle == "pop") {
            // One word at a time, popping in from smaller.
            for (int i = 0; i < line.words.size(); ++i) {
                const double start = i == 0 ? line.start : line.words[i].start;
                const double end = i + 1 < line.words.size() ? line.words[i + 1].start : line.end;
                ass += dialogue(start, end, "Caption",
                                "{" + pos + "\\fscx70\\fscy70\\t(0,90,\\fscx100\\fscy100)}"
                                    + escape(line.words[i].text.toUpper()));
            }
        } else if (captionStyle == "box") {
            ass += dialogue(line.start, line.end, "Box", "{" + boxPos + "}" + escape(line.text()));
        } else {
            ass += dialogue(line.start, line.end, "Caption", "{" + pos + "}" + escape(line.text().toUpper()));
        }
    }

    for (const Overlay &overlay : project.overlays) {
        if (overlay.text.trimmed().isEmpty() || overlay.end <= overlay.start || overlay.style == "fisheye")
            continue;  // fisheye text is an image laid over by the export (src/fisheyetext.h)
        const int x = qRound(overlay.x * Project::Width), y = qRound(overlay.y * Project::Height);
        QString tags = QString("\\an5\\fs%1").arg(overlay.size);
        // Type: font, weight, slant, letter spacing (\fsp, in pixels).
        tags += QString("\\fn%1\\b%2\\i%3").arg(QString(overlay.fontOf()).remove('\\').remove('}'))
                    .arg(overlay.boldOf() ? 1 : 0).arg(overlay.italic ? 1 : 0);
        if (overlay.letterSpacing != 0)
            tags += QString("\\fsp%1").arg(num(overlay.letterSpacing, 1));
        if (overlay.animation == "fade")
            tags += QString("\\pos(%1,%2)\\fad(250,150)").arg(x).arg(y);
        else if (overlay.animation == "pop")
            tags += QString("\\pos(%1,%2)\\fscx40\\fscy40\\t(0,180,\\fscx100\\fscy100)").arg(x).arg(y);
        else if (overlay.animation == "slide")
            tags += QString("\\move(%1,%2,%1,%3,0,250)\\fad(200,0)").arg(x).arg(y + 160).arg(y);
        else
            tags += QString("\\pos(%1,%2)").arg(x).arg(y);
        // Colors: \1 fill, \3 outline (or the box), \4 shadow. ASS alpha counts up to transparent.
        const QColor fill = QColor::fromString(overlay.colorOf("fill"));
        const QColor outline = QColor::fromString(overlay.colorOf("outline"));
        const QColor shadow = QColor::fromString(overlay.colorOf("shadow"));
        const QColor box = QColor::fromString(overlay.colorOf("box"));
        tags += colorTag(1, fill);
        if (box.alpha() > 0)
            tags += colorTag(3, box);  // the box replaces the outline
        else if (outline.alpha() > 0 && overlay.outlineWidthOf() > 0)
            tags += colorTag(3, outline) + QString("\\bord%1").arg(num(overlay.outlineWidthOf(), 1));
        else
            tags += "\\bord0";
        if (shadow.alpha() > 0 && overlay.shadowDistanceOf() > 0)
            tags += colorTag(4, shadow) + QString("\\shad%1").arg(num(overlay.shadowDistanceOf(), 1));
        ass += dialogue(overlay.start, overlay.end, box.alpha() > 0 ? "HookBox" : "Hook",
                        "{" + tags + "}" + escape(overlay.text));
    }
    return ass;
}
