#pragma once
#include <QByteArray>
#include <QString>
namespace Spad {
bool tablesValid();
QByteArray encrypt(const QByteArray &plain);
QByteArray decrypt(const QByteArray &cipher);
QString accessCode(const QByteArray &cipher, QByteArray *plain=nullptr, QString *error=nullptr);
}
