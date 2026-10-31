#include "transcript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

QVector<Word> parseWhisperJson(const QByteArray &json) {
    // Whisper marks music and silence as [BLANK_AUDIO], (music) and the like.
    static const QRegularExpression marker(R"(^[\[(].*[\])]$)");
    QVector<Word> words;
    const QJsonArray segments = QJsonDocument::fromJson(json).object().value("transcription").toArray();
    for (const QJsonValue &value : segments) {
        const QJsonObject segment = value.toObject();
        const QString text = segment.value("text").toString().trimmed();
        if (text.isEmpty() || marker.match(text).hasMatch())
            continue;
        const QJsonObject offsets = segment.value("offsets").toObject();
        Word word{text, offsets.value("from").toDouble() / 1000.0, offsets.value("to").toDouble() / 1000.0};
        // A lone punctuation segment belongs to the word before it.
        if (!text.at(0).isLetterOrNumber() && !words.isEmpty() && text.size() <= 2) {
            words.last().text += text;
            words.last().end = qMax(words.last().end, word.end);
            continue;
        }
        if (word.end <= word.start)
            word.end = word.start + 0.2;
        words.append(word);
    }
    return words;
}
