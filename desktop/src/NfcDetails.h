#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#define SMARTCARD_DETAIL_MSG 0x12

struct NfcDocument {
    QJsonObject summary;
    QList<QJsonObject> records;
    bool operator==(const NfcDocument &other) const {
        return summary == other.summary && records == other.records;
    }
};

// One bounded document per scan. Transport framing already checks CRC;
// this layer checks JSON types, identity, ordering and document consistency.
class NfcDetails {
public:
    enum class Result { Rejected, Pending, Complete };
    Result feed(const QByteArray &payload, NfcDocument &document, QString *error = nullptr);
    void reset();
    void activated(const QByteArray &uid);
    static QString typeName(const QJsonObject &summary);
    static QString money(qint32 value, const QString &currency);
    static QString warningText(const QString &warning);
private:
    quint32 scan = 0;
    int count = 0;
    QByteArray uid, activeUid;
    QMap<int, QJsonObject> parts;
    bool complete = false;
};
