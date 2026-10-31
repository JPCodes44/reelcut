#include "transitions.h"

namespace Transitions {

const QVector<Kind> &kinds() {
    // previewMode numbers must match the branches in src/shaders/transition.frag.
    static const QVector<Kind> list = {
        {"dissolve", "Dissolve", "fade", 0},
        {"dip-black", "Dip to Black", "fadeblack", 1},
        {"flash", "Flash", "fadewhite", 2},
        {"fade-grey", "Fade Through Grey", "fadegrays", 3},
        {"wipe-left", "Wipe Left", "wipeleft", 4},
        {"wipe-up", "Wipe Up", "wipeup", 5},
        {"slide-left", "Slide Left", "slideleft", 6},
        {"slide-up", "Slide Up", "slideup", 7},
        {"cover-left", "Cover", "coverleft", 8},
        {"iris", "Iris", "circleopen", 9},
        {"clock", "Clock", "radial", 10},
        {"zoom", "Zoom", "zoomin", 11},
        {"pixelate", "Pixelate", "pixelize", 12},
        {"blur", "Blur", "hblur", 13},
        {"squeeze", "Squeeze", "squeezeh", 14},
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

}  // namespace Transitions
