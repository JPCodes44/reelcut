#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QVariantMap>

// The Songs page's request, every key optional, as ACE-Step /release_task
// form fields plus the attachment files to upload with them.
namespace SongRequest {
struct Form {
    QList<QPair<QString, QString>> fields;
    QString referencePath;  // uploaded as reference_audio
    QString sourcePath;     // uploaded as src_audio
    QString caption;        // prompt + style + image style, as sent
};
Form build(const QVariantMap &request);
// Up to four lowercase words from the prompt (or style) for the folder name.
QString slug(const QVariantMap &request);
}
