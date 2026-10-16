#include "RomDownloadPanel.h"
#include "FirmwareUpdatePage.h"
#include "SerialReader.h"
#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>
#include <QTabWidget>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAILED line %d: %s\n",__LINE__,#x);return 1;}} while(false)
class RomDownloadPanelTestAccess {
public:
    static void busy(RomDownloadPanel &p){p.busy=true;p.refresh();}
    static void pending(RomDownloadPanel &p){p.requested=true;p.refresh();}
    static void finish(RomDownloadPanel &p,const QString &error,bool committed){p.complete(error,committed);}
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);SerialReader reader;RomDownloadPanel panel(&reader);
    auto enter=panel.findChild<QPushButton*>("enterRomDownload"),choose=panel.findChild<QPushButton*>("chooseRomFirmware"),flash=panel.findChild<QPushButton*>("flashRomFirmware");
    CHECK(enter && choose && flash && !enter->isEnabled() && choose->isEnabled() && !flash->isEnabled());
    CHECK(!panel.findChild<QPushButton*>("romReconnect"));
    CHECK(flash->text()==QStringLiteral("3. 开始烧录"));
    auto transport=panel.findChild<QComboBox*>("romTransport"),port=panel.findChild<QComboBox*>("romPort");
    transport->setCurrentIndex(1);port->setCurrentIndex(-1);CHECK(!flash->isEnabled());transport->setCurrentIndex(0);
    panel.setExternalBusy(true);CHECK(!choose->isEnabled());panel.setExternalBusy(false);CHECK(choose->isEnabled());
    RomDownloadPanelTestAccess::busy(panel);CHECK(panel.isBusy() && !choose->isEnabled());
    RomDownloadPanelTestAccess::finish(panel,"transport failed",false);CHECK(!panel.isBusy() && choose->isEnabled());
    CHECK(panel.findChild<QLabel*>("romStatus")->text().contains("transport failed"));
    CHECK(panel.findChild<QLabel*>("romStatus")->text().contains(QStringLiteral("重新插拔 USB")));
    RomDownloadPanelTestAccess::pending(panel);reader.frame(0xB4,QByteArray(1,char(1)),{});
    CHECK(!panel.isBusy() && panel.findChild<QLabel*>("romStatus")->text().contains(QStringLiteral("正在写卡")));
    RomDownloadPanelTestAccess::pending(panel);reader.frame(0xB4,QByteArray(1,char(3)),{});
    CHECK(!panel.isBusy() && panel.findChild<QLabel*>("romStatus")->text().contains("eFuse"));
    RomDownloadPanelTestAccess::busy(panel);RomDownloadPanelTestAccess::finish(panel,{},true);
    CHECK(panel.findChild<QProgressBar*>("romProgress")->value()==100);
    CHECK(!panel.isBusy() && choose->isEnabled());
    const auto completion=panel.findChild<QLabel*>("romStatus")->text();
    CHECK(completion.contains(QStringLiteral("烧录与校验完成")) && completion.contains(QStringLiteral("重新插拔 USB")));
    CHECK(!completion.contains(QStringLiteral("自动复位")) && !completion.contains(QStringLiteral("已重新连接")));
    // A user may leave the device in ROM indefinitely; do not replace the
    // unplug/replug instruction with a reset timeout or lock the file chooser.
    for(auto timer:panel.findChildren<QTimer*>())CHECK(timer->interval()!=20000);
    reader.stop();CHECK(panel.findChild<QLabel*>("romStatus")->text()==completion && choose->isEnabled());
    FirmwareUpdatePage page(&reader);page.show();auto tabs=page.findChild<QTabWidget*>("firmwareMethods");CHECK(tabs && tabs->count()==2);
    tabs->setCurrentIndex(1);QTemporaryDir temp;CHECK(temp.isValid());QFile file(temp.filePath("bad.bin"));CHECK(file.open(QIODevice::WriteOnly));file.write("bad firmware");file.close();
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(file.fileName())});
    QDragEnterEvent drag(QPoint(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&page,&drag);CHECK(drag.isAccepted());
    QDropEvent drop(QPointF(10,10),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&page,&drop);CHECK(drop.isAccepted());
    CHECK(!page.updateInProgress());CHECK(page.findChild<QLabel*>("romStatus")->text().contains(QStringLiteral("应用固件")));
    QString error;CHECK(!reader.enterRomDownload(false,&error));CHECK(error.contains("2.7.0"));
    std::puts("ROM UI rejection, completion, busy recovery, drop routing and legacy firmware guard passed");
}
