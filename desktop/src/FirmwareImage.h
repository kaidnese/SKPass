#pragma once
#include <QByteArray>
#include <QString>

namespace FirmwareImage {
constexpr quint32 Family = 0xC47E5767;
constexpr int MaxSize = 6 * 1024 * 1024;
constexpr const char *Board = "SKPASS-S3-N16R8-UF2-V1";
// Accept only a SKPass application .bin or our application-relative .uf2.
QByteArray prepare(const QByteArray &input, bool isUf2, QString *error);
bool validBootloader(const QByteArray &input,QString *error);
}
