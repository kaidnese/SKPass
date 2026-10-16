#include "SegatoolsEditor.h"
#include "CardManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif

static QByteArray readFile(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))throw f.errorString();return f.readAll();}
static void writeFile(const QString &path,const QByteArray &bytes) {QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit())throw f.errorString();}
static bool runningIn(const QString &directory) {
#ifdef Q_OS_WIN
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(Process32FirstW(snapshot,&entry))do {
        HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!p)continue;wchar_t path[32768];DWORD size=32768;
        if(QueryFullProcessImageNameW(p,0,path,&size)) {
            QString exe=QString::fromWCharArray(path);
            found=entry.th32ProcessID!=GetCurrentProcessId() &&
                QDir(QFileInfo(exe).absolutePath())==QDir(directory);
        }
        CloseHandle(p);if(found)break;
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return found;
#else
    return false;
#endif
}
SegatoolsEditor::SegatoolsEditor(CardManager *m,QWidget *parent):QWidget(parent),manager(m) {
    auto l=new QVBoxLayout(this);l->setContentsMargins(0,8,0,0);l->setSpacing(14);
    auto title=new QLabel(QStringLiteral("segatools 编辑"));title->setProperty("role","pageTitle");l->addWidget(title);
    auto hint=new QLabel(QStringLiteral("选择 segatools.ini 所在目录，安装 AimeIO 并同步虚拟卡。应用前自动备份，保留原配置的注释与编码。"));hint->setWordWrap(true);hint->setProperty("role","muted");l->addWidget(hint);
    auto row=new QHBoxLayout;directory=new QLineEdit(QSettings().value("segatoolsDirectory").toString());directory->setPlaceholderText(QStringLiteral("请选择 segatools.ini 所在目录"));auto browse=new QPushButton(QStringLiteral("选择游戏目录"));row->addWidget(directory,1);row->addWidget(browse);l->addLayout(row);
    directory->setObjectName("segatoolsDirectory");
    auto form=new QFormLayout;dllName=new QLineEdit("aimeio_skpass");dllName->setObjectName("segatoolsDllName");dllName->setPlaceholderText(QStringLiteral("只输入文件名，后缀自动添加"));
    auto nameRow=new QHBoxLayout;nameRow->addWidget(dllName,1);auto suffix=new QLabel(".dll");suffix->setObjectName("segatoolsDllSuffix");suffix->setStyleSheet("font-family:Consolas;font-weight:600;padding:8px;");nameRow->addWidget(suffix);
    architecture=new QComboBox;architecture->setObjectName("segatoolsArchitecture");architecture->addItem(QStringLiteral("64 位 · x64"),"x64");architecture->addItem(QStringLiteral("32 位 · x86"),"x86");form->addRow(QStringLiteral("DLL 文件名"),nameRow);form->addRow(QStringLiteral("加载进程位数"),architecture);l->addLayout(form);
    auto note=new QLabel(QStringLiteral("按实际加载 AimeIO 的进程选择位数；游戏画面进程的位数可能与加载进程不同。\n应用后，JSON 的读取和保存都会使用该游戏目录的 cards.json；已有文件会先备份。"));note->setWordWrap(true);note->setProperty("role","muted");l->addWidget(note);
    auto actions=new QHBoxLayout;auto inspect=new QPushButton(QStringLiteral("读取并预览修改"));auto install=new QPushButton(QStringLiteral("放入 DLL · 备份并应用"));install->setProperty("role","primary");auto undo=new QPushButton(QStringLiteral("恢复 INI 备份"));actions->addWidget(inspect);actions->addWidget(install);actions->addWidget(undo);l->addLayout(actions);
    inspect->setObjectName("segatoolsPreview");install->setObjectName("segatoolsApply");
    status=new QLabel;status->setObjectName("segatoolsStatus");status->setWordWrap(true);l->addWidget(status);preview=new QPlainTextEdit;preview->setReadOnly(true);l->addWidget(preview,1);
    connect(browse,&QPushButton::clicked,this,[this]{auto d=QFileDialog::getExistingDirectory(this,QStringLiteral("选择 segatools.ini 所在目录"),directory->text());if(!d.isEmpty()){directory->setText(d);prepare();}});
    connect(inspect,&QPushButton::clicked,this,[this]{prepare();});connect(install,&QPushButton::clicked,this,[this]{apply();});connect(undo,&QPushButton::clicked,this,[this]{restore();});
    connect(directory,&QLineEdit::textChanged,this,[this]{preview->clear();});connect(dllName,&QLineEdit::textChanged,this,[this]{preview->clear();});
}
bool SegatoolsEditor::prepare() {
    try {
        if(directory->text().trimmed().isEmpty()||!QDir(directory->text()).exists())throw QStringLiteral("请先选择有效的游戏目录。");
        QString base=dllName->text().trimmed(),name=base+".dll";
        if(!QRegularExpression("^[A-Za-z0-9_-]+$").match(base).hasMatch())throw QStringLiteral("文件名只能包含字母、数字、下划线或短横线。.dll 后缀由程序自动添加，请勿输入。");
        if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\\.",QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())throw QStringLiteral("该文件名是 Windows 保留名称，请换一个名称。");
        iniPath=QDir(directory->text()).absoluteFilePath("segatools.ini");original=readFile(iniPath);
        // Latin-1 is a byte-preserving view: existing UTF-8/ANSI comments survive unchanged.
        if(original.startsWith("\xff\xfe")||original.startsWith("\xfe\xff")||original.contains('\0'))throw QStringLiteral("当前不支持 UTF-16 INI，已停止修改。请使用 UTF-8 或 ANSI 编码。");
        auto text=QString::fromLatin1(original);QString newline=text.contains("\r\n")?"\r\n":"\n";
        auto lines=text.split(newline);QString section;QHash<QString,int> sections,keys;
        for(int i=0;i<lines.size();++i){auto t=lines[i].trimmed();if(i==0&&t.startsWith(QString::fromLatin1("\xef\xbb\xbf")))t=t.mid(3);
            auto s=QRegularExpression("^\\[([^]]+)\\]\\s*(?:[;#].*)?$").match(t);
            if(s.hasMatch()){section=s.captured(1).toLower();if(section=="aimeio"||section=="aime"){if(sections.contains(section))throw QStringLiteral("存在重复的 [%1]，请先手动整理配置。").arg(section);sections[section]=i;}continue;}
            auto k=QRegularExpression("^([^=;#]+)=(.*)$").match(t);if(!k.hasMatch())continue;auto key=section+"/"+k.captured(1).trimmed().toLower();
            if(key=="aimeio/path"||key=="aimeio/cardspath"||key=="aime/enable"){if(keys.contains(key))throw QStringLiteral("配置项重复：%1").arg(key);keys[key]=i;}
        }
        QMap<QString,QString> values{{"aimeio/path",name},{"aimeio/cardspath","cards.json"},{"aime/enable","1"}};
        for(auto it=values.begin();it!=values.end();++it)if(keys.contains(it.key())) {
            auto &line=lines[keys[it.key()]];int equal=line.indexOf('=');QString suffix;auto tail=line.mid(equal+1);auto comment=QRegularExpression("\\s+[;#].*$").match(tail);if(comment.hasMatch())suffix=comment.captured();line=line.left(equal+1)+it.value()+suffix;
        }
        for(const auto &sec:{QString("aimeio"),QString("aime")}) {
            QStringList missing;for(auto it=values.begin();it!=values.end();++it)if(it.key().startsWith(sec+"/")&&!keys.contains(it.key()))missing<<it.key().section('/',1)+"="+it.value();
            if(missing.isEmpty())continue;
            if(!sections.contains(sec)){lines<<""<<"["+sec+"]";lines.append(missing);}
            else {int at=-1;for(int i=0;i<lines.size();++i)if(QRegularExpression("^\\["+sec+"\\]\\s*(?:[;#].*)?$",QRegularExpression::CaseInsensitiveOption).match(lines[i].trimmed()).hasMatch()){at=i+1;break;}if(at<0)throw QStringLiteral("段落格式无法安全修改。");for(const auto &v:missing)lines.insert(at++,v);}
        }
        modified=lines.join(newline).toLatin1();preview->setPlainText(QStringLiteral("将修改：\n[aimeio] path=%1\n[aimeio] cardsPath=cards.json\n[aime] enable=1\n\n修改后 INI：\n").arg(name)+QString::fromUtf8(modified));status->setStyleSheet("color:#166534;");status->setText(QStringLiteral("已读取配置。请选择正确位数，并关闭游戏后应用。"));return true;
    }catch(const QString &e){status->setStyleSheet("color:#b91c1c;");status->setText(e);preview->clear();return false;}
}
void SegatoolsEditor::apply() {
    if(!prepare())return;
    QString dir=QFileInfo(iniPath).absolutePath(),dll=QDir(dir).filePath(dllName->text().trimmed()+".dll"),json=QDir(dir).filePath("cards.json");
    if(runningIn(dir)){status->setText(QStringLiteral("检测到该目录中的游戏进程正在运行，请关闭游戏和 amdaemon 后再应用。"));return;}
    if((QFile::exists(dll)||QFile::exists(json))&&QMessageBox::question(this,QStringLiteral("备份后覆盖"),QStringLiteral("目标 DLL 或 cards.json 已存在。先备份，再用内置 DLL 和当前卡片数据覆盖？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    QList<QPair<QString,QString>> backups;QStringList created;
    try {
        auto bytes=readFile(":/aimeio/"+architecture->currentData().toString()+".dll");
        QString stamp=QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
        for(const auto &p:{iniPath,dll,json}) {if(QFile::exists(p)){auto backup=p+".skpass-"+stamp+".bak";if(!QFile::copy(p,backup))throw QStringLiteral("无法创建备份：%1").arg(backup);backups.append({p,backup});}else created<<p;}
        writeFile(dll,bytes);writeFile(iniPath,modified);
        if(!manager->exportToGame(dir))throw QStringLiteral("JSON 导出失败，正在恢复配置。");
        QSettings().setValue("segatoolsDirectory",dir);status->setStyleSheet("color:#166534;font-weight:600;");status->setText(QStringLiteral("安装成功：%1\nJSON 读取与保存已切换到：%2\nINI 备份：%3").arg(dll,json,backups.first().second));
    }catch(const QString &e){QString rollback;for(const auto &b:backups)try{writeFile(b.first,readFile(b.second));}catch(const QString &r){rollback+="\n恢复失败："+b.first+" · "+r;}for(const auto &p:created)if(QFile::exists(p)&&!QFile::remove(p))rollback+="\n无法移除新增文件："+p;status->setStyleSheet("color:#b91c1c;");status->setText(e+rollback);}
}
void SegatoolsEditor::restore() {
    QString dir=directory->text();auto backup=QFileDialog::getOpenFileName(this,QStringLiteral("选择 INI 备份（只恢复配置）"),dir,"INI backups (segatools.ini*.bak)");if(backup.isEmpty())return;
    if(runningIn(dir)){status->setText(QStringLiteral("请关闭游戏后恢复配置。"));return;}
    if(QMessageBox::question(this,QStringLiteral("恢复配置"),QStringLiteral("将先备份当前 INI，再恢复所选备份。DLL 和 JSON 不会随此操作恢复。"))!=QMessageBox::Yes)return;
    try{auto target=QDir(dir).filePath("segatools.ini");auto bytes=readFile(backup);if(!QFile::copy(target,target+".before-restore-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".bak"))throw QStringLiteral("当前 INI 备份失败，未恢复。");writeFile(target,bytes);prepare();status->setText(QStringLiteral("INI 已恢复，当前配置已留备份。"));}catch(const QString &e){status->setText(e);}
}
