#include "Spad.h"
#include "SpadTables.h"
#include "AimeCode.h"
#include <stdexcept>
namespace {
const auto &inverse() {
    static const auto tables=[] {
        std::array<std::array<std::uint8_t,256>,9> inv{};
        for(int t=0;t<9;++t) {
            std::array<bool,256> seen{};
            for(int i=0;i<256;++i) {
                auto b=SpadTables[t][i];
                if(seen[b])throw std::runtime_error("Invalid SPAD permutation");
                seen[b]=true;inv[t][b]=std::uint8_t(i);
            }
        }
        return inv;
    }();
    return tables;
}
}
namespace Spad {
bool tablesValid() {try {(void)inverse();return true;}catch(...){return false;}}
QByteArray encrypt(const QByteArray &plain) {
    if(plain.size()!=16 || !tablesValid())return {};
    QByteArray d=plain;
    int count=(quint8(d[15])>>4)+7,table=quint8(d[15]);
    for(int k=0;k<count;++k,table+=5) {
        for(int i=0;i<15;++i)d[i]=char(SpadTables[table%8][quint8(d[i])]);
        quint8 prior=quint8(d[0]);
        for(int i=14;i>=0;--i) {quint8 old=quint8(d[i]);d[i]=char(((old&7)<<5)|(prior>>3));prior=old;}
    }
    for(int i=0;i<16;++i)d[i]=char(SpadTables[8][quint8(d[i])]);
    return d;
}
QByteArray decrypt(const QByteArray &cipher) {
    if(cipher.size()!=16 || !tablesValid())return {};
    const auto &inv=inverse();QByteArray d=cipher;
    for(int i=0;i<16;++i)d[i]=char(inv[8][quint8(d[i])]);
    int count=(quint8(d[15])>>4)+7,table=quint8(d[15])+5*count;
    for(int k=0;k<count;++k) {
        table-=5;quint8 prior=quint8(d[14]);
        for(int i=0;i<15;++i) {quint8 old=quint8(d[i]);d[i]=char((old>>5)|((prior&31)<<3));prior=old;}
        for(int i=0;i<15;++i)d[i]=char(inv[table%8][quint8(d[i])]);
    }
    return d;
}
QString accessCode(const QByteArray &cipher,QByteArray *plain,QString *error) {
    auto d=decrypt(cipher);if(plain)*plain=d;
    if(d.size()!=16 || (quint8(d[0])&15)!=0) {
        if(error)*error=QStringLiteral("SPAD解密后版本/长度无效");return {};
    }
    auto code=AimeCode::fromBcd(d.mid(6,10));
    if(code.isEmpty() && error)*error=QStringLiteral("SPAD偏移6处没有有效20位BCD卡号");
    return code;
}
}
