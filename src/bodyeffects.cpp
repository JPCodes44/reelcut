#include "bodyeffects.h"

#include "exportgraph.h"
#include "project.h"

namespace Body {

const QVector<Kind> &kinds() {
    // previewMode numbers must match the branches in src/shaders/body.frag.
    static const QVector<Kind> list = {
        {"background-blur", "Background Blur", 0},
        {"color-pop", "Color Pop", 1},
        {"spotlight", "Spotlight", 2},
        {"neon-outline", "Neon Outline", 3},
        {"backdrop", "Backdrop", 4},
    };
    return list;
}

QStringList ids() {
    QStringList out;
    for (const Kind &k : kinds())
        out << k.id;
    return out;
}

const Kind *find(const QString &id) {
    for (const Kind &k : kinds())
        if (k.id.compare(id, Qt::CaseInsensitive) == 0)
            return &k;
    return nullptr;
}

QStringList exportStatements(const Clip &clip, const QString &in, const QString &mask, const QString &out, int serial) {
    const double a = qBound(0.0, clip.bodyAmount, 1.0);
    const QString n = QString::number(serial);
    // maskedmerge weights each plane by the same plane of the mask, so all
    // three inputs must reach it in planar RGB (gbrp), where the mask's grey
    // is in every plane. Filters that only work in YUV (hue, eq) would
    // otherwise pull the merge into YUV, where the mask's color planes are
    // mid-grey and every pixel comes out half and half. Hence the
    // format=gbrp right before each maskedmerge input.
    QStringList s;
    // The mask runs on the clip's clock: same start, same 30 fps, so the
    // merge pairs frame for frame. settb puts the result back on the 1/30
    // timebase every other clip uses, which xfade insists on.
    const QString clock = QString("fps=%1,settb=1/%1").arg(Project::Fps);
    s << QString("%1setpts=PTS-STARTPTS,%2,scale=%3:%4:flags=bicubic,format=gbrp[bmask%5]")
             .arg(mask, clock).arg(Project::Width).arg(Project::Height).arg(n);

    if (clip.body == "neon-outline") {
        // The mask's edge, softened into a glow, picks a neon color over the picture.
        const QString gain = num(2.0 + 6.0 * a, 2);
        s << QString("%1settb=1/%2,format=gbrp,split[bpic%3][bcol%3]").arg(in).arg(Project::Fps).arg(n);
        s << QString("[bcol%1]lutrgb=r=0:g=235:b=255,format=gbrp[bneon%1]").arg(n);
        s << QString("[bmask%1]edgedetect=low=0.05:high=0.2,gblur=sigma=%2,lutrgb=r='min(val*%3,255)':"
                     "g='min(val*%3,255)':b='min(val*%3,255)',format=gbrp[bglow%1]")
                 .arg(n, num(3 + 6 * a, 1), gain);
        s << QString("[bpic%1][bneon%1][bglow%1]maskedmerge,format=yuv420p,%2%3").arg(n, clock, out);
        return s;
    }

    QString background;
    if (clip.body == "background-blur")
        background = QString("gblur=sigma=%1").arg(num(4 + 36 * a, 1));
    else if (clip.body == "color-pop")
        background = QString("hue=s=%1").arg(num(1.0 - a, 3));
    else if (clip.body == "spotlight")
        background = QString("eq=brightness=%1:saturation=%2").arg(num(-0.35 * a, 3), num(1.0 - 0.5 * a, 3));
    else  // backdrop: the background fades to a deep blue-grey
        background = QString("lutrgb=r='val*%1+%2':g='val*%1+%3':b='val*%1+%4'")
                         .arg(num(1.0 - a, 3), num(24 * a, 1), num(26 * a, 1), num(40 * a, 1));

    // First input where the mask is black (background), second where it's white (person).
    s << QString("%1settb=1/%2,format=gbrp,split[bpic%3][bback%3]").arg(in).arg(Project::Fps).arg(n);
    s << QString("[bback%1]%2,format=gbrp[bbg%1]").arg(n, background);
    s << QString("[bbg%1][bpic%1][bmask%1]maskedmerge,format=yuv420p,%2%3").arg(n, clock, out);
    return s;
}

}  // namespace Body
