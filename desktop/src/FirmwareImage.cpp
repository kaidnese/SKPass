#include "FirmwareImage.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <cstring>

namespace {
quint32 word(const QByteArray &b, int at) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData()+at));
}
bool imageValid(const QByteArray &b, QString *error,bool application=true) {
    auto reject=[&](const QString &s){if(error)*error=s;return false;};
    if(b.size()<288 || b.size()>FirmwareImage::MaxSize || uchar(b[0])!=0xE9 ||
       uchar(b[1])==0 || uchar(b[1])>16 || uchar(b[12])!=9 || uchar(b[13])!=0)
        return reject(QStringLiteral("请选择 ESP32-S3 的 SKPass 应用固件，不能选择 bootloader 或合并固件。"));
    const QByteArray project=b.mid(80,32);
    const int terminator=project.indexOf('\0');
    const auto name=terminator<0?QByteArray():project.first(terminator);
    if(application && (word(b,32)!=0xABCD5432 || (name!="SKPass" && name!="pn7150_cardio")))
        return reject(QStringLiteral("固件工程名不是 SKPass。"));
    int offset=24; uchar checksum=0xEF;
    for(int i=0;i<uchar(b[1]);i++) {
        if(offset+8>b.size())return reject(QStringLiteral("固件段头不完整。"));
        quint32 n=word(b,offset+4);offset+=8;
        if(n>quint32(b.size()-offset))return reject(QStringLiteral("固件段数据不完整。"));
        for(quint32 j=0;j<n;j++)checksum^=uchar(b[offset+int(j)]);
        offset+=int(n);
    }
    int end=(offset+16)&~15;
    if(end>b.size() || uchar(b[end-1])!=checksum)return reject(QStringLiteral("固件校验和错误，文件可能已损坏。"));
    if(uchar(b[23])!=1 || end+32>b.size() ||
       QCryptographicHash::hash(b.first(end),QCryptographicHash::Sha256)!=b.mid(end,32))
        return reject(QStringLiteral("固件 SHA-256 校验失败；需要带哈希的 ESP-IDF 应用镜像。"));
    return true;
}
}
bool FirmwareImage::validBootloader(const QByteArray &input,QString *error) {
    if(input.size()>0x8000) {if(error)*error=QStringLiteral("Bootloader 大小无效。 ");return false;}
    return imageValid(input,error,false);
}

QByteArray FirmwareImage::prepare(const QByteArray &input,bool isUf2,QString *error) {
    if(error)error->clear();
    QByteArray app;
    if(isUf2) {
        if(input.isEmpty() || input.size()%512 || input.size()>MaxSize*2) {
            if(error)*error=QStringLiteral("UF2 大小无效。 ");return {};
        }
        const int count=input.size()/512;
        app=QByteArray(count*256,char(0xFF));QByteArray seen(count,0);
        for(int i=0;i<count;i++) {
            const auto b=input.mid(i*512,512);quint32 number=word(b,20);
            if(word(b,0)!=0x0A324655 || word(b,4)!=0x9E5D5157 || word(b,508)!=0x0AB16F30 ||
               word(b,8)!=0x2000 || word(b,28)!=Family || word(b,16)!=256 ||
               word(b,24)!=quint32(count) || number>=quint32(count) || word(b,12)!=number*256 || seen[int(number)]) {
                if(error)*error=QStringLiteral("不是完整的 SKPass UF2：检查芯片、应用相对地址、块数和重复块。 ");return {};
            }
            seen[int(number)]=1;std::memcpy(app.data()+number*256,b.constData()+32,256);
        }
    } else app=input;
    if(!imageValid(app,error))return {};
    if(isUf2)return input;
    int count=(app.size()+255)/256;
    app.append(QByteArray(count*256-app.size(),char(0xFF)));
    QByteArray result;result.reserve(count*512);
    for(int i=0;i<count;i++) {
        QByteArray block(512,0);
        auto put=[&](int o,quint32 n){qToLittleEndian(n,reinterpret_cast<uchar *>(block.data()+o));};
        put(0,0x0A324655);put(4,0x9E5D5157);put(8,0x2000);put(12,i*256);put(16,256);
        put(20,i);put(24,count);put(28,Family);put(508,0x0AB16F30);
        std::memcpy(block.data()+32,app.constData()+i*256,256);result+=block;
    }
    return result;
}
