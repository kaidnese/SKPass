#pragma once
#include <QObject>
#include <QByteArray>
class FrameParser : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void feed(const QByteArray &data);
    void reset() {buffer.clear();}
    quint64 badFrames() const {return bad;}
    static quint16 crc16(const QByteArray &data);
signals:
    void frame(quint8 message, QByteArray payload, QByteArray raw);
    void rejected(quint64 total);
private:
    QByteArray buffer;
    quint64 bad = 0;
};
