#include "modalusage.h"
#include "publisher.h"
#include <QJsonObject>

double ModalUsage::rateFor(const QString &gpu) {
    const double host = 4 * 0.0000131 + 16 * 0.00000222;
    const QString g = gpu.toUpper();
    if (g.contains("L40S")) return 0.000542 + host;
    if (g.contains("L4")) return 0.000222 + host;
    if (g.contains("A10")) return 0.000306 + host;
    return 0.000542 + host;
}

void ModalUsage::setGpu(const QString &name) {
    if (name == m_gpu) return;
    m_gpu = name;
    emit changed();
}

ModalUsage::ModalUsage(const QString &stateDir, QObject *parent) : QObject(parent), m_stateDir(stateDir) {
    m_tail.setSingleShot(true);
    m_tail.setInterval(kIdleTailSeconds * 1000);
    connect(&m_tail, &QTimer::timeout, this, &ModalUsage::endTail);
    reload();
}

ModalUsage::~ModalUsage() { flushTail(); }

void ModalUsage::reload() {
    const QJsonObject o = Net::load(Net::stateFile(m_stateDir, "modal-usage.json"));
    m_month = o.value("month").toString();
    m_limit = o.contains("limit") ? o.value("limit").toDouble() : 30;
    m_entries = o.value("entries").toArray();
    rollover();
    emit changed();
}

void ModalUsage::rollover() {
    const QString month = now().toUTC().toString("yyyy-MM");
    if (m_month != month) { m_month = month; m_entries = {}; }
}

double ModalUsage::total(const QString &kind) const {
    double s = 0;
    for (const auto &e : m_entries)
        if (e.toObject().value("kind").toString() == kind) s += e.toObject().value("seconds").toDouble();
    return s;
}

double ModalUsage::spent() const {
    double d = 0;
    for (const auto &e : m_entries) d += e.toObject().value("dollars").toDouble();
    return d;
}

int ModalUsage::songCount() const {
    int n = 0;
    for (const auto &e : m_entries) n += e.toObject().value("songs").toInt();
    return n;
}

QString ModalUsage::level() const {
    const double f = fraction();
    return f >= 1 ? "over" : f >= 0.8 ? "warn" : "ok";
}

void ModalUsage::setLimit(double dollars) {
    if (!(dollars >= 0) || dollars == m_limit) return;
    m_limit = dollars;
    save();
    emit changed();
}

void ModalUsage::record(const QString &kind, double seconds, int songs) {
    rollover();
    if (seconds <= 0 && songs == 0) return;
    m_entries.append(QJsonObject{{"at", now().toUTC().toString(Qt::ISODate)}, {"kind", kind},
                                 {"seconds", seconds}, {"songs", songs},
                                 {"gpu", m_gpu}, {"dollars", seconds * ratePerSecond()}});
    save();
    emit changed();
}

void ModalUsage::requestStarted() { flushTail(); }

void ModalUsage::requestFinished(double billedSeconds, int songs) {
    record("busy", billedSeconds, songs);
    m_tailStart = now();
    m_tail.start();
}

void ModalUsage::endTail() {
    if (!m_tailStart.isValid()) return;
    m_tailStart = {};
    m_tail.stop();
    record("idle", kIdleTailSeconds);
}

void ModalUsage::flushTail() {
    if (!m_tailStart.isValid()) return;
    const double s = qBound<qint64>(0, m_tailStart.secsTo(now()), qint64(kIdleTailSeconds));
    m_tailStart = {};
    m_tail.stop();
    record("idle", s);
}

void ModalUsage::save() {
    Net::savePrivate(Net::stateFile(m_stateDir, "modal-usage.json"),
                     {{"month", m_month}, {"limit", m_limit}, {"entries", m_entries}});
    Net::savePrivate(Net::stateFile(m_stateDir, "modal-status.json"),
                     {{"spentDollars", spent()}, {"limitDollars", m_limit}, {"overBudget", level() == "over"},
                      {"songs", songCount()}, {"month", m_month}});
}
