#include "CardManager.h"
#include "AimeCode.h"
#include "CardDialogs.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QFileDialog>
#include <QCoreApplication>
#include <QSettings>
#include <QRegularExpression>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QRandomGenerator>
#include <QDateTime>
#include <QSet>
#include <QDir>
#include <QGroupBox>
#include <QTimer>

static QString normalizedCode(const QString &input) {
    QString result;
    for(auto ch:input){if(ch.isSpace() || ch=='-' || ch==QChar(0x2013) || ch==QChar(0x2014))continue;
        if(ch.unicode()>=0xFF10 && ch.unicode()<=0xFF19)ch=QChar('0'+ch.unicode()-0xFF10);
        result+=ch;
    }
    return result;
}

bool CardManager::valid(const QString &code,QString *error) {
    if(!QRegularExpression("^[0-9]{20}$").match(code).hasMatch() || code==QString(20,'0')) {
        if(error)*error=QStringLiteral("卡号必须是20位ASCII数字，且不能全为0。");return false;
    }
    if(code.startsWith("01036")) {
        QString detail;
        if(AimeCode::serialFromCode(code,&detail).isEmpty()) {
            if(error)*error=QStringLiteral("Classic AiMe卡号摘要校验失败：")+detail;return false;
        }
    }
    return true;
}
CardManager::CardManager(QWidget *parent):QWidget(parent) {
    auto layout=new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(12);
    auto exportGroup=new QGroupBox(QStringLiteral("JSON 导出位置"));
    auto exportLayout=new QVBoxLayout(exportGroup);exportLayout->setSpacing(8);
    auto exportHint=new QLabel(QStringLiteral("请选择游戏目录（aimeio.dll所在目录）"));exportHint->setProperty("role","muted");exportLayout->addWidget(exportHint);
    auto exportRow=new QHBoxLayout;pathLabel=new QLabel;pathLabel->setWordWrap(true);pathLabel->setTextFormat(Qt::PlainText);pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto chooseDirectory=new QPushButton(QStringLiteral("选择导出目录"));chooseDirectory->setProperty("role","primary");
    exportRow->addWidget(pathLabel,1);exportRow->addWidget(chooseDirectory);exportLayout->addLayout(exportRow);
    auto fileRow=new QHBoxLayout;auto defaultHint=new QLabel(QStringLiteral("默认：上位机 EXE 所在目录 / cards.json"));defaultHint->setProperty("role","muted");defaultHint->setWordWrap(true);
    auto choose=new QPushButton(QStringLiteral("读取已有 JSON"));fileRow->addWidget(defaultHint,1);fileRow->addWidget(choose);exportLayout->addLayout(fileRow);
    status=new QLabel;status->setObjectName("virtualCardStatus");status->setWordWrap(true);layout->addWidget(status);
    layout->addWidget(createHistoryView(this));
    auto inputGroup=new QGroupBox(QStringLiteral("添加卡号"));auto inputLayout=new QVBoxLayout(inputGroup);inputLayout->setSpacing(8);
    auto row=new QHBoxLayout;manual=new QLineEdit;manual->setObjectName("manualAccessCode");manual->setMaxLength(64);manual->setPlaceholderText(QStringLiteral("输入或粘贴20位卡号，可带空格或横线"));
    saveManual=new QPushButton(QStringLiteral("保存输入卡号"));saveManual->setObjectName("saveManualCode");auto random=new QPushButton(QStringLiteral("随机生成卡号"));row->addWidget(manual,1);row->addWidget(random);row->addWidget(saveManual);inputLayout->addLayout(row);
    validation=new QLabel(QStringLiteral("请输入20位卡号。"));validation->setObjectName("codeValidation");validation->setWordWrap(true);inputLayout->addWidget(validation);
    auto serialRow=new QHBoxLayout;auto serialInput=new QLineEdit;serialInput->setObjectName("virtualSerial");serialInput->setMaxLength(8);serialInput->setPlaceholderText(QStringLiteral("输入8位 Serial，自动生成校验位"));auto generate=new QPushButton(QStringLiteral("从 Serial 生成"));generate->setObjectName("generateVirtualCode");generate->setEnabled(false);
    serialRow->addWidget(serialInput,1);serialRow->addWidget(generate);inputLayout->addLayout(serialRow);
    auto inputHint=new QLabel(QStringLiteral("已有实体卡：直接粘贴完整20位卡号。创建虚拟卡：输入8位 Serial 或随机生成，再保存并启用。Serial 不是 UID；生成号码不代表官方注册。"));inputHint->setWordWrap(true);inputHint->setProperty("role","muted");inputLayout->addWidget(inputHint);layout->addWidget(inputGroup);
    connect(serialInput,&QLineEdit::textChanged,this,[generate](const QString &s){generate->setEnabled(QRegularExpression("^[0-9]{8}$").match(s).hasMatch());});
    connect(generate,&QPushButton::clicked,this,[this,serialInput]{manual->setText(AimeCode::fromSerial(serialInput->text()));});
    auto savedHeader=new QHBoxLayout;
    auto savedHint=new QLabel(QStringLiteral("已保存卡片（双击备注可编辑，点击保存备注写入JSON）"));savedHint->setWordWrap(true);
    deleteSaved=new QPushButton(QStringLiteral("删除选中卡片"));deleteSaved->setObjectName("deleteSavedCard");deleteSaved->setEnabled(false);
    deleteSaved->setToolTip(QStringLiteral("删除选中虚拟卡及备注，并立即同步当前 JSON 文件"));
    deleteSaved->setStyleSheet("QPushButton{color:#b91c1c;border:1px solid #fecaca;background:#fff7f7;}QPushButton:hover{background:#fee2e2;}QPushButton:disabled{color:#94a3b8;border:1px solid #e2e8f0;background:#f8fafc;}");
    savedHeader->addWidget(savedHint,1);savedHeader->addWidget(deleteSaved);layout->addLayout(savedHeader);
    savedTable=new QTableWidget(0,3);savedTable->setHorizontalHeaderLabels({QStringLiteral("状态"),QStringLiteral("Access code"),QStringLiteral("备注 / 持卡人")});
    savedTable->setObjectName("savedCards");
    savedTable->setStyleSheet("QTableWidget::item:selected{border:1px solid #2563eb;background:transparent;}");
    savedTable->setSelectionBehavior(QAbstractItemView::SelectRows);savedTable->setSelectionMode(QAbstractItemView::SingleSelection);savedTable->setAlternatingRowColors(true);
    savedTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);savedTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);savedTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);savedTable->setMinimumHeight(180);layout->addWidget(savedTable,1);
    savedTable->verticalHeader()->hide();savedTable->verticalHeader()->setDefaultSectionSize(40);savedTable->setShowGrid(false);savedTable->setWordWrap(false);savedTable->horizontalHeader()->setMinimumSectionSize(64);
    saveManual->setProperty("role","primary");
    manual->setFont(QFont("Consolas",12));
    row=new QHBoxLayout;auto enable=new QPushButton(QStringLiteral("启用选中虚拟卡"));auto disable=new QPushButton(QStringLiteral("停用虚拟卡"));auto saveNotes=new QPushButton(QStringLiteral("保存备注"));
    keyButton=new QPushButton;keyButton->setToolTip(QStringLiteral("点击后按新键，Esc取消"));
    row->addWidget(enable);row->addWidget(disable);row->addWidget(saveNotes);row->addStretch();row->addWidget(new QLabel(QStringLiteral("按住刷卡键")));row->addWidget(keyButton);layout->addLayout(row);
    enable->setProperty("role","primary");
    saveStatus=new QLabel;saveStatus->setObjectName("jsonSaveNotice");saveStatus->setWordWrap(true);saveStatus->hide();layout->addWidget(saveStatus);
    saveNoticeTimer=new QTimer(this);saveNoticeTimer->setSingleShot(true);saveNoticeTimer->setInterval(4000);
    connect(saveNoticeTimer,&QTimer::timeout,this,[this]{saveStatus->clear();saveStatus->hide();});
    layout->addWidget(exportGroup);
    const auto dataDir=QCoreApplication::instance()->property("dataDir").toString();
    path=QSettings().value("cardsJson",QDir(dataDir.isEmpty()?QCoreApplication::applicationDirPath():dataDir).filePath("cards.json")).toString();
    connect(chooseDirectory,&QPushButton::clicked,this,&CardManager::chooseExportDirectory);
    connect(saveManual,&QPushButton::clicked,this,[this]{saveNumber(normalizedCode(manual->text()));});
    connect(manual,&QLineEdit::textChanged,this,[this](const QString &s){validateInput(s);});
    connect(random,&QPushButton::clicked,this,[this]{
        QString serial,code;do{serial=QString::number(QRandomGenerator::global()->bounded(100000000u)).rightJustified(8,'0');code=AimeCode::fromSerial(serial);}while(code==manual->text()||history.contains(code)||cards.contains(code));
        manual->setText(code);
    });
    connect(enable,&QPushButton::clicked,this,[this]{int row=savedTable->currentRow();if(row<0)return;auto old=active;active=savedTable->item(row,1)->text();if(!persist())active=old;refresh();});
    connect(saveNotes,&QPushButton::clicked,this,[this]{persist();});
    connect(deleteSaved,&QPushButton::clicked,this,&CardManager::deleteSelectedCard);
    connect(savedTable,&QTableWidget::itemSelectionChanged,this,[this]{deleteSaved->setEnabled(loadValid && !savedTable->selectionModel()->selectedRows().isEmpty());});
    connect(savedTable,&QTableWidget::itemChanged,this,[this](QTableWidgetItem *item){if(item->column()==2){notes.insert(savedTable->item(item->row(),1)->text(),item->text());saveNoticeTimer->stop();saveStatus->show();saveStatus->setStyleSheet("color:#b15a00;");saveStatus->setText(QStringLiteral("备注已修改，点击保存备注写入JSON。"));}});
    connect(disable,&QPushButton::clicked,this,[this]{auto old=active;active.clear();if(!persist())active=old;refresh();});
    connect(keyButton,&QPushButton::clicked,this,[this]{CardKeyDialog dialog(this,keyVk);if(dialog.exec()!=QDialog::Accepted)return;auto old=keyVk;keyVk=dialog.chosen;if(!persist())keyVk=old;refresh();});
    connect(choose,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,QStringLiteral("读取已有卡片 JSON"),path,"JSON (*.json)");if(p.isEmpty())return;path=p;QSettings().setValue("cardsJson",path);load();});
    load();
    validateInput(manual->text());
}
void CardManager::refresh() {
    QSignalBlocker blocker(savedTable);QString selected;int row=savedTable->currentRow();if(row>=0)selected=savedTable->item(row,1)->text();savedTable->setRowCount(0);
    for(const auto &s:cards){int r=savedTable->rowCount();savedTable->insertRow(r);auto state=new QTableWidgetItem(s==active?QStringLiteral("启用"):QStringLiteral("—"));auto number=new QTableWidgetItem(s);state->setFlags(state->flags()&~Qt::ItemIsEditable);number->setFlags(number->flags()&~Qt::ItemIsEditable);savedTable->setItem(r,0,state);savedTable->setItem(r,1,number);savedTable->setItem(r,2,new QTableWidgetItem(notes.value(s)));if(s==selected)savedTable->selectRow(r);}
    for(int r=0;r<savedTable->rowCount();r++)if(savedTable->item(r,1)->text()==active){
        savedTable->item(r,0)->setText(QStringLiteral("✓ 已启用"));
        for(int c=0;c<3;c++){auto item=savedTable->item(r,c);item->setBackground(QColor("#dcfce7"));item->setForeground(QColor("#166534"));auto font=item->font();font.setBold(true);item->setFont(font);}
    }
    pathLabel->setText(QStringLiteral("共享JSON：")+path);
    keyButton->setText(cardKeyName(keyVk)+QStringLiteral(" · 修改"));
    status->setStyleSheet(active.isEmpty()?"color:#b91c1c;background:#fef2f2;border:1px solid #fecaca;border-radius:8px;padding:12px;font-weight:600;":"color:#166534;background:#dcfce7;border:1px solid #86efac;border-radius:8px;padding:12px;font-weight:600;");
    status->setText(active.isEmpty()?QStringLiteral("未启用任何虚拟卡：AimeIO 无法通过按键刷入虚拟卡。请保存卡号并点击“启用选中虚拟卡”。"):
        QStringLiteral("✓ 已启用：%1\n游戏内按住 %2 刷卡，松开移卡；实体卡优先。").arg(active,cardKeyName(keyVk)));
    deleteSaved->setEnabled(loadValid && !savedTable->selectionModel()->selectedRows().isEmpty());
}
void CardManager::load() {
    cards.clear();notes.clear();active.clear();saveNoticeTimer->stop();saveStatus->clear();saveStatus->hide();loadValid=true;keyVk=13;
    QFile file(path);if(!file.exists()){refresh();return;}
    if(!file.open(QIODevice::ReadOnly)){loadValid=false;refresh();status->setText(file.errorString());return;}
    QJsonParseError err;auto doc=QJsonDocument::fromJson(file.readAll(),&err);
    if(err.error!=QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt()!=1) {loadValid=false;refresh();status->setText(QStringLiteral("JSON格式/版本无效，请选择有效文件；原文件未改动。"));return;}
    auto root=doc.object();for(auto v:root.value("cards").toArray()){auto s=v.toObject().value("accessCode").toString();if(valid(s,nullptr) && !cards.contains(s)){cards<<s;notes.insert(s,v.toObject().value("note").toString());}}
    auto selected=root.value("activeCode").toString();if(cards.contains(selected))active=selected;
    unsigned loadedKey=root.value("keyVk").toInt(13);if(supportedCardKey(loadedKey))keyVk=loadedKey;
    refresh();
}
bool CardManager::persist() {
    if(!loadValid){QMessageBox::warning(this,QStringLiteral("不能覆盖无效JSON"),QStringLiteral("请先选择有效JSON或新的文件路径。"));return false;}
    QJsonArray array;for(const auto &s:cards)array.append(QJsonObject{{"type","classic"},{"accessCode",s},{"note",notes.value(s)}});
    QJsonObject root{{"version",1},{"activeCode",active},{"keyVk",int(keyVk)},{"cards",array}};
    QSaveFile file(path);auto bytes=QJsonDocument(root).toJson();
    if(bytes.size()>65536 || cards.size()>200) {
        QMessageBox::warning(this,QStringLiteral("JSON超出限制"),QStringLiteral("最多保存200张卡，文件最多64KiB。"));return false;
    }
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        QMessageBox::warning(this,QStringLiteral("JSON保存失败"),file.errorString());return false;
    }
    refresh();saveStatus->setStyleSheet("color:#16803c;font-weight:600;");saveStatus->setText(QStringLiteral("保存成功：JSON共 %1 张卡。").arg(cards.size()));saveStatus->show();saveNoticeTimer->start();return true;
}
void CardManager::saveNumber(const QString &s) {
    saveNumbers({s});
}
void CardManager::saveNumbers(const QStringList &numbers) {
    for(const auto &s:numbers){QString error;if(!valid(s,&error)){QMessageBox::warning(this,QStringLiteral("卡号检查失败"),s+"\n"+error);return;}}
    auto old=cards;for(const auto &s:numbers)if(!cards.contains(s))cards<<s;if(!persist()){cards=old;refresh();saveNoticeTimer->stop();saveStatus->show();saveStatus->setStyleSheet("color:#c62828;");saveStatus->setText(QStringLiteral("保存失败，请检查文件路径和权限。"));}
}
void CardManager::deleteSelectedCard() {
    const auto selection=savedTable->selectionModel()->selectedRows();
    if(!loadValid || selection.isEmpty())return;
    const int row=selection.first().row();
    const auto code=savedTable->item(row,1)->text();
    CardDeleteDialog dialog(this,code,notes.value(code),path,active==code);
    if(dialog.exec()!=QDialog::Accepted)return;

    const auto previousCards=cards;
    const auto previousNotes=notes;
    const auto previousActive=active;
    cards.removeAll(code);notes.remove(code);
    if(active==code)active.clear();
    if(!persist()) {
        cards=previousCards;notes=previousNotes;active=previousActive;refresh();
        saveNoticeTimer->stop();saveStatus->setStyleSheet("color:#c62828;");
        saveStatus->setText(QStringLiteral("删除失败：JSON 未更新，卡片和启用状态已保留。请检查文件路径和权限。"));saveStatus->show();return;
    }
    if(savedTable->rowCount()>0)savedTable->selectRow(qMin(row,savedTable->rowCount()-1));
    saveStatus->setText(QStringLiteral("删除成功，已同步 JSON；剩余 %1 张卡。%2").arg(cards.size()).arg(
        previousActive==code?QStringLiteral("已停用虚拟卡。"):QString()));
}
void CardManager::validateInput(const QString &input) {
    const auto s=normalizedCode(input);
    QString error;
    saveManual->setEnabled(valid(s,nullptr));
    if(s.isEmpty()) {validation->setText(QStringLiteral("请输入20位卡号。"));validation->setStyleSheet("color:#64748b;");return;}
    if(valid(s,&error)) {validation->setText(QStringLiteral("格式正确：20位数字%1").arg(s.startsWith("01036")?QStringLiteral("，Classic卡号校验通过"):QString()));validation->setStyleSheet("color:#16803c;font-weight:600;");return;}
    int bad=-1;for(int i=0;i<s.size();++i)if(s[i]<'0'||s[i]>'9'){bad=i;break;}
    QString reason;
    if(bad>=0)reason=QStringLiteral("第%1位不是ASCII数字。请使用0–9。").arg(bad+1);
    else if(s.size()<20)reason=QStringLiteral("当前%1位，还差%2位数字。").arg(s.size()).arg(20-s.size());
    else if(s.size()>20)reason=QStringLiteral("共%1位，超过20位。请检查多余数字。").arg(s.size());
    else if(s==QString(20,'0'))reason=QStringLiteral("不能使用全零卡号。");
    else if(s.startsWith("01036"))reason=QStringLiteral("此 Classic 卡号的校验位不匹配。已有卡请核对完整20位号码；创建虚拟卡请使用下方8位 Serial 生成或“随机生成卡号”，无需手填校验位。");
    else reason=error;
    validation->setText(reason);validation->setStyleSheet("color:#c62828;font-weight:600;");
}
void CardManager::record(const QString &s) {
    if(s.size()!=20 || s==QString(20,'0'))return;
    history.removeAll(s);history.prepend(s);historyTimes.insert(s,QDateTime::currentDateTime().toString("HH:mm:ss"));while(history.size()>10)historyTimes.remove(history.takeLast());
    checkedHistory.intersect(QSet<QString>(history.begin(),history.end()));
    refreshHistory();
}

