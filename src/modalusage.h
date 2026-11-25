#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <functional>

// This month's estimated Modal spend for the Songs page: billed request time
// plus the 120 s the GPU stays up afterwards, at Modal's list prices.
class ModalUsage : public QObject {
    Q_OBJECT
    Q_PROPERTY(double spent READ spent NOTIFY changed)
    Q_PROPERTY(double limit READ limit WRITE setLimit NOTIFY changed)
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)
    Q_PROPERTY(QString level READ level NOTIFY changed)
    Q_PROPERTY(int songCount READ songCount NOTIFY changed)
    Q_PROPERTY(double busySeconds READ busySeconds NOTIFY changed)
    Q_PROPERTY(double idleSeconds READ idleSeconds NOTIFY changed)
public:
    static constexpr double kRatePerSecond = 0.000222 + 4 * 0.0000131 + 16 * 0.00000222;
    static constexpr int kIdleTailSeconds = 120;

    explicit ModalUsage(const QString &stateDir = {}, QObject *parent = nullptr);
    ~ModalUsage() override;

    double spent() const;
    double limit() const { return m_limit; }
    void setLimit(double dollars);
    double fraction() const { return m_limit > 0 ? spent() / m_limit : 0; }
    QString level() const;
    int songCount() const;
    double busySeconds() const { return total("busy"); }
    double idleSeconds() const { return total("idle"); }

    void requestStarted();                             // closes an open idle tail
    void requestFinished(double billedSeconds, int songs);
    void endTail();                                    // the full tail ran out
    void flushTail();                                  // record the tail so far (quit)
    Q_INVOKABLE void reload();

    std::function<QDateTime()> now = [] { return QDateTime::currentDateTimeUtc(); };

signals:
    void changed();

private:
    void record(const QString &kind, double seconds, int songs = 0);
    void rollover();
    void save();
    double total(const QString &kind) const;
    QString m_stateDir;
    QString m_month;
    double m_limit = 30;
    QJsonArray m_entries;
    QDateTime m_tailStart;
    QTimer m_tail;
};
