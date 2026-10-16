#include "MainWindow.h"
#include "FirmwareUpdatePage.h"
#include "AppTheme.h"
#include <QApplication>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QEventLoop>
#include <QListWidget>
#include <cstdio>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
class SerialReaderTestAccess {
public:
    static void feed(SerialReader &r,quint8 msg,const QByteArray &payload) {
        QByteArray raw=QByteArray::fromHex("CA5A01");raw.append(char(msg));
        raw.append(char(payload.size()));raw.append(char(payload.size()>>8));raw+=payload;
        auto crc=FrameParser::crc16(raw.mid(2));raw.append(char(crc));raw.append(char(crc>>8));r.parser.feed(raw);
    }
    static void query(SerialReader &r,int timeout=3000){r.infoRequested=true;r.infoTimeout.start(timeout);}
    static void expire(SerialReader &r){QThread::msleep(5100);r.checkConnection();}
    static bool confirmed(const SerialReader &r){return r.confirmed;}
};
class MainWindowTestAccess {
public:
    static SerialReader &reader(MainWindow &w){return w.reader;}
};
class FirmwareUpdatePageTestAccess {
public:
    static void query(FirmwareUpdatePage &p){p.beginDeviceQuery();}
    static bool pending(const FirmwareUpdatePage &p){return p.infoQueryPending;}
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setOrganizationName("SKPassTests");app.setApplicationName("device-information");
    QTemporaryDir dir;CHECK(dir.isValid());app.setProperty("dataDir",dir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
    applyAppTheme(app);
    MainWindow w;auto &reader=MainWindowTestAccess::reader(w);
    auto page=w.findChild<FirmwareUpdatePage*>();CHECK(page);
    auto details=page->findChild<QLabel*>("deviceInfoDetails");auto status=page->findChild<QLabel*>("deviceInfoStatus");
    auto button=page->findChild<QPushButton*>("queryDeviceInfo");auto choose=page->findChild<QPushButton*>("chooseFirmware");
    auto connection=w.findChild<QLabel*>("connectionStatus");CHECK(details && status && button && choose && connection);
    CHECK(!w.findChild<QLabel*>("deviceFirmware"));CHECK(!w.findChild<QLabel*>("deviceNfcStatus"));
    CHECK(connection->text()==QStringLiteral("读卡器已断开"));CHECK(!button->isEnabled());
    QString error;CHECK(!reader.requestDeviceInfo(&error));CHECK(!error.isEmpty());
    QJsonObject info{{"schema",1},{"firmware","SKPass-2.5.0-retry-uf2-write"},
        {"version","2.5.0"},{"built","Oct 11 2026 16:00:00"},{"idf","v5.2.5"},
        {"target","esp32s3"},{"app_sha256",QString(64,'a')},{"usb_state","connected"},{"nfc_state","ready"}};
    auto feed=[&](const QJsonObject &data,quint8 msg=0xB1){SerialReaderTestAccess::feed(reader,msg,QJsonDocument(data).toJson(QJsonDocument::Compact));};
    auto query=[&](int timeout=3000){SerialReaderTestAccess::query(reader,timeout);FirmwareUpdatePageTestAccess::query(*page);};
    feed(info);CHECK(reader.deviceInformation().isEmpty());
    SerialReaderTestAccess::feed(reader,1,"SKPass-2.5.0-retry-uf2-write");
    CHECK(SerialReaderTestAccess::confirmed(reader));CHECK(connection->text()==QStringLiteral("读卡器已连接"));
    feed(info);feed(info,2);CHECK(reader.deviceInformation().isEmpty());CHECK(details->text().isEmpty());
    query();CHECK(!choose->isEnabled());CHECK(!button->isEnabled());
    auto invalid=info;invalid["firmware"]="other-device";feed(invalid);CHECK(reader.deviceInformation().isEmpty());
    invalid=info;invalid["schema"]=2;feed(invalid);CHECK(reader.deviceInformation().isEmpty());
    for(const auto &hash:{QString("garbage"),QString("5d3a"),QString(65,'a')}) {
        invalid=info;invalid["app_sha256"]=hash;feed(invalid);CHECK(reader.deviceInformation().isEmpty());
    }
    feed(info);CHECK(reader.deviceInformation()==info);CHECK(!FirmwareUpdatePageTestAccess::pending(*page));
    CHECK(choose->isEnabled());CHECK(details->text().contains("v2.5.0"));CHECK(details->text().contains("16:00:00"));
    CHECK(details->text().contains(QString(64,'a')));CHECK(details->text().contains(QStringLiteral("快照")));
    const auto unchanged=details->text();int changes=0;
    QObject::connect(&reader,&SerialReader::deviceInfoChanged,[&](QJsonObject){++changes;});
    auto recovering=info;recovering["nfc_state"]="recovering";feed(recovering);feed(recovering,2);
    CHECK(changes==0);CHECK(details->text()==unchanged);
    query();feed(info);CHECK(changes==1);CHECK(!FirmwareUpdatePageTestAccess::pending(*page));
    auto abbreviated=info;abbreviated["app_sha256"]="5d3a06329";
    query();feed(abbreviated,2);CHECK(reader.deviceInformation()==abbreviated);
    CHECK(details->text().contains(QStringLiteral("SHA256 前缀：5d3a06329")));
    query(10);QEventLoop loop;QTimer::singleShot(40,&loop,&QEventLoop::quit);loop.exec();
    CHECK(!FirmwareUpdatePageTestAccess::pending(*page));CHECK(choose->isEnabled());CHECK(details->text().isEmpty());
    CHECK(status->text().contains(QStringLiteral("超时")));CHECK(SerialReaderTestAccess::confirmed(reader));
    query();reader.stop();CHECK(!FirmwareUpdatePageTestAccess::pending(*page));CHECK(details->text().isEmpty());
    CHECK(reader.deviceInformation().isEmpty());CHECK(status->text().contains(QStringLiteral("中断")));
    CHECK(connection->text()==QStringLiteral("读卡器已断开"));CHECK(!button->isEnabled());
    SerialReaderTestAccess::feed(reader,1,"SKPass-2.5.0-retry-uf2-write");query();feed(info);
    SerialReaderTestAccess::expire(reader);CHECK(!SerialReaderTestAccess::confirmed(reader));
    CHECK(reader.deviceInformation().isEmpty());CHECK(details->text().isEmpty());CHECK(!reader.canWrite() && !reader.canUpdate());
    auto legacy=info;legacy["firmware"]="pn7150_cardio-2.4.0-retry-uf2-write";legacy["version"]="2.4.0";
    SerialReaderTestAccess::feed(reader,1,"pn7150_cardio-2.4.0-retry-uf2-write");
    CHECK(SerialReaderTestAccess::confirmed(reader));query();feed(legacy);CHECK(reader.deviceInformation()==legacy);reader.stop();
    // Render a queried snapshot without opening a port or modifying user settings.
    if(argc>1){w.show();auto nav=w.findChild<QListWidget*>("navigation");if(nav)nav->setCurrentRow(nav->count()-2);
        SerialReaderTestAccess::feed(reader,1,"SKPass-2.5.0-retry-uf2-write");query();feed(info);
        app.processEvents();w.grab().save(QString::fromLocal8Bit(argv[1]));}
    std::puts("Device information tests passed: on-demand replies, legacy prefixes, validation, repeated queries, timeout and disconnection.");
    return 0;
}
