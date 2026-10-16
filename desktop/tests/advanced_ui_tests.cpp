#include "AdvancedPanel.h"
#include "FrameParser.h"
#include "AppTheme.h"
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTimer>
#include <QEventLoop>
#include <QMessageBox>
#include <QCheckBox>
#include <QTableWidget>
#include <cstdio>
extern "C" {
#include "reader_frame.h"
#include "hid_wire.h"
}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(false)
static advanced_target_t card(int protocol,int subtype,int last,int unit,int uidlen){
    advanced_target_t t={};t.token=15;t.uid_len=uidlen;for(int x=0;x<uidlen;x++)t.uid[x]=x+1;
    t.protocol=protocol;t.subtype=subtype;t.last=last;t.first=protocol==2?4:0;t.unit=unit;t.available=1;return t;
}
static QByteArray targetBytes(advanced_target_t t){uint8_t data[ADV_TARGET_BYTES];Advanced_Encode_Target(&t,data);return QByteArray(reinterpret_cast<char*>(data),sizeof(data));}
int main(int argc,char **argv){
    QApplication app(argc,argv);applyAppTheme(app);AdvancedPanel panel;panel.connected(true);
    auto t=card(0x80,ADV_CLASSIC_1K,63,16,4);advanced_target_t parsed;
    CHECK(ReaderProtocol::target(targetBytes(t),parsed));
    auto forged=t;forged.protocol=4;CHECK(!ReaderProtocol::target(targetBytes(forged),parsed));
    QString error;auto req=ReaderProtocol::request(t,123,ADV_WRITE,3,1,0,0x60,QByteArray(6,char(0xFF)),QByteArray(16,0),&error);CHECK(req.isEmpty());
    req=ReaderProtocol::request(t,123,ADV_READ,64,1,0,0x60,QByteArray(6,char(0xFF)),{},&error);CHECK(req.isEmpty());
    req=ReaderProtocol::request(t,123,ADV_READ,4,1,0,0x61,QByteArray(6,char(0xFF)),{},&error);CHECK(req.size()==36);
    for(bool hid:{false,true}){
        uint8_t raw[READER_MAX_FRAME];size_t n=reader_encode_frame(ADV_REQUEST_MSG,reinterpret_cast<const uint8_t*>(req.constData()),req.size(),raw,sizeof(raw));CHECK(n);
        FrameParser decoder;int received=0;QObject::connect(&decoder,&FrameParser::frame,[&](quint8 msg,QByteArray payload,QByteArray){advanced_request_t q;CHECK(msg==ADV_REQUEST_MSG);CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(payload.constData()),payload.size(),&q));CHECK(Advanced_Validate(&t,&q)==ADV_OK);received++;return 0;});
        if(hid){skpass_hid_assembly_t assembly={};uint8_t report[64];for(size_t x=0;x<skpass_hid_fragments(n);x++){CHECK(skpass_hid_fragment(raw,n,1,x,report));auto ready=skpass_hid_receive(&assembly,report,64);CHECK(ready>=0);if(ready)decoder.feed(QByteArray(reinterpret_cast<char*>(assembly.data),assembly.size));}}
        else for(size_t x=0;x<n;x+=3)decoder.feed(QByteArray(reinterpret_cast<char*>(raw+x),qMin(size_t(3),n-x)));
        CHECK(received==1);
    }
    panel.targetFrame(targetBytes(t));panel.findChild<QGroupBox*>("advancedOperationGroup")->setChecked(true);
    CHECK(panel.findChild<QSpinBox*>("advancedAddress")->maximum()==63);
    CHECK(panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    auto saved=panel.findChild<QTableWidget*>("advancedResultTable");CHECK(saved->rowCount()==0);
    auto input=panel.findChild<QPlainTextEdit*>("advancedData");input->setPlainText(QString(32,'A'));
    panel.targetFrame(targetBytes(t));CHECK(input->toPlainText()==QString(32,'A'));
    QEventLoop expiry;QTimer::singleShot(2300,&expiry,&QEventLoop::quit);expiry.exec();
    CHECK(!panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    panel.targetFrame(targetBytes(t));
    QByteArray sent;QObject::connect(&panel,&AdvancedPanel::request,[&](QByteArray p){sent=p;});
    panel.findChild<QPushButton*>("advancedRead")->click();CHECK(!sent.isEmpty());
    advanced_request_t q;CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));
    advanced_result_t r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_OK;r.completed=1;r.length=16;
    uint8_t response[300];auto size=Advanced_Encode_Result(&r,response,sizeof(response));
    auto wrong=r;wrong.target.uid[0]^=1;auto badSize=Advanced_Encode_Result(&wrong,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),badSize));
    CHECK(!panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));
    CHECK(panel.findChild<QLabel*>("advancedStatus")->text().contains(QStringLiteral("读取成功")));
    CHECK(saved->rowCount()==1);
    auto renewed=t;renewed.token++;panel.targetFrame(targetBytes(renewed));CHECK(saved->rowCount()==1);
    panel.activated({});CHECK(saved->rowCount()==1);panel.connected(false);CHECK(saved->rowCount()==1);
    panel.connected(true);panel.targetFrame(targetBytes(t));
    CHECK(panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    auto writer=panel.findChild<QPushButton*>("advancedWrite");sent.clear();input->setPlainText(QString(32,'A'));panel.findChild<QSpinBox*>("advancedAddress")->setValue(1);
    QTimer::singleShot(0,[&]{auto changed=t;changed.uid[0]=99;changed.token++;panel.targetFrame(targetBytes(changed));for(auto w:app.topLevelWidgets())if(auto box=qobject_cast<QMessageBox*>(w))box->button(QMessageBox::Yes)->click();});
    writer->click();CHECK(sent.isEmpty());CHECK(panel.findChild<QLabel*>("advancedStatus")->text().contains("CARD_MISMATCH"));
    panel.activated(QByteArray::fromHex("99887766"));CHECK(!panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    t=card(2,ADV_NTAG215,129,4,7);panel.targetFrame(targetBytes(t));CHECK(panel.findChild<QSpinBox*>("advancedAddress")->minimum()==4 && panel.findChild<QSpinBox*>("advancedAddress")->maximum()==129);
    panel.findChild<QSpinBox*>("advancedAddress")->setValue(129);CHECK(panel.findChild<QSpinBox*>("advancedCount")->maximum()==1);
    auto ndef=ReaderProtocol::ndef("https://example.com",true,504,&error);CHECK(!ndef.isEmpty() && ndef.size()%4==0);
    req=ReaderProtocol::request(t,44,ADV_NDEF,4,ndef.size()/4,0,0x60,QByteArray(6,0),ndef,&error);CHECK(!req.isEmpty());
    CHECK(ReaderProtocol::ndef(QStringLiteral("测试 NFC 文本"),false,504,&error).size()>8);
    CHECK(ReaderProtocol::ndef("javascript:alert(1)",true,504,&error).isEmpty());
    CHECK(ReaderProtocol::ndef(QString(300,'A'),false,504,&error).isEmpty());
    t=card(3,ADV_LITE_S,13,16,8);panel.targetFrame(targetBytes(t));CHECK(panel.findChild<QSpinBox*>("advancedAddress")->maximum()==13);
    req=ReaderProtocol::request(t,44,ADV_WRITE,14,1,0,0x60,QByteArray(6,0),QByteArray(16,0),&error);CHECK(req.isEmpty());
    t=card(6,ADV_ISO15693_4,31,4,8);panel.targetFrame(targetBytes(t));CHECK(panel.findChild<QSpinBox*>("advancedAddress")->maximum()==31);
    t.subtype=0;t.protocol=4;panel.targetFrame(targetBytes(t));CHECK(!panel.findChild<QPushButton*>("advancedWrite")->isEnabled());
    CHECK(panel.findChild<QLabel*>("advancedStatus")->text().contains(QStringLiteral("金融安全保护")));
    panel.connected(false);CHECK(!panel.findChild<QPushButton*>("advancedRead")->isEnabled());
    CHECK(saved->rowCount()==1);
    // Scan partial data, explicit failure retry, lease renewal and replacement-card rejection.
    panel.connected(true,true);t=card(0x80,ADV_CLASSIC_1K,63,16,4);panel.targetFrame(targetBytes(t));
    panel.findChild<QPushButton*>("advancedScan")->click();CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));
    CHECK(q.operation==ADV_SCAN && q.count==4 && saved->rowCount()==64);
    r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_RF_FAILED;r.completed=2;r.length=32;memset(r.data,0x41,32);
    size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));
    CHECK(saved->item(0,2)->text().startsWith("41 41"));CHECK(saved->item(2,2)->text().isEmpty());
    sent.clear();app.processEvents();CHECK(sent.isEmpty()); // Wait for fresh target, not a stale lease.
    t.token++;panel.targetFrame(targetBytes(t));app.processEvents();CHECK(!sent.isEmpty());
    CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));CHECK(q.address==4 && q.operation==ADV_SCAN);
    panel.findChild<QPushButton*>("advancedStopScan")->click();
    r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_OK;r.completed=4;r.length=64;
    size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));CHECK(!saved->item(4,2)->text().isEmpty());
    panel.activated({});CHECK(saved->rowCount()==64);t.token++;panel.targetFrame(targetBytes(t));
    panel.findChild<QPushButton*>("advancedRetryScan")->click();CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));CHECK(q.address==2 && q.count==1);
    r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_OK;r.completed=1;r.length=16;
    size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));
    sent.clear();auto other=t;other.uid[0]=99;other.token++;panel.targetFrame(targetBytes(other));app.processEvents();CHECK(sent.isEmpty());CHECK(saved->rowCount()==64);
    panel.connected(false);CHECK(saved->rowCount()==64);
    panel.findChild<QPushButton*>("advancedClearResults")->click();CHECK(saved->rowCount()==0);
    panel.connected(true,true);t=card(0x80,ADV_CLASSIC_4K,63,16,4);panel.targetFrame(targetBytes(t));panel.findChild<QPushButton*>("advancedScan")->click();CHECK(saved->rowCount()==256);
    int total=0;for(int sector=0;sector<40;sector++){
        CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));CHECK(q.operation==ADV_SCAN && q.address==total && q.count==(sector<32?4:16));
        r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_OK;r.completed=q.count;r.length=q.count*16;memset(r.data,0x31,r.length);total+=q.count;
        size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));sent.clear();app.processEvents();
        if(sector<39){t.token++;panel.targetFrame(targetBytes(t));app.processEvents();CHECK(!sent.isEmpty());}
    }
    CHECK(total==256 && !saved->item(255,2)->text().isEmpty() && !panel.findChild<QPushButton*>("advancedStopScan")->isEnabled());
    req=ReaderProtocol::request(t,123,ADV_SCAN,128,16,0,0x60,QByteArray(6,char(0xFF)),{},&error);CHECK(!req.isEmpty());
    req=ReaderProtocol::request(t,123,ADV_WRITE,128,1,0,0x60,QByteArray(6,char(0xFF)),QByteArray(16,0),&error);CHECK(req.isEmpty());
    if(app.arguments().contains("--screenshot")){
        panel.connected(true,true);t=card(0x80,ADV_CLASSIC_1K,63,16,4);panel.targetFrame(targetBytes(t));panel.findChild<QPushButton*>("advancedScan")->click();
        CHECK(Advanced_Decode_Request(reinterpret_cast<const uint8_t*>(sent.constData()),sent.size(),&q));r={};r.id=q.id;r.target=q.target;r.operation=q.operation;r.address=q.address;r.status=ADV_RF_FAILED;r.completed=2;r.length=32;for(int x=0;x<32;x++)r.data[x]=x+0x30;
        size=Advanced_Encode_Result(&r,response,sizeof(response));panel.response(QByteArray(reinterpret_cast<char*>(response),size));panel.findChild<QPushButton*>("advancedStopScan")->click();
        panel.resize(1080,1450);panel.show();app.processEvents();panel.grab().save("advanced-classic.png");
        t=card(2,ADV_NTAG215,129,4,7);panel.targetFrame(targetBytes(t));app.processEvents();panel.grab().save("advanced-type2.png");
    }
    puts("Advanced forms, guarded protocol, NDEF and CDC/HID vectors passed");return 0;
}
