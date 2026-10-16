#pragma once
#include <QByteArray>
#include <QString>
extern "C" {
#include "advanced_protocol.h"
}
namespace ReaderProtocol {
bool target(const QByteArray &,advanced_target_t &);
QByteArray request(const advanced_target_t &,quint32 id,int operation,int address,int count,
                   int flags,int keyType,const QByteArray &key,const QByteArray &data,QString *error);
bool result(const QByteArray &,advanced_result_t &);
QString statusText(int status);
QString subtypeName(int subtype);
QByteArray ndef(const QString &,bool url,int capacity,QString *error);
}
