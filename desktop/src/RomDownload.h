#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

namespace RomDownload {
struct Plan {
    quint32 address=0, metadataAddress=0, sequence=0;
    QByteArray metadata;
};
struct Options {
    QString firmware, port, tool, firstFlashDirectory, workRoot;
    QStringList previousNativePorts;
    bool firstFlash=false;
};
struct Result {QString error; bool committed=false;};
using Reporter=std::function<void(int,const QString &)>;
bool validLayout(const QByteArray &table,QString *error);
bool planUpdate(const QByteArray &table,const QByteArray &ota,Plan *plan,QString *error);
QStringList nativePorts();
bool isNativeUsb(quint16 vendor,quint16 product);
Result run(const Options &options,const Reporter &report);
QString toolPath();
QString firstFlashPath();
}
