#include "NfcDetails.h"
#include "FrameParser.h"
#include "NfcPage.h"
#include "AppTheme.h"
#include "AdvancedPanel.h"
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <QListWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <cstdio>
extern "C" {
#include "reader_frame.h"
#include "hid_wire.h"
size_t nfc_test_part(unsigned scan,unsigned part,const char *kind,char *out,size_t capacity);
}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    applyAppTheme(app);
    for(bool hid:{false,true}) {
        FrameParser wire;NfcDetails assembler;NfcDocument doc;int messages=0,completed=0,rejected=0;
        NfcPage page;page.activated(QByteArray::fromHex("011E65740A055390"));
        QObject::connect(&wire,&FrameParser::frame,[&](quint8 msg,QByteArray p,QByteArray){
            if(msg!=0x12)return;
            messages++;QString error;const auto result=assembler.feed(p,doc,&error);
            if(result==NfcDetails::Result::Complete)completed++;
            if(result==NfcDetails::Result::Rejected)rejected++;
            page.receive(p,&error);
        });
        for(unsigned part:{2u,0u,1u}) {
            char payload[513];uint8_t encoded[READER_MAX_FRAME];
            const size_t n=nfc_test_part(42,part,"transit_japan",payload,sizeof(payload));CHECK(n && n<=512);
            const size_t f=reader_encode_frame(0x12,reinterpret_cast<uint8_t *>(payload),n,encoded,sizeof(encoded));CHECK(f);
            if(hid) {
                skpass_hid_assembly_t assembly={};uint8_t report[64];
                for(size_t index=0;index<skpass_hid_fragments(f);index++) {
                    CHECK(skpass_hid_fragment(encoded,f,part,index,report));
                    const auto ready=skpass_hid_receive(&assembly,report,64);CHECK(ready>=0);
                    if(ready==1)wire.feed(QByteArray(reinterpret_cast<char *>(assembly.data),assembly.size));
                }
            } else {
                for(size_t offset=0;offset<f;offset+=7)wire.feed(QByteArray(reinterpret_cast<char *>(encoded+offset),qMin(size_t(7),f-offset)));
            }
        }
        CHECK(messages==3 && completed==1 && rejected==0);CHECK(doc.records.size()==2);
        CHECK(page.findChild<QLabel*>("nfcType")->text()==QStringLiteral("交通卡"));
        CHECK(page.findChild<QLabel*>("nfcBalance")->text()==QStringLiteral("¥ 1234 JPY"));
        CHECK(page.findChild<QTableWidget*>("nfcHistory")->rowCount()==2);
        auto functions=page.findChild<QTabBar*>("nfcFunctions");auto pages=page.findChild<QStackedWidget*>("nfcFunctionPages");
        CHECK(functions && functions->count()==3 && pages && pages->count()==3);
        CHECK(functions->tabText(0)==QStringLiteral("交通卡识别") && functions->tabText(1)==QStringLiteral("高级读写"));
        CHECK(functions->tabText(2)==QStringLiteral("卡模拟"));
        advanced_target_t target={};target.token=1;target.uid_len=4;target.uid[0]=1;target.uid[1]=2;target.uid[2]=3;target.uid[3]=4;
        target.protocol=0x80;target.subtype=ADV_CLASSIC_1K;target.last=63;target.unit=16;target.available=1;
        uint8_t targetData[ADV_TARGET_BYTES];Advanced_Encode_Target(&target,targetData);
        page.advanced->connected(true,true);page.advanced->targetFrame(QByteArray(reinterpret_cast<char*>(targetData),sizeof(targetData)));
        int requests=0;QObject::connect(page.advanced,&AdvancedPanel::request,[&](QByteArray){requests++;});
        functions->setCurrentIndex(1);CHECK(pages->currentIndex()==1);
        CHECK(!page.advanced->findChild<QGroupBox*>("advancedOperationGroup")->isCheckable());
        auto input=page.advanced->findChild<QPlainTextEdit*>("advancedData");input->setPlainText("AA BB CC DD");
        functions->setCurrentIndex(0);CHECK(pages->currentIndex()==0);
        CHECK(page.findChild<QTableWidget*>("nfcHistory")->rowCount()==2);
        functions->setCurrentIndex(1);CHECK(input->toPlainText()=="AA BB CC DD" && requests==0);functions->setCurrentIndex(0);
        char payload[513];nfc_test_part(41,0,"transit_japan",payload,sizeof(payload));QString error;
        CHECK(!page.receive(payload,&error));
        CHECK(page.findChild<QLabel*>("nfcType")->text()==QStringLiteral("交通卡"));
        if(app.arguments().contains("--screenshot")) {
            page.resize(980,820);page.show();app.processEvents();page.grab().save("nfc-page.png");
            functions->setCurrentIndex(1);app.processEvents();page.grab().save("nfc-advanced-page.png");
        }
    }
    std::puts("Production JSON -> CDC/HID -> Qt NFC page round trip passed");return 0;
}
