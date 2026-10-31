#pragma once

#include <QByteArray>
#include <QVector>

#include "project.h"

// Words from whisper-cli's JSON output (-oj), run with one word per segment
// (-ml 1 -sow). Offsets are milliseconds; non-speech markers are dropped.
QVector<Word> parseWhisperJson(const QByteArray &json);
