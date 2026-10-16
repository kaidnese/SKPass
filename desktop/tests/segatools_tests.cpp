#include "CardManager.h"
#include "SegatoolsEditor.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x);return 1;}}while(false)
static QByteArray read(const QString &p){QFile f(p);f.open(QIODevice::ReadOnly);return f.readAll();}
static bool put(const QString &p,const QByteArray &b){QFile f(p);return f.open(QIODevice::WriteOnly)&&f.write(b)==b.size();}
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setOrganizationName("SKPassTests");app.setApplicationName("segatools-v1.3");
    QTemporaryDir tmp(QDir::currentPath()+"/segatools-test-XXXXXX");CHECK(tmp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,tmp.path());QSettings().clear();
    app.setProperty("dataDir",tmp.path());CardManager manager;manager.saveNumber("50100000000000000000"); // Synthetic, never a user's printed card.
    auto old=manager.jsonPath();
    for(int arch=0;arch<2;++arch){
        QString game=tmp.path()+QString("/game%1").arg(arch);CHECK(QDir().mkpath(game));
        QByteArray original="\xef\xbb\xbf; preserve comment\r\n[other]\r\nport=COM19\r\n[aimeio]\r\npath = old.dll ; preserve this\r\n[aime]\r\nenable=0\r\n";
        CHECK(put(game+"/segatools.ini",original));SegatoolsEditor editor(&manager);
        editor.findChild<QLineEdit*>("segatoolsDirectory")->setText(game);
        editor.findChild<QLineEdit*>("segatoolsDllName")->setText("my_reader");
        editor.findChild<QComboBox*>("segatoolsArchitecture")->setCurrentIndex(arch);
        editor.findChild<QPushButton*>("segatoolsApply")->click();
        CHECK(editor.findChild<QLabel*>("segatoolsStatus")->text().startsWith(QStringLiteral("安装成功")));
        CHECK(manager.jsonPath()==game+"/cards.json");CHECK(read(manager.jsonPath()).contains("50100000000000000000"));
        auto ini=read(game+"/segatools.ini");CHECK(ini.startsWith("\xef\xbb\xbf"));CHECK(ini.contains("path =my_reader.dll ; preserve this"));CHECK(ini.contains("port=COM19\r\n"));CHECK(ini.contains("cardsPath=cards.json")||ini.contains("cardspath=cards.json"));CHECK(ini.contains("enable=1"));
        auto backups=QDir(game).entryList({"segatools.ini*.bak"},QDir::Files);CHECK(backups.size()==1);CHECK(read(game+"/"+backups[0])==original);
        auto dll=read(game+"/my_reader.dll"),resource=read(arch?":/aimeio/x86.dll":":/aimeio/x64.dll");CHECK(dll==resource&&!dll.isEmpty());
    }
    QString invalid=tmp.path()+"/invalid";CHECK(QDir().mkpath(invalid));QByteArray duplicates="[aimeio]\npath=a.dll\npath=b.dll\n";CHECK(put(invalid+"/segatools.ini",duplicates));
    SegatoolsEditor bad(&manager);bad.findChild<QLineEdit*>("segatoolsDirectory")->setText(invalid);bad.findChild<QPushButton*>("segatoolsApply")->click();CHECK(read(invalid+"/segatools.ini")==duplicates);CHECK(!QFile::exists(invalid+"/aimeio_skpass.dll"));
    auto last=manager.jsonPath();CardManager reloaded;CHECK(reloaded.jsonPath()==last);
    QSettings().clear();std::puts("INI preservation, backups, x86/x64 extraction, JSON switching and duplicate rejection passed");return 0;
}
