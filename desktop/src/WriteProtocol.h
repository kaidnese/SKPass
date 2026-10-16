#pragma once
#include <QByteArray>
#include <QString>
namespace WriteProtocol {
QByteArray request(quint32 id,const QByteArray &uid,const QByteArray &key,const QString &code,QString *error=nullptr,bool writeSegaKey=false);
QByteArray blocks(const QString &code);
struct Result {
    quint32 id=0;quint8 status=0,stage=0,detail=0,verifiedMask=0;
    QByteArray uid,data;
};
bool parse(const QByteArray &payload,Result &result);
QString statusText(const Result &result);
}
