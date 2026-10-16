#include "CardEmulationPanel.h"
#include "FrameParser.h"
#include "AppTheme.h"
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QtEndian>
#include <cstdio>
#include <cstring>
#include <string>
extern "C" {
#include "ce_protocol.h"
#include "reader_frame.h"
#include "hid_wire.h"
}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
static QByteArray response(quint32 id,int status,int mode){uint8_t p[CE_REPLY_BYTES];uint8_t uid[]={1,2,3,4};CE_Encode_Reply(id,status,mode,uid,0,p);return QByteArray(reinterpret_cast<char *>(p),sizeof(p));}
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setOrganizationName("SKPassTests");app.setApplicationName("CE");applyAppTheme(app);
    CardEmulationPanel panel;QByteArray sent;int mode=-1;
    QObject::connect(&panel,&CardEmulationPanel::request,[&](QByteArray p){sent=p;});QObject::connect(&panel,&CardEmulationPanel::modeChanged,[&](int m){mode=m;});
    panel.connected(true);CHECK(sent.size()==5 && sent[4]==CE_QUERY);auto id=qFromLittleEndian<quint32>(sent.constData());panel.response(response(id,CE_OK,CE_MODE_RW));
    auto uid=panel.findChild<QLineEdit *>("ceUid");auto content=panel.findChild<QPlainTextEdit *>("ceNdefContent");auto apply=panel.findChild<QPushButton *>("ceApply");uid->setText("12345678");content->setPlainText("https://example.com");
    uid->setText("ZZ123456");sent.clear();apply->click();CHECK(sent.isEmpty());uid->setText("12345678");
    auto rules=panel.findChild<QTableWidget *>("ceRules");rules->item(0,1)->setText("80CA");rules->item(0,2)->setText("AABB9000");apply->click();CHECK(!sent.isEmpty() && mode==CE_MODE_PENDING && !apply->isEnabled());
    ce_request_t q={};CHECK(CE_Decode_Request(reinterpret_cast<const uint8_t *>(sent.constData()),sent.size(),&q));CHECK(q.action==CE_APPLY && q.config.rule_count==1 && q.config.ndef_len);
    panel.response(response(q.id+1,CE_OK,CE_MODE_LISTEN));CHECK(!apply->isEnabled());panel.response(response(q.id,CE_ACCEPTED,CE_MODE_PENDING));CHECK(!apply->isEnabled());panel.response(response(q.id,CE_OK,CE_MODE_LISTEN));CHECK(apply->isEnabled() && mode==CE_MODE_LISTEN);
    for(bool hid:{false,true}){
        ce_request_t maximum=q;maximum.config.rule_count=4;maximum.config.ndef_len=CardEmu_Build_Ndef(1,std::string(233,'a').c_str(),maximum.config.ndef,240);
        for(auto &r:maximum.config.rules){r.match_len=16;r.response_len=32;memset(r.match,0xAA,16);memset(r.response,0xBB,32);}
        uint8_t payload[512],frame[READER_MAX_FRAME];size_t n=CE_Encode_Request(&maximum,payload,sizeof(payload));CHECK(n==459);size_t size=reader_encode_frame(CE_REQUEST_MSG,payload,n,frame,sizeof(frame));
        FrameParser decoder;int received=0;QObject::connect(&decoder,&FrameParser::frame,[&](quint8 msg,QByteArray p,QByteArray){ce_request_t parsed;bool valid=msg==CE_REQUEST_MSG && CE_Decode_Request(reinterpret_cast<const uint8_t *>(p.constData()),p.size(),&parsed);if(valid)received++;});
        if(hid){skpass_hid_assembly_t assembly={};uint8_t report[64];for(size_t i=0;i<skpass_hid_fragments(size);i++){CHECK(skpass_hid_fragment(frame,size,17,i,report));int ready=skpass_hid_receive(&assembly,report,64);CHECK(ready>=0);if(ready)decoder.feed(QByteArray(reinterpret_cast<char *>(assembly.data),assembly.size));}}
        else for(size_t i=0;i<size;i+=7)decoder.feed(QByteArray(reinterpret_cast<char *>(frame+i),qMin(size_t(7),size-i)));
        CHECK(received==1);
    }
    QByteArray event(CE_EVENT_HEADER,0);event[0]=1;event[1]=CE_EVT_RX;event+=QByteArray::fromHex("00A4040007D276000085010100");panel.event(event);
    CHECK(panel.findChild<QPlainTextEdit *>("ceCapture")->toPlainText().contains("RX APDU"));
    event.resize(CE_EVENT_HEADER);event[1]=CE_EVT_NDEF;event+=QByteArray(reinterpret_cast<const char *>(q.config.ndef),q.config.ndef_len);panel.event(event);
    CHECK(panel.findChild<QLabel *>("ceReceivedNdef")->text().contains("https://example.com"));
    auto log=panel.findChild<QPlainTextEdit *>("ceCapture")->toPlainText();panel.connected(false);CHECK(panel.findChild<QPlainTextEdit *>("ceCapture")->toPlainText()==log);
    if(app.arguments().contains("--screenshot")){panel.resize(1100,1100);panel.show();app.processEvents();panel.grab().save("ce-preview.png");}
    std::puts("CE UI validation, confirmed mode, persistent events, CDC/HID maximum request passed");return 0;
}
