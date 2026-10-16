#include "AimeCode.h"
#include "Spad.h"
#include "FrameParser.h"
#include "CardTypes.h"
#include "reader_frame.h"
#include "writer_protocol.h"
#include "WriteProtocol.h"
#include <QCoreApplication>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cstring>
static void check(bool ok,const char *name) {if(!ok){std::cerr<<"FAILED: "<<name<<std::endl;std::exit(1);}}
static QByteArray frame(quint8 msg,const QByteArray &p) {
    uint8_t raw[520];auto n=reader_encode_frame(msg,reinterpret_cast<const uint8_t *>(p.constData()),p.size(),raw,sizeof(raw));
    return QByteArray(reinterpret_cast<const char *>(raw),int(n));
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const QStringList serials={"12345678","10000001","20000002"};
    const QStringList codes={"01036261387195961452","01036137219732328682","01036995956171196675"};
    for(int i=0;i<serials.size();++i){check(AimeCode::fromSerial(serials[i])==codes[i],"classic forward");check(AimeCode::serialFromCode(codes[i])==serials[i],"classic reverse");}
    auto plain=QByteArray::fromHex("00000000000001035640268054098707");
    auto encrypted=QByteArray::fromHex("d276519cb52c008b0227155ed7d2bed9");
    check(Spad::tablesValid(),"S-box permutation");check(Spad::encrypt(plain)==encrypted,"SPAD encrypt vector");
    check(Spad::decrypt(encrypted)==plain,"SPAD decrypt vector");check(Spad::accessCode(encrypted)=="01035640268054098707","SPAD offset6");
    for(int p=0;p<256;++p){auto d=plain;d[15]=char(p);check(Spad::decrypt(Spad::encrypt(d))==d,"SPAD all parameters");}
    auto mifare=QByteArray::fromHex("1DB40F0DAB080400C832002000000016"
                                   "00000000000000000000000000000000"
                                   "00000000000010114514191981023333");
    check(mifare.size()==48,"MIFARE fixture size");check(AimeCode::fromMifare(mifare)=="10114514191981023333","MIFARE sample");
    check(AimeCode::fromMifare(QByteArray(48,0)).isEmpty(),"reject zero data");
    check(AimeCode::derivedFromIdm(QByteArray::fromHex("8000000000000000"))=="09223372036854775808","INT64_MIN magnitude");
    check(reader_crc16(reinterpret_cast<const uint8_t *>("123456789"),9)==0x29B1,"CRC known vector");
    FrameParser parser;int accepted=0;QByteArray expected("firmware");
    QObject::connect(&parser,&FrameParser::frame,[&](quint8 msg,QByteArray p,QByteArray raw){check(msg==1 && p==expected && !raw.isEmpty(),"parsed content");++accepted;});
    auto good=frame(1,expected),bad=good;bad[6]=char(bad[6]^1);
    parser.feed("noise"+good.left(4));check(accepted==0,"half frame");parser.feed(good.mid(4));check(accepted==1,"split frame");
    parser.feed(bad+QByteArray::fromHex("CA5A017FFF7F")+good+good);check(accepted==3 && parser.badFrames()>=2,"CRC length and glued recovery");
    CardEvent event;QString error;check(!CardEvent::parse(QByteArray(19,0),event,error),"short event rejected");
    check(!CardEvent::parse(QByteArray(20,0)+"x",event,error),"extra event bytes rejected");
    auto uid=QByteArray::fromHex("11223344"),key=QByteArray::fromHex("574343467632");
    auto request=WriteProtocol::request(0x12345678,uid,key,codes[0]);
    writer_request_t q{};
    check(request.size()==32 && writer_decode_request(reinterpret_cast<const uint8_t *>(request.constData()),request.size(),&q),"Qt to firmware write request");
    check(q.id==0x12345678 && q.uid_len==4 && QByteArray(reinterpret_cast<const char *>(q.uid),4)==uid,"write request UID and endian");
    uint8_t desired[32];writer_build_blocks(&q,desired);
    check(QByteArray(reinterpret_cast<const char *>(desired),32)==WriteProtocol::blocks(codes[0]),"write block layout matches firmware");
    writer_result_t wr{};wr.id=q.id;wr.status=WRITE_SUCCESS;wr.uid_len=4;wr.verified_mask=3;
    std::memcpy(wr.uid,uid.constData(),4);std::memcpy(wr.blocks,desired,32);
    uint8_t response[WRITER_RESULT_BYTES];writer_encode_result(&wr,response);
    WriteProtocol::Result result;
    check(WriteProtocol::parse(QByteArray(reinterpret_cast<const char *>(response),51),result) && result.uid==uid && result.data==WriteProtocol::blocks(codes[0]),"firmware to Qt write response");
    check(WriteProtocol::request(1,uid,key,QString(20,'0')).isEmpty(),"reject zero write code");
    check(WriteProtocol::request(1,uid,key,"abc").isEmpty(),"reject nondecimal write code");
    std::cout<<"All SKPass vectors and parser checks passed\n";
}
