#include "FrameParser.h"
quint16 FrameParser::crc16(const QByteArray &data) {
    quint16 crc = 0xFFFF;
    for(char c : data) {
        crc ^= quint16(quint8(c)) << 8;
        for(int b=0;b<8;++b) crc = crc & 0x8000 ? quint16((crc<<1)^0x1021) : quint16(crc<<1);
    }
    return crc;
}
void FrameParser::feed(const QByteArray &data) {
    // Bound memory even if a device sends unlimited garbage in one read.
    for(qsizetype pos=0;pos<data.size();pos+=1024) {
        buffer += data.mid(pos,1024);
        for(;;) {
            const qsizetype i = buffer.indexOf(QByteArray::fromHex("CA5A"));
            if(i<0) {buffer = buffer.endsWith(char(0xCA)) ? QByteArray(1,char(0xCA)) : QByteArray(); break;}
            if(i>0) buffer.remove(0,i);
            if(buffer.size()<6) break;
            quint16 n = quint8(buffer[4]) | (quint16(quint8(buffer[5]))<<8);
            if(quint8(buffer[2])!=1 || n>512) {
                buffer.remove(0,1); emit rejected(++bad); continue;
            }
            if(buffer.size()<n+8) break;
            quint16 crc = quint8(buffer[n+6]) | (quint16(quint8(buffer[n+7]))<<8);
            if(crc16(buffer.mid(2,n+4))!=crc) {
                buffer.remove(0,1); emit rejected(++bad); continue;
            }
            auto raw = buffer.left(n+8);
            auto payload = buffer.mid(6,n);
            quint8 msg = quint8(buffer[3]);
            buffer.remove(0,n+8);
            emit frame(msg,payload,raw);
        }
    }
}
