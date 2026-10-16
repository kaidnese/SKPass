#include "CardManager.h"
#include "AimeCode.h"
#include <QApplication>
#include <QSettings>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

#define CHECK(condition) do { if(!(condition)){std::fprintf(stderr,"Failed: %s (line %d)\n",#condition,__LINE__);return 1;} } while(false)
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("SKPassTests");app.setApplicationName("v1.2-ui-tests");
    QTemporaryDir data(QDir::currentPath()+"/skpass-ui-test-XXXXXX");CHECK(data.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
    app.setProperty("dataDir",data.path());
    CardManager manager;
    auto preview=manager.createHistoryView(&manager);
    auto previewTable=preview->findChild<QTableWidget*>();
    auto tables=manager.findChildren<QTableWidget*>();
    CHECK(tables.size()==3);
    QTableWidget *history=nullptr;
    for(auto table:tables)if(table!=previewTable && table->horizontalHeaderItem(0)->text()==QStringLiteral("选择"))history=table;
    CHECK(history);
    for(int i=0;i<12;i++)manager.record(AimeCode::fromSerial(QString::number(10000000+i)));
    CHECK(history->rowCount()==10 && previewTable->rowCount()==10);
    CHECK(history->item(0,2)->text()==AimeCode::fromSerial("10000011"));
    auto selected=previewTable->item(2,2)->text();
    previewTable->item(2,0)->setCheckState(Qt::Checked);
    CHECK(history->item(2,0)->checkState()==Qt::Checked);
    history->item(4,0)->setCheckState(Qt::Checked);
    CHECK(previewTable->item(4,0)->checkState()==Qt::Checked);
    manager.record(selected);
    CHECK(history->rowCount()==10 && history->item(0,2)->text()==selected);
    CHECK(previewTable->item(0,0)->checkState()==Qt::Checked);
    bool defaultPath=false;
    for(auto label:manager.findChildren<QLabel*>())if(label->text().contains(QDir(data.path()).filePath("cards.json")))defaultPath=true;
    CHECK(defaultPath);
    for(auto button:preview->findChildren<QPushButton*>())if(button->text()==QStringLiteral("清空记录"))button->click();
    CHECK(history->rowCount()==0 && previewTable->rowCount()==0);
    auto status=manager.findChild<QLabel*>("virtualCardStatus");CHECK(status->text().contains(QStringLiteral("未启用")));CHECK(status->styleSheet().contains("#b91c1c"));
    auto serial=manager.findChild<QLineEdit*>("virtualSerial");auto generate=manager.findChild<QPushButton*>("generateVirtualCode");auto manual=manager.findChild<QLineEdit*>("manualAccessCode");
    serial->setText("1234567");CHECK(!generate->isEnabled());serial->setText("12345678");CHECK(generate->isEnabled());generate->click();CHECK(manual->text()==AimeCode::fromSerial("12345678"));
    auto save=manager.findChild<QPushButton*>("saveManualCode");CHECK(save->isEnabled());
    auto code=manual->text();manual->setText(code.left(4)+" "+code.mid(4,4)+"-"+code.mid(8));CHECK(save->isEnabled());save->click();
    auto saved=manager.findChild<QTableWidget*>("savedCards");CHECK(saved->rowCount()==1 && saved->item(0,1)->text()==code);saved->selectRow(0);
    for(auto button:manager.findChildren<QPushButton*>())if(button->text()==QStringLiteral("启用选中虚拟卡"))button->click();
    CHECK(status->styleSheet().contains("#166534"));CHECK(saved->item(0,0)->text().contains(QStringLiteral("已启用")));CHECK(saved->item(0,1)->background().color()==QColor("#dcfce7"));
    auto notice=manager.findChild<QLabel*>("jsonSaveNotice");CHECK(!notice->isHidden() && !notice->text().contains(":"));
    QEventLoop wait;QTimer::singleShot(4400,&wait,&QEventLoop::quit);wait.exec();CHECK(notice->isHidden());
    manual->setText(QString(20,'0'));CHECK(!save->isEnabled());
    std::puts("Shared history, multi-select, deduplication, clear and default JSON path passed.");
    return 0;
}
