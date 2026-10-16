#include "SerialReader.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include <QVector>
#include <cstdio>
#include <cstring>
extern "C" {
#include "ce_protocol.h"
}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
static int openResult=1, closeCount=0;
static QVector<QByteArray> incoming, sent;
extern "C" size_t skpass_hid_list(skpass_hid_device_t *,size_t) {return 0;}
extern "C" int skpass_hid_open(skpass_hid_handle_t *h,const wchar_t *,wchar_t id[128]) {
    if(openResult==1){h->handle=reinterpret_cast<HANDLE>(1);h->session=0x12345678;if(id)wcscpy(id,L"CARDIOP1");}return openResult;
}
extern "C" void skpass_hid_close(skpass_hid_handle_t *h){h->handle=nullptr;++closeCount;}
extern "C" int skpass_hid_read(skpass_hid_handle_t *,uint8_t out[64]) {
    if(incoming.isEmpty())return 0;auto r=incoming.takeFirst();memcpy(out,r.constData(),64);return 1;
}
extern "C" int skpass_hid_send(skpass_hid_handle_t *,const uint8_t *frame,size_t size,DWORD) {
    sent.append(QByteArray(reinterpret_cast<const char *>(frame),int(size)));return 1;
}
class SerialReaderTestAccess {
public:
    static void poll(SerialReader &r){r.pollHid();}
    static void expire(SerialReader &r){QThread::msleep(5100);r.checkConnection();}
};
static QByteArray makeFrame(quint8 msg,const QByteArray &payload) {
    QByteArray f=QByteArray::fromHex("CA5A01");f.append(char(msg));f.append(char(payload.size()));f.append(char(payload.size()>>8));f+=payload;
    auto crc=FrameParser::crc16(f.mid(2));f.append(char(crc));f.append(char(crc>>8));return f;
}
static void enqueue(quint8 msg,const QByteArray &payload) {
    auto f=makeFrame(msg,payload);uint8_t report[64];
    for(size_t i=0;i<skpass_hid_fragments(size_t(f.size()));++i) {
        skpass_hid_fragment(reinterpret_cast<const uint8_t *>(f.constData()),size_t(f.size()),7,uint8_t(i),report);
        incoming.append(QByteArray(reinterpret_cast<const char *>(report),64));
    }
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);SerialReader r;int frames=0;QString detail;
    QObject::connect(&r,&SerialReader::frame,[&](quint8 msg,QByteArray,QByteArray){if(msg==0x10)++frames;});
    QObject::connect(&r,&SerialReader::connectionChanged,[&](SerialReader::ConnectionState,QString d){detail=d;});
    r.open("auto");CHECK(sent.size()==1 && sent[0]==makeFrame(0x32,QByteArray::fromHex("78563412")));
    enqueue(1,"SKPass-2.6.0-retry-uf2-write");SerialReaderTestAccess::poll(r);CHECK(!r.isConnected());
    enqueue(0xB2,QByteArray::fromHex("0178563413")); // wrong session
    enqueue(1,"SKPass-2.6.0-retry-uf2-write");SerialReaderTestAccess::poll(r);CHECK(!r.isConnected());
    enqueue(0xB2,QByteArray::fromHex("0178563412"));
    enqueue(1,"SKPass-2.6.0-retry-uf2-write");SerialReaderTestAccess::poll(r);
    CHECK(r.isConnected() && r.canWrite() && r.canUpdate());CHECK(detail.contains("HID"));
    enqueue(0x10,QByteArray(84,'a'));SerialReaderTestAccess::poll(r);CHECK(frames==1); // requires two HID reports
    QString error;CHECK(r.requestDeviceInfo(&error));CHECK(sent.last()[3]==char(0x31));
    QJsonObject info{{"schema",1},{"firmware","SKPass-2.6.0-retry-uf2-write"},{"version","2.6.0"},
        {"built","Oct 12 2026 18:00:00"},{"app_sha256",QString(64,'a')},{"usb_state","connected"},{"nfc_state","ready"}};
    enqueue(0xB1,QJsonDocument(info).toJson(QJsonDocument::Compact));SerialReaderTestAccess::poll(r);
    CHECK(r.deviceInformation()==info);
    CHECK(r.sendWrite(QByteArray(32,'a'),&error));CHECK(sent.last()[3]==char(0x20));
    CHECK(r.enterFirmwareUpdate(&error));CHECK(sent.last()[3]==char(0x30));
    CHECK(!r.canRomDownload() && !r.enterRomDownload(false,&error));
    enqueue(1,"SKPass-2.7.0-retry-rom-uf2-write");SerialReaderTestAccess::poll(r);
    CHECK(r.canRomDownload());CHECK(r.enterRomDownload(false,&error));CHECK(sent.last()==makeFrame(0x34,QByteArray(1,char(0))));
    enqueue(1,"SKPass-2.10.0-retry-rom-adv-scan-ce-uf2-write");SerialReaderTestAccess::poll(r);
    CHECK(r.canCardEmulation() && r.canAdvanced());
    ce_request_t ce={};ce.id=15;ce.action=CE_QUERY;uint8_t cp[512];auto cn=CE_Encode_Request(&ce,cp,sizeof(cp));
    CHECK(r.sendCardEmulation(QByteArray(reinterpret_cast<char *>(cp),cn),&error) && sent.last()[3]==char(CE_REQUEST_MSG));
    r.setCardEmulationMode(CE_MODE_LISTEN);CHECK(!r.canWrite() && !r.canAdvanced() && r.canCardEmulation());
    CHECK(!r.sendWrite(QByteArray(31,'a'),&error));r.setCardEmulationMode(CE_MODE_RW);CHECK(r.canWrite());
    CHECK(r.enterRomDownload(true,&error));CHECK(sent.last()==makeFrame(0x34,QByteArray(1,char(1))));
    r.stop();CHECK(!r.isConnected());CHECK(sent.last()==makeFrame(0x33,QByteArray::fromHex("78563412")));
    int closes=closeCount;openResult=-1;r.open("auto");CHECK(!r.isConnected() && detail.contains(QStringLiteral("占用")));CHECK(closeCount==closes);
    r.stop();openResult=1;r.open("hid:CARDIOP1");
    enqueue(0xB2,QByteArray::fromHex("0178563412"));enqueue(1,"SKPass-2.6.0-retry-uf2-write");SerialReaderTestAccess::poll(r);
    SerialReaderTestAccess::expire(r);CHECK(!r.isConnected() && r.deviceInformation().isEmpty());
    r.stop();std::puts("HID reader: nonce handshake, segmented events, query/write/update, close, busy and heartbeat expiry passed");return 0;
}
