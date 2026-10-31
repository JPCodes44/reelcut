#include "effects.h"

#include "exportgraph.h"
#include "project.h"

#include <QtMath>

namespace Effects {

const QVector<Kind> &kinds() {
    // previewMode numbers must match the branches in src/shaders/effect.frag.
    static const QVector<Kind> list = {
        {"shake", "Shake", 0, true},
        {"zoom-pulse", "Zoom Pulse", 1, true},
        {"strobe", "Strobe", 2, true},
        {"vhs", "VHS", 3, false},
        {"rgb-split", "RGB Split", 4, false},
        {"glitch", "Glitch", 5, true},
        {"vignette", "Vignette", 6, false},
        {"blur", "Blur", 7, false},
        {"grain", "Film Grain", 8, false},
        {"mono-flicker", "Mono Flicker", 9, true},
        {"invert", "Invert", 10, false},
        {"mirror", "Mirror", 11, false},
        {"light-trails", "Light Trails", 12, false},
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

QStringList exportChain(const Effect &e, const QString &in, const QString &out, int serial) {
    const double i = qBound(0.0, e.intensity, 1.0);
    const double s = qBound(0.0, e.speed, 1.0);
    // Local time inside the effect, for expressions that animate.
    const QString T = QString("(t-%1)").arg(num(e.start));
    const QString base = QString("[fxb%1]").arg(serial);
    const QString work = QString("[fxw%1]").arg(serial);
    const QString done = QString("[fxd%1]").arg(serial);

    QString chain;
    QString enable = QString("between(t,%1,%2)").arg(num(e.start), num(e.end));
    if (e.kind == "shake") {
        // Scale up a little, then move the crop window on two sine waves.
        const double amp = 10 + 40 * i;
        const double freq = 12 + 40 * s;
        chain = QString("scale=%1:%2,crop=%3:%4:x='(iw-ow)/2+%5*sin(%6*%7)':y='(ih-oh)/2+%5*cos(%6*%7*1.37)'")
                    .arg(qRound(Project::Width * 1.1) / 2 * 2).arg(qRound(Project::Height * 1.1) / 2 * 2)
                    .arg(Project::Width).arg(Project::Height).arg(num(amp), T, num(freq));
    } else if (e.kind == "zoom-pulse") {
        // zoompan: the zoom swells on a sine; centred.
        const double depth = 0.05 + 0.25 * i;
        const double rate = 0.5 + 3.5 * s;
        chain = QString("zoompan=z='1+%1*abs(sin(PI*%2*(in_time-%3)))':x='iw/2-iw/zoom/2':y='ih/2-ih/zoom/2':d=1:s=%4x%5:fps=%6")
                    .arg(num(depth), num(rate), num(e.start)).arg(Project::Width).arg(Project::Height).arg(Project::Fps);
    } else if (e.kind == "strobe") {
        // Brightness spikes that fall off quickly, `rate` times a second.
        const double rate = 1 + 7 * s;
        chain = QString("eq=brightness='%1*exp(-pow(mod(%2*%3,1)*6,2))':eval=frame").arg(num(0.2 + 0.6 * i), T, num(rate));
    } else if (e.kind == "vhs") {
        const int shift = qMax(1, qRound(3 + 9 * i));
        chain = QString("rgbashift=rh=%1:bh=-%1,eq=saturation=1.25,noise=alls=%2:allf=t,"
                        "drawgrid=w=iw:h=4:t=1:color=black@%3")
                    .arg(shift).arg(qRound(8 + 22 * i)).arg(num(0.15 + 0.35 * i, 2));
    } else if (e.kind == "rgb-split") {
        const int shift = qMax(1, qRound(6 + 30 * i));
        chain = QString("rgbashift=rh=-%1:bh=%1:gv=%2").arg(shift).arg(shift / 3);
    } else if (e.kind == "glitch") {
        // Jumps: the picture slides sideways on random frames, colors split.
        const double amp = 30 + 120 * i;
        const double rate = 4 + 20 * s;
        chain = QString("scale=%1:%2,crop=%3:%4:x='(iw-ow)/2+%5*(random(1)-0.5)*gt(random(2),0.6)':"
                        "y='(ih-oh)/2+%6*gt(mod(%7*%8,1),0.85)',rgbashift=rh=%9:bh=-%9")
                    .arg(qRound(Project::Width * 1.2) / 2 * 2).arg(qRound(Project::Height * 1.1) / 2 * 2)
                    .arg(Project::Width).arg(Project::Height).arg(num(amp), num(amp / 3), T, num(rate))
                    .arg(qRound(4 + 12 * i));
    } else if (e.kind == "vignette") {
        chain = QString("vignette=angle=%1").arg(num(0.25 + 0.55 * i, 3));
    } else if (e.kind == "blur") {
        chain = QString("gblur=sigma=%1").arg(num(2 + 28 * i, 1));
    } else if (e.kind == "grain") {
        chain = QString("noise=alls=%1:allf=t+u").arg(qRound(6 + 34 * i));
    } else if (e.kind == "mono-flicker") {
        // Black and white, switched on and off: the enable expression does it.
        const double rate = 1 + 9 * s;
        chain = QString("hue=s=0,eq=contrast=%1").arg(num(1.0 + 0.4 * i));
        enable += QString("*lt(mod(%1*%2,1),0.5)").arg(T, num(rate));
    } else if (e.kind == "invert") {
        // Each channel moves toward its opposite by the intensity:
        // v + i * (max - 2v) is v at 0 and max - v at 1.
        const QString mix = QString("'val+%1*(maxval-2*val)'").arg(num(i, 3));
        chain = QString("lutrgb=r=%1:g=%1:b=%1,format=yuv420p").arg(mix);
    } else if (e.kind == "mirror") {
        // Left half, flipped onto the right.
        return {QString("%1split[fxm%2][fxs%2]").arg(in).arg(serial),
                QString("[fxs%1]crop=iw/2:ih:0:0,split[fxl%1][fxr%1]").arg(serial),
                QString("[fxr%1]hflip[fxf%1]").arg(serial),
                QString("[fxl%1][fxf%1]hstack%2").arg(serial).arg(done),
                QString("[fxm%1]%2overlay=enable='%3'%4").arg(serial).arg(done, enable, out)};
    } else if (e.kind == "light-trails") {
        // Bright pixels fade slowly instead of vanishing: lagfun keeps the
        // brightest recent value per pixel.
        chain = QString("lagfun=decay=%1").arg(num(0.9 + 0.08 * i, 3));
    } else {
        return {QString("%1null%2").arg(in, out)};
    }

    QStringList statements{QString("%1split%2%3").arg(in, base, work), QString("%1%2%3").arg(work, chain, done)};
    return statements << QString("%1%2overlay=enable='%3'%4").arg(base, done, enable, out);
}

}  // namespace Effects
