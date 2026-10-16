#include "MainWindow.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QSettings>
#include <QListWidget>
#include <QStackedWidget>
#include <cstdio>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
class MainWindowTestAccess {
public:
    static void receive(MainWindow &w,quint8 message,const QByteArray &p){w.receive(message,p,{});}
    static const QString &code(const MainWindow &w){return w.code;}
    static QTimer *timer(MainWindow &w){return w.previewTimer;}
    static bool canCopy(const MainWindow &w){return w.copyCodeButton->isEnabled();}
    static QStringList overview(const MainWindow &w){return {w.codeLabel->text(),w.uidLabel->text(),w.details->text(),w.errors->text(),w.previewHint->text()};}
    static int scanCount(const MainWindow &w){return w.rows.size();}
};
static QByteArray event(quint8 status,const QByteArray &uid,const QByteArray &blocks={}) {
    QByteArray result;result.append(char(0x10));result.append(char(status));
    auto id=uid.left(8);id.append(QByteArray(8-id.size(),0));result+=id;
    result+=QByteArray(8,0);result.append(char(0));result.append(char(blocks.size()));result+=blocks;return result;
}
static void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setOrganizationName("SKPassTests");app.setApplicationName("preview-lifetime");
    QTemporaryDir temp;CHECK(temp.isValid());app.setProperty("dataDir",temp.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    MainWindow w;
    auto codeLabel=w.findChild<QLabel*>("currentAccessCode");auto uidLabel=w.findChild<QLabel*>("currentPhysicalId");
    CHECK(codeLabel && uidLabel);CHECK(codeLabel->text()==QStringLiteral("未取得卡号"));CHECK(!MainWindowTestAccess::canCopy(w));
    CHECK(MainWindowTestAccess::timer(w)->interval()==10000);
    auto blocks=QByteArray::fromHex("1DB40F0DAB080400C832002000000016"
        "00000000000000000000000000000000" "00000000000010114514191981023333");
    const auto id=QByteArray::fromHex("1DB40F0D");
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=1DB40F0D mode=00 protocol=80 interface=80 sak=08");
    MainWindowTestAccess::receive(w,0x10,event(0,id,blocks));
    CHECK(MainWindowTestAccess::code(w)=="10114514191981023333");CHECK(MainWindowTestAccess::canCopy(w));
    CHECK(uidLabel->text()==QStringLiteral("UID：1D B4 0F 0D"));
    CHECK(MainWindowTestAccess::timer(w)->isActive());
    const auto visibleCode=codeLabel->text();
    MainWindowTestAccess::timer(w)->start(10);
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=1DB40F0D mode=00 protocol=80 interface=80 sak=08");
    CHECK(codeLabel->text()==visibleCode);CHECK(MainWindowTestAccess::canCopy(w));
    wait(30);CHECK(codeLabel->text()==visibleCode); // Same-card activation extends expiry.
    MainWindowTestAccess::receive(w,0x10,event(0,id,blocks));
    CHECK(codeLabel->text()==visibleCode);
    // CARD_REMOVED does not clear the preview; an immediate same-card re-read stays steady.
    MainWindowTestAccess::receive(w,0x11,{});
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=1DB40F0D mode=00 protocol=80 interface=80 sak=08");
    CHECK(codeLabel->text()==visibleCode);
    // Expiry clears the current result and copy action, preserving scan history.
    MainWindowTestAccess::timer(w)->start(30);wait(60);
    CHECK(MainWindowTestAccess::code(w).isEmpty());CHECK(!MainWindowTestAccess::canCopy(w));
    CHECK(codeLabel->text()==QStringLiteral("未取得卡号"));CHECK(uidLabel->text()==QStringLiteral("UID / IDm：—"));
    bool retained=false;for(auto table:w.findChildren<QTableWidget*>())for(int row=0;row<table->rowCount();++row)
        for(int col=0;col<table->columnCount();++col)if(table->item(row,col) && table->item(row,col)->text()=="10114514191981023333")retained=true;
    CHECK(retained);
    // A failed scan of a different card must never display the previous code/UID.
    MainWindowTestAccess::receive(w,0x10,event(0,id,blocks));
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=04112233445566 mode=00 protocol=80 interface=80 sak=08");
    CHECK(MainWindowTestAccess::code(w).isEmpty());
    MainWindowTestAccess::receive(w,0x10,event(0x12,QByteArray::fromHex("04112233445566")));
    CHECK(codeLabel->text()==QStringLiteral("未取得卡号"));CHECK(!MainWindowTestAccess::canCopy(w));
    CHECK(uidLabel->text()==QStringLiteral("UID：04 11 22 33 44 55 66"));
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=04112233445566778899 mode=00 protocol=80 interface=80 sak=08");
    MainWindowTestAccess::receive(w,0x10,event(0x13,QByteArray::fromHex("04112233445566778899")));
    CHECK(uidLabel->text()==QStringLiteral("UID：04 11 22 33 44 55 66 77 88 99"));
    // A new result restarts the timeout; no old singleShot can clear it early.
    MainWindowTestAccess::timer(w)->start(10);
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=1DB40F0D mode=00 protocol=80 interface=80 sak=08");
    MainWindowTestAccess::receive(w,0x10,event(0,id,blocks));wait(35);
    CHECK(!MainWindowTestAccess::code(w).isEmpty());
    // An actual failed result for the same card must still remove the old code.
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=1DB40F0D mode=00 protocol=80 interface=80 sak=08");
    CHECK(!MainWindowTestAccess::code(w).isEmpty());
    MainWindowTestAccess::receive(w,0x10,event(0x12,id));
    CHECK(MainWindowTestAccess::code(w).isEmpty());CHECK(!MainWindowTestAccess::canCopy(w));
    CHECK(uidLabel->text()==QStringLiteral("UID：1D B4 0F 0D"));
    // Replay the user's recovery loop: the same card repeatedly fails reading.
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=0B58884F mode=00 protocol=80 interface=80 sak=08");
    MainWindowTestAccess::receive(w,0x10,event(0x13,QByteArray::fromHex("0B58884F")));
    const auto failureView=MainWindowTestAccess::overview(w);
    const auto scanCount=MainWindowTestAccess::scanCount(w);
    for(int i=0;i<3;++i) {
        MainWindowTestAccess::receive(w,0x7f,"NFC_RECOVERY: hardware reset; initializing PN7150");
        MainWindowTestAccess::receive(w,0x7f,"NFC_READY: RF Discovery initialized");
        MainWindowTestAccess::receive(w,0x7f,"TAG uid=0B58884F mode=00 protocol=80 interface=80 sak=08");
        CHECK(MainWindowTestAccess::overview(w)==failureView);
        MainWindowTestAccess::receive(w,0x7f,"MIFARE block=00 result=ESP_OK raw=10B0");
        MainWindowTestAccess::receive(w,0x10,event(0x13,QByteArray::fromHex("0B58884F")));
        CHECK(MainWindowTestAccess::overview(w)==failureView);
    }
    CHECK(MainWindowTestAccess::scanCount(w)==scanCount+3);
    // A changed outcome must still be reported even for the same UID.
    MainWindowTestAccess::receive(w,0x10,event(0x12,QByteArray::fromHex("0B58884F")));
    CHECK(MainWindowTestAccess::overview(w)!=failureView);
    MainWindowTestAccess::receive(w,0x10,event(0,QByteArray::fromHex("0B58884F"),blocks));
    CHECK(MainWindowTestAccess::code(w)=="10114514191981023333");
    const auto successView=MainWindowTestAccess::overview(w);
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=0B58884F mode=00 protocol=80 interface=80 sak=08");
    CHECK(MainWindowTestAccess::overview(w)==successView);
    MainWindowTestAccess::receive(w,0x10,event(0,QByteArray::fromHex("0B58884F"),blocks));
    CHECK(MainWindowTestAccess::overview(w)==successView);
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=04112233445566 mode=00 protocol=80 interface=80 sak=08");
    CHECK(MainWindowTestAccess::code(w).isEmpty());
    CHECK(MainWindowTestAccess::overview(w)!=successView);
    MainWindowTestAccess::timer(w)->start(20);wait(40);
    CHECK(uidLabel->text()==QStringLiteral("UID / IDm：—"));
    // FeliCa repeated reads preserve the IDm and code in the same way.
    const QByteArray idm=QByteArray::fromHex("011E65740A055390");
    QByteArray felica;felica.append(char(0x20));felica.append(char(0));felica+=idm;felica+=QByteArray(8,0);
    felica.append(char(16));felica+=QByteArray::fromHex("d276519cb52c008b0227155ed7d2bed9");felica.append(char(0));
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=011E65740A055390 mode=02 protocol=03 interface=01 sak=00");
    MainWindowTestAccess::receive(w,0x10,felica);
    CHECK(MainWindowTestAccess::code(w)=="01035640268054098707");
    const auto felicaCode=codeLabel->text();
    MainWindowTestAccess::receive(w,0x7f,"TAG uid=011E65740A055390 mode=02 protocol=03 interface=01 sak=00");
    CHECK(codeLabel->text()==felicaCode);CHECK(uidLabel->text()==QStringLiteral("IDm：01 1E 65 74 0A 05 53 90"));
    // A lengthy same-card block scan must not expire the previous result.
    MainWindowTestAccess::timer(w)->start(10);
    MainWindowTestAccess::receive(w,0x7f,"FELICA idm=011E65740A055390 service=0900 block=00 result=ESP_OK data=0000");
    wait(30);CHECK(codeLabel->text()==felicaCode);
    // An unrelated card's diagnostics must not extend this preview's lifetime.
    MainWindowTestAccess::timer(w)->start(10);
    MainWindowTestAccess::receive(w,0x7f,"FELICA idm=9988776655443322 service=0900 block=00 result=ESP_OK data=0000");
    wait(30);CHECK(MainWindowTestAccess::code(w).isEmpty());
    // Public NFC documents have their own page and never become virtual cards
    // or clear a same-card arcade preview while fragments are arriving.
    auto navigation=w.findChild<QListWidget*>("navigation");
    CHECK(navigation && navigation->count()==7 && navigation->item(4)->text()=="NFC");
    navigation->setCurrentRow(4);
    auto nfc=w.findChild<QWidget*>("nfcPage");CHECK(nfc && nfc->parentWidget());
    const auto overview=MainWindowTestAccess::overview(w);
    MainWindowTestAccess::receive(w,0x12,"{\"schema\":1,\"scan\":10,\"uid\":\"011E65740A055390\",\"part\":0,\"count\":1,\"kind\":\"transit_japan\",\"status\":\"ok\",\"protocol\":3,\"interface\":1,\"currency\":\"JPY\",\"balance\":100}");
    CHECK(nfc->findChild<QLabel*>("nfcType")->text()==QStringLiteral("交通卡"));
    CHECK(MainWindowTestAccess::overview(w)==overview);
    MainWindowTestAccess::timer(w)->stop();
    std::puts("Preview expiry, retained history, exact UID and failed-scan tests passed");
}
