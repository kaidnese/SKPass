#include "FirmwareUpdatePage.h"
#include "SerialReader.h"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QEventLoop>
#include <QUrl>
#include <QFrame>
#include <QDragLeaveEvent>
#include <cstdio>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
class FirmwareUpdatePageTestAccess {
public:
    static void busy(FirmwareUpdatePage &p){p.busy=true;p.requested=true;p.refreshButtons();}
    static void finish(FirmwareUpdatePage &p,const QString &error,bool automatic=false){p.finishUpdate(error,automatic);}
    static void pending(FirmwareUpdatePage &p){p.requested=true;p.refreshButtons();p.requestTimeout->start(10);}
    static bool idle(const FirmwareUpdatePage &p){return !p.busy && !p.requested;}
    static bool canEnter(const FirmwareUpdatePage &p){return p.enter->isEnabled();}
};
static bool accepts(FirmwareUpdatePage &page,const QList<QUrl> &urls){
    QMimeData mime;mime.setUrls(urls);
    QDragEnterEvent event(QPoint(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&page,&event);return event.isAccepted();
}
int main(int argc,char **argv){
    QApplication app(argc,argv);SerialReader reader;FirmwareUpdatePage page(&reader);page.show();
    auto choose=page.findChild<QPushButton*>("chooseFirmware");
    auto query=page.findChild<QPushButton*>("queryDeviceInfo");CHECK(query && !query->isEnabled());
    auto status=page.findChild<QLabel*>("firmwareStatus");
    int completed=0;bool success=false;
    QObject::connect(&page,&FirmwareUpdatePage::updateFinished,[&](bool ok){++completed;success=ok;});
    FirmwareUpdatePageTestAccess::busy(page);CHECK(!choose->isEnabled());
    CHECK(!query->isEnabled());
    CHECK(!accepts(page,{QUrl::fromLocalFile("C:/firmware.bin")}));
    FirmwareUpdatePageTestAccess::finish(page,{});
    CHECK(FirmwareUpdatePageTestAccess::idle(page));CHECK(choose->isEnabled());CHECK(success);
    CHECK(page.findChild<QProgressBar*>("uf2Progress")->value()==100);
    CHECK(!FirmwareUpdatePageTestAccess::canEnter(page)); // CDC is offline until replug/reconnect.
    CHECK(status->text().contains(QStringLiteral("更新成功")));
    FirmwareUpdatePageTestAccess::busy(page);FirmwareUpdatePageTestAccess::finish(page,"Device ERROR");
    CHECK(FirmwareUpdatePageTestAccess::idle(page));CHECK(choose->isEnabled());CHECK(!success);
    CHECK(status->text()=="Device ERROR");
    CHECK(accepts(page,{QUrl::fromLocalFile("C:/firmware.BIN")}));
    auto zone=page.findChild<QFrame*>("firmwareDropZone");CHECK(zone);
    CHECK(zone->property("dragActive").toBool());
    QDragLeaveEvent leave;QApplication::sendEvent(&page,&leave);
    CHECK(!zone->property("dragActive").toBool());
    CHECK(accepts(page,{QUrl::fromLocalFile("C:/firmware.uf2")}));
    CHECK(!accepts(page,{QUrl::fromLocalFile("C:/firmware.txt")}));
    CHECK(!zone->property("dragActive").toBool());
    CHECK(!accepts(page,{QUrl("https://example.com/firmware.bin")}));
    CHECK(!accepts(page,{QUrl::fromLocalFile("C:/a.bin"),QUrl::fromLocalFile("C:/b.uf2")}));
    FirmwareUpdatePageTestAccess::pending(page);CHECK(!choose->isEnabled());
    QEventLoop timeoutLoop;QTimer::singleShot(30,&timeoutLoop,&QEventLoop::quit);timeoutLoop.exec();
    CHECK(FirmwareUpdatePageTestAccess::idle(page));CHECK(choose->isEnabled());
    QTemporaryDir temp;CHECK(temp.isValid());const auto path=temp.filePath("invalid.bin");
    QFile f(path);CHECK(f.open(QIODevice::WriteOnly));f.write("Not an ESP32 image");f.close();
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent drag(QPoint(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&page,&drag);CHECK(drag.isAccepted());
    QDropEvent drop(QPointF(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&page,&drop);CHECK(drop.isAccepted());CHECK(!choose->isEnabled());
    QEventLoop loop;bool timedOut=false;
    QObject::connect(&page,&FirmwareUpdatePage::updateFinished,&loop,&QEventLoop::quit);
    QTimer::singleShot(3000,&loop,[&]{timedOut=true;loop.quit();});loop.exec();
    CHECK(!timedOut);CHECK(completed==3);CHECK(!success);
    CHECK(FirmwareUpdatePageTestAccess::idle(page));CHECK(choose->isEnabled());
    // COMPLETE from a new updater resumes connection on the retry timer.
    // Finish and stop synchronously so this test never opens real hardware.
    SerialReader::ConnectionState connection=SerialReader::ConnectionState::Disconnected;
    QObject::connect(&reader,&SerialReader::connectionChanged,[&](auto state,const QString &){connection=state;});
    FirmwareUpdatePageTestAccess::busy(page);FirmwareUpdatePageTestAccess::finish(page,{},true);
    CHECK(success);CHECK(FirmwareUpdatePageTestAccess::idle(page));
    CHECK(status->text().contains(QStringLiteral("自动重启")));
    CHECK(connection==SerialReader::ConnectionState::Connecting);
    reader.stop();
    std::puts("Firmware UI completion, auto-reconnect, timeout and drag/drop tests passed");
}
