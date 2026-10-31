#include "segment.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <cmath>

#include "bodyeffects.h"
#include "exportgraph.h"
#include "jobs.h"

namespace Segment {

QString modelPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           "/reelcut/models/human_segmentation_pphumanseg_2023mar.onnx";
}

QString modelUrl() {
    return "https://huggingface.co/opencv/human_segmentation_pphumanseg/resolve/main/"
           "human_segmentation_pphumanseg_2023mar.onnx";
}

bool downloadModel(QString *error) {
    return downloadFile(modelUrl(), modelPath(), QObject::tr("the person model"), error);
}

MaskFn loadModel(const QString &path, QString *error) {
    auto net = std::make_shared<cv::dnn::Net>();
    try {
        *net = cv::dnn::readNet(path.toStdString());
    } catch (const cv::Exception &e) {
        *error = QObject::tr("Could not load the person model: %1").arg(QString::fromStdString(e.what()));
        return {};
    }
    if (net->empty()) {
        *error = QObject::tr("Could not load the person model.");
        return {};
    }
    return [net](const unsigned char *rgb, unsigned char *mask) {
        // The model wants RGB scaled to -1..1, as a 1x3x192x192 tensor.
        cv::Mat image(ModelSize, ModelSize, CV_8UC3, const_cast<unsigned char *>(rgb));
        cv::Mat scaled;
        image.convertTo(scaled, CV_32FC3, 2.0 / 255.0, -1.0);
        net->setInput(cv::dnn::blobFromImage(scaled));
        const cv::Mat out = net->forward();  // 1 x 2 x 192 x 192: background, person scores
        const float *background = out.ptr<float>(0, 0);
        const float *person = out.ptr<float>(0, 1);
        for (int i = 0; i < ModelSize * ModelSize; ++i) {
            // Softmax of two scores is a logistic of their difference.
            const float p = 1.0f / (1.0f + std::exp(background[i] - person[i]));
            mask[i] = static_cast<unsigned char>(p * 255.0f + 0.5f);
        }
    };
}

QString maskPath(const Clip &clip) {
    const QFileInfo file(clip.path);
    const QString key = QStringList{clip.path, QString::number(file.size()),
                                    QString::number(file.lastModified().toMSecsSinceEpoch()), num(clip.in, 4),
                                    num(clip.out, 4), num(clip.speed, 4), num(clip.zoom, 4), num(clip.panX, 4),
                                    num(clip.panY, 4), clip.still ? "still" : "video", clip.motion,
                                    QString::number(clip.width), QString::number(clip.height)}
                            .join('|');
    const QString hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/masks/" + hash + ".mp4";
}

bool hasMask(const Clip &clip) {
    return QFileInfo(maskPath(clip)).size() > 0;
}

bool makeMask(const Clip &clip, const MaskFn &fn, const QString &output,
              const std::function<bool(double)> &progress, QString *error) {
    QDir().mkpath(QFileInfo(output).absolutePath());
    const QString part = output + ".part.mp4";

    // Decode: the clip's own export chain, shrunk to the model's input.
    QStringList decodeArgs{"-hide_banner", "-nostdin", "-loglevel", "error"};
    if (clip.still)
        decodeArgs << "-loop" << "1" << "-framerate" << QString::number(Project::Fps) << "-t" << num(clip.duration() + 0.1);
    decodeArgs << "-i" << clip.path << "-filter_complex"
               << clipVideoChain(clip, "[0:v]") + QString(",scale=%1:%1:flags=area,format=rgb24[out]").arg(ModelSize)
               << "-map" << "[out]" << "-f" << "rawvideo" << "-";
    // Encode: grey frames, scaled up to a 9:16 mask video.
    const QStringList encodeArgs{"-hide_banner", "-nostdin", "-loglevel", "error", "-y",
                                 "-f", "rawvideo", "-pix_fmt", "gray", "-s", QString("%1x%1").arg(ModelSize),
                                 "-r", QString::number(Project::Fps), "-i", "-",
                                 "-vf", QString("scale=%1:%2:flags=bicubic,format=yuv420p").arg(MaskWidth).arg(MaskHeight),
                                 "-c:v", "libx264", "-preset", "fast", "-crf", "12", part};
    QProcess decode, encode;
    decode.start("ffmpeg", decodeArgs);
    encode.start("ffmpeg", encodeArgs);
    if (!decode.waitForStarted(10000) || !encode.waitForStarted(10000)) {
        *error = QObject::tr("Could not start ffmpeg.");
        return false;
    }

    const qint64 frameBytes = qint64(ModelSize) * ModelSize * 3;
    const int expected = qMax(1, qRound(clip.duration() * Project::Fps));
    QByteArray frame;
    QByteArray mask(ModelSize * ModelSize, '\0');
    int done = 0;
    bool cancelled = false;
    while (true) {
        while (frame.size() < frameBytes && (decode.bytesAvailable() > 0 || decode.waitForReadyRead(30000)))
            frame += decode.read(frameBytes - frame.size());
        if (frame.size() < frameBytes)
            break;  // end of the clip
        fn(reinterpret_cast<const unsigned char *>(frame.constData()), reinterpret_cast<unsigned char *>(mask.data()));
        encode.write(mask);
        while (encode.bytesToWrite() > (1 << 20))
            encode.waitForBytesWritten(30000);
        frame.clear();
        ++done;
        if (progress && !progress(qMin(1.0, double(done) / expected))) {
            cancelled = true;
            break;
        }
    }
    decode.kill();
    decode.waitForFinished(5000);
    encode.closeWriteChannel();
    const bool encoded = encode.waitForFinished(120000) && encode.exitCode() == 0;
    if (cancelled || !encoded || done == 0) {
        QFile::remove(part);
        if (!cancelled)
            *error = done == 0 ? QObject::tr("Could not read frames from %1.").arg(clip.path)
                               : QObject::tr("Could not write the person mask:\n%1").arg(tail(encode.readAllStandardError()));
        return false;
    }
    QFile::remove(output);
    return QFile::rename(part, output);
}

bool ensureMasks(const Project &project, const std::function<bool(double)> &progress, QString *error) {
    QVector<int> missing;
    for (int i = 0; i < project.clips.size(); ++i)
        if (Body::find(project.clips[i].body) && !hasMask(project.clips[i]))
            missing << i;
    if (missing.isEmpty())
        return true;
    if (!QFileInfo::exists(modelPath()) && !downloadModel(error))
        return false;
    const MaskFn fn = loadModel(modelPath(), error);
    if (!fn)
        return false;
    for (int k = 0; k < missing.size(); ++k) {
        const Clip &clip = project.clips[missing[k]];
        auto step = [&](double f) { return !progress || progress((k + f) / missing.size()); };
        if (!makeMask(clip, fn, maskPath(clip), step, error))
            return false;
    }
    return true;
}

}  // namespace Segment