QWidget *CardManager::createHistoryView(QWidget *parent) {
    auto view=new QWidget(parent);view->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto layout=new QVBoxLayout(view);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);layout->setAlignment(Qt::AlignTop);
    auto titleRow=new QHBoxLayout;auto title=new QLabel(QStringLiteral("最近刷入卡号"));title->setStyleSheet("font-size:16px;font-weight:600;");
    auto save=new QPushButton(QStringLiteral("保存选中记录到 JSON"));auto clear=new QPushButton(QStringLiteral("清空记录"));
    titleRow->addWidget(title);titleRow->addStretch();titleRow->addWidget(save);titleRow->addWidget(clear);layout->addLayout(titleRow);
    auto hint=new QLabel(QStringLiteral("保留最近 10 个不重复卡号 · 勾选可多选 · 与虚拟卡管理同步"));hint->setProperty("role","muted");hint->setWordWrap(true);layout->addWidget(hint);
    auto table=new QTableWidget(0,3,view);table->setHorizontalHeaderLabels({QStringLiteral("选择"),QStringLiteral("时间"),QStringLiteral("Access code")});
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(38);table->setShowGrid(false);table->setWordWrap(false);table->setMinimumHeight(175);table->setMaximumHeight(235);
    layout->addWidget(table,1);historyTables.append(table);
    connect(table,&QTableWidget::itemChanged,this,[this,table](QTableWidgetItem *item){if(item->column()!=0)return;auto code=table->item(item->row(),2)->text();if(item->checkState()==Qt::Checked)checkedHistory.insert(code);else checkedHistory.remove(code);refreshHistory();});
    connect(clear,&QPushButton::clicked,this,[this]{history.clear();historyTimes.clear();checkedHistory.clear();refreshHistory();});
    connect(save,&QPushButton::clicked,this,[this]{QStringList selected;for(const auto &code:history)if(checkedHistory.contains(code))selected<<code;if(selected.isEmpty()){QMessageBox::information(this,QStringLiteral("选择刷卡记录"),QStringLiteral("请先勾选要保存的卡号。"));return;}saveNumbers(selected);});
    refreshHistory();return view;
}
void CardManager::refreshHistory() {
    for(auto table:historyTables){QSignalBlocker block(table);table->setRowCount(0);
        for(const auto &code:history){int row=table->rowCount();table->insertRow(row);auto check=new QTableWidgetItem;
            check->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable);check->setCheckState(checkedHistory.contains(code)?Qt::Checked:Qt::Unchecked);table->setItem(row,0,check);
            table->setItem(row,1,new QTableWidgetItem(historyTimes.value(code)));auto number=new QTableWidgetItem(code);number->setFont(QFont("Consolas",11));table->setItem(row,2,number);
            if(checkedHistory.contains(code))for(int c=0;c<3;c++)table->item(row,c)->setBackground(QColor("#eaf2ff"));
        }
    }
}
void CardManager::chooseExportDirectory() {
    auto dir=QFileDialog::getExistingDirectory(this,QStringLiteral("请选择游戏目录（aimeio.dll所在目录）"),QFileInfo(path).absolutePath());if(dir.isEmpty())return;
    auto destination=QDir(dir).filePath("cards.json");
    if(QFileInfo::exists(destination) && destination!=path && QMessageBox::question(this,QStringLiteral("导出卡片 JSON"),QStringLiteral("目标目录已有 cards.json。是否用当前保存的卡片覆盖该文件？\n如需读取原卡片，请使用“读取已有 JSON”。"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    auto previous=path;auto previousValid=loadValid;path=destination;loadValid=true;
    if(!persist()){path=previous;loadValid=previousValid;refresh();return;}
    QSettings().setValue("cardsJson",path);
}
bool CardManager::exportToGame(const QString &directory) {
    if(!loadValid)return false;
    auto previous=path;path=QDir(directory).filePath("cards.json");
    if(!persist()){path=previous;refresh();return false;}
    QSettings().setValue("cardsJson",path);
    load();return true;
}
