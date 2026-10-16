#pragma once
#include <QString>
#include <QByteArray>
namespace AimeCode {
QString digest(quint32 serial);
QString fromSerial(const QString &serial, QString *error = nullptr);
QString serialFromCode(const QString &code, QString *error = nullptr);
QString fromBcd(const QByteArray &bytes);
QString fromMifare(const QByteArray &bytes, QString *error = nullptr);
QString derivedFromIdm(const QByteArray &idm);
}
