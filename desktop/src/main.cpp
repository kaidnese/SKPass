#include <QApplication>
#include <QTimer>
#include "MainWindow.h"
#include "AppTheme.h"
#include "CardDialogs.h"
#include "CardManager.h"
#include "AimeCode.h"
#include <QListWidget>
#include <QDir>
#include <QSettings>
#include <QJsonObject>
#include <QTabWidget>
#include <QTabBar>
#include <QIcon>
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setApplicationName("SKPass v1.0");app.setOrganizationName("SKPass");
    app.setWindowIcon(QIcon(":/icons/app.png"));
    const auto args=app.arguments();
    // Preserve the current developer installation's preferences on first launch.
    if(!args.contains("--smoke-test") && !args.contains("--ui-preview") && !QSettings().contains("cardsJson")) {
        QSettings previous("SKPass", "SKPass v1.9");
        for(const auto &key : {"cardsJson", "segatoolsDirectory"})
            if(previous.contains(key))QSettings().setValue(key, previous.value(key));
    }
    const auto dataDirArg=args.indexOf("--data-dir");
    if(dataDirArg>=0 && dataDirArg+1<args.size()) {
        app.setProperty("dataDir",QDir(args[dataDirArg+1]).absolutePath());
    }
    applyAppTheme(app);
    MainWindow window;window.show();
    if(!args.contains("--smoke-test") && !args.contains("--ui-preview"))window.connectAutomatically();
    auto preview=app.arguments().indexOf("--ui-preview");
    if(preview>=0 && preview+1<app.arguments().size()) {
        auto directory=app.arguments()[preview+1];
        for(auto widget:window.findChildren<QWidget*>())if(auto manager=dynamic_cast<CardManager*>(widget)) {
            for(int i=0;i<10;i++)manager->record(AimeCode::fromSerial(QString::number(10000000+i)));
        }
        QTimer::singleShot(100,&app,[&app,&window,directory]{
            QDir().mkpath(directory);auto navigation=window.findChild<QListWidget*>("navigation");
            for(int i=0;navigation && i<navigation->count();++i){
                navigation->setCurrentRow(i);app.processEvents();window.grab().save(QDir(directory).filePath(QString("page-%1.png").arg(i)));
                if(i==4)if(auto functions=window.findChild<QTabBar *>("nfcFunctions")){
                    functions->setCurrentIndex(2);app.processEvents();window.grab().save(QDir(directory).filePath("nfc-card-emulation.png"));functions->setCurrentIndex(0);
                }
                if(i==5)if(auto methods=window.findChild<QTabWidget*>("firmwareMethods")) {
                    methods->setCurrentIndex(1);app.processEvents();window.grab().save(QDir(directory).filePath("firmware-rom.png"));methods->setCurrentIndex(0);
                }
            }
            if(navigation)navigation->setCurrentRow(0);
            // Render state examples without opening a device during UI preview.
            for(const auto state:{SerialReader::ConnectionState::Connected,SerialReader::ConnectionState::AwaitingHeartbeat,SerialReader::ConnectionState::Disconnected}) {
                QMetaObject::invokeMethod(&window,"renderConnection",Qt::DirectConnection,
                    Q_ARG(SerialReader::ConnectionState,state),
                    Q_ARG(QString,QStringLiteral("COM46 · SKPass 读卡器（界面预览）")));
                app.processEvents();window.grab().save(QDir(directory).filePath(QString("connection-%1.png").arg(int(state))));
            }
            {CardSavedDialog dialog(&window,5,2,QStringLiteral("C:/SKPass/cards.json"));dialog.show();app.processEvents();dialog.grab().save(QDir(directory).filePath("saved-dialog.png"));dialog.close();}
            {CardKeyDialog dialog(&window,13);dialog.show();app.processEvents();dialog.grab().save(QDir(directory).filePath("key-dialog.png"));dialog.close();}
            app.quit();
        });
    }
    if(app.arguments().contains("--smoke-test"))QTimer::singleShot(100,&app,&QCoreApplication::quit);
    return app.exec();
}
