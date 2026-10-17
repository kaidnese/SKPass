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
#include <QDialog>
#include <QMessageBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>

#define CHECK(condition) do { if(!(condition)){std::fprintf(stderr,"Failed: %s (line %d)\n",#condition,__LINE__);return 1;} } while(false)
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("SKPassTests");app.setApplicationName("v1.2-ui-tests");
    QTemporaryDir data(QDir::currentPath()+"/skpass-ui-test-XXXXXX");CHECK(data.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,data.path());
    app.setProperty("dataDir",data.path());
    CardManager manager;
    auto remove=manager.findChild<QPushButton*>("deleteSavedCard");CHECK(remove && !remove->isEnabled());
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
    // Exercise deletion through the UI against an isolated JSON file.
    auto readJson=[&]{QFile file(manager.jsonPath());if(!file.open(QIODevice::ReadOnly))return QByteArray();return file.readAll();};
    bool sawDeleteDialog=false,sawSaveError=false;
    auto deleteCard=[&](bool confirm,bool expectSaveError=false){
        sawDeleteDialog=false;sawSaveError=false;
        QTimer::singleShot(0,&manager,[&,confirm,expectSaveError]{
            auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            sawDeleteDialog=dialog && dialog->objectName()=="deleteVirtualCardDialog";
            if(!sawDeleteDialog){if(dialog)dialog->reject();return;}
            if(!confirm){dialog->reject();return;}
            if(expectSaveError)QTimer::singleShot(0,&manager,[&]{
                auto error=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                sawSaveError=error!=nullptr;if(error)error->accept();
            });
            dialog->findChild<QPushButton*>("confirmDeleteVirtualCard")->click();
        });
        remove->click();
    };
    saved->item(0,2)->setText(QStringLiteral("保留的备注"));
    const auto secondCode=AimeCode::fromSerial("87654321");manager.saveNumber(secondCode);manager.record(secondCode);
    saved->item(1,2)->setText(QStringLiteral("将删除的备注"));
    for(auto button:manager.findChildren<QPushButton*>())if(button->text()==QStringLiteral("保存备注"))button->click();
    CHECK(saved->rowCount()==2);
    saved->clearSelection();CHECK(!remove->isEnabled());saved->selectRow(1);CHECK(remove->isEnabled());
    const auto beforeCancel=readJson();deleteCard(false);
    CHECK(sawDeleteDialog && saved->rowCount()==2 && readJson()==beforeCancel);
    deleteCard(true);CHECK(sawDeleteDialog && saved->rowCount()==1);
    auto root=QJsonDocument::fromJson(readJson()).object();
    CHECK(root["cards"].toArray().size()==1 && root["activeCode"].toString()==code && root["keyVk"].toInt()==13);
    CHECK(root["cards"].toArray()[0].toObject()["note"].toString()==QStringLiteral("保留的备注"));
    CHECK(!readJson().contains(secondCode.toUtf8()) && !readJson().contains(QStringLiteral("将删除的备注").toUtf8()));
    CHECK(previewTable->rowCount()==1 && previewTable->item(0,2)->text()==secondCode);
    CHECK(notice->text().contains(QStringLiteral("删除成功")) && !notice->isHidden());
    {CardManager reloaded;auto table=reloaded.findChild<QTableWidget*>("savedCards");
        CHECK(table->rowCount()==1 && table->item(0,1)->text()==code && table->item(0,0)->text().contains(QStringLiteral("已启用")));}

    // A failed atomic save must restore the card, note and active selection.
    saved->selectRow(0);const auto beforeFailure=readJson();const auto held=manager.jsonPath()+".held";
    CHECK(QFile::rename(manager.jsonPath(),held));CHECK(QDir().mkdir(manager.jsonPath()));
    deleteCard(true,true);CHECK(sawDeleteDialog && sawSaveError);
    CHECK(saved->rowCount()==1 && saved->item(0,2)->text()==QStringLiteral("保留的备注"));
    CHECK(saved->item(0,0)->text().contains(QStringLiteral("已启用")) && notice->text().contains(QStringLiteral("删除失败")));
    CHECK(QDir().rmdir(manager.jsonPath()));CHECK(QFile::rename(held,manager.jsonPath()));CHECK(readJson()==beforeFailure);
    saved->selectRow(0);deleteCard(true);CHECK(sawDeleteDialog && saved->rowCount()==0 && !remove->isEnabled());
    root=QJsonDocument::fromJson(readJson()).object();
    CHECK(root["cards"].toArray().isEmpty() && root["activeCode"].toString().isEmpty() && root["keyVk"].toInt()==13);
    CHECK(status->text().contains(QStringLiteral("未启用")) && status->styleSheet().contains("#b91c1c"));
    {CardManager reloaded;CHECK(reloaded.findChild<QTableWidget*>("savedCards")->rowCount()==0);}
    // The current game/export path must be the only file changed by deletion.
    const auto gameDir=data.path()+"/game";CHECK(QDir().mkdir(gameDir));manager.saveNumber(code);
    const auto sourceJson=readJson();const auto sourcePath=manager.jsonPath();CHECK(manager.exportToGame(gameDir));
    saved->selectRow(0);deleteCard(true);CHECK(sawDeleteDialog);
    CHECK(manager.jsonPath()==QDir(gameDir).filePath("cards.json") && QJsonDocument::fromJson(readJson()).object()["cards"].toArray().isEmpty());
    QFile source(sourcePath);CHECK(source.open(QIODevice::ReadOnly) && source.readAll()==sourceJson);
    {CardManager reloaded;CHECK(reloaded.jsonPath()==manager.jsonPath() && reloaded.findChild<QTableWidget*>("savedCards")->rowCount()==0);}
    std::puts("Shared history, multi-select, deduplication, clear and default JSON path passed.");
    std::puts("Saved card deletion, cancel, JSON persistence, active card clearing, rollback and game path passed.");
    return 0;
}
