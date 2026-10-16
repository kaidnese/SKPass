#include "AdvancedPanel.h"
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QSaveFile>
#include <QFontDatabase>
#include <QDateTime>
#include <cstring>
namespace {
bool sameCard(const advanced_target_t &a,const advanced_target_t &b){
    return a.uid_len && a.uid_len==b.uid_len && !memcmp(a.uid,b.uid,a.uid_len) &&
        a.protocol==b.protocol && a.subtype==b.subtype && a.first==b.first && a.last==b.last && a.unit==b.unit;
}
QString uidText(const advanced_target_t &t){return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(t.uid),t.uid_len).toHex(' ').toUpper());}
QString scanError(int status){
    switch(status){case ADV_AUTH_FAILED:return QStringLiteral("认证失败");case ADV_RF_FAILED:return QStringLiteral("射频读取失败");case STATUS_CARD_MISMATCH:return QStringLiteral("卡片不匹配 / 离场");case ADV_DISCONNECTED:return QStringLiteral("连接断开");case ADV_BUSY:return QStringLiteral("设备忙");case ADV_INVALID:return QStringLiteral("参数无效");default:return ReaderProtocol::statusText(status);}
}
}
AdvancedPanel::AdvancedPanel(QWidget *parent,bool standalone):QWidget(parent){
    setObjectName("advancedPanel");auto outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    group=new QGroupBox(standalone?QStringLiteral("当前卡片与读写操作"):QStringLiteral("高级读写选项（Advanced R/W）"));group->setCheckable(!standalone);group->setChecked(false);group->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);outer->addWidget(group);outer->addStretch();
    group->setObjectName("advancedOperationGroup");auto layout=new QVBoxLayout(group);layout->setAlignment(Qt::AlignTop);layout->setSpacing(12);identity=new QLabel;identity->setWordWrap(true);layout->addWidget(identity);
    body=new QWidget;auto form=new QVBoxLayout(body);form->setAlignment(Qt::AlignTop);form->setSpacing(12);form->setContentsMargins(0,8,0,0);layout->addWidget(body);body->hide();
    auto row=new QFormLayout;row->setAlignment(Qt::AlignTop);address=new QSpinBox;address->setObjectName("advancedAddress");count=new QSpinBox;count->setObjectName("advancedCount");
    row->addRow(QStringLiteral("块号 / 页号"),address);row->addRow(QStringLiteral("读取页数（单块类型固定为 1）"),count);form->addLayout(row);
    classicOptions=new QWidget;classicOptions->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto cf=new QFormLayout(classicOptions);cf->setContentsMargins(0,0,0,0);
    keyType=new QComboBox;keyType->addItems({"Key A · 60","Key B · 61"});keyType->setObjectName("advancedKeyType");
    keyPreset=new QComboBox;keyPreset->addItems({QStringLiteral("空白卡 · FFFFFFFFFFFF"),QStringLiteral("Aime / WCCFv2 · 574343467632"),QStringLiteral("Bana · 6090D00632F5"),QStringLiteral("自定义密钥")});
    key=new QLineEdit("FFFFFFFFFFFF");key->setMaxLength(12);key->setObjectName("advancedKey");
    cf->addRow(QStringLiteral("认证方式"),keyType);cf->addRow(QStringLiteral("密钥预设（不是通用密钥）"),keyPreset);cf->addRow(QStringLiteral("6 字节密钥（HEX）"),key);
    trailer=new QCheckBox(QStringLiteral("解锁尾块写入：我确认新密钥与访问位，了解可能锁死扇区"));trailer->setObjectName("advancedTrailerUnlock");cf->addRow(trailer);form->addWidget(classicOptions);
    data=new QPlainTextEdit;data->setObjectName("advancedData");data->setPlaceholderText(QStringLiteral("写入数据：每字节两位十六进制，可用空格换行分隔"));data->setFixedHeight(84);form->addWidget(data);
    auto buttons=new QHBoxLayout;read=new QPushButton(QStringLiteral("读取"));write=new QPushButton(QStringLiteral("写入并验证"));read->setObjectName("advancedRead");write->setObjectName("advancedWrite");buttons->addWidget(read);buttons->addWidget(write);buttons->addStretch();form->addLayout(buttons);
    uidOptions=new QWidget;auto uf=new QFormLayout(uidOptions);uf->setContentsMargins(0,12,0,0);
    auto note=new QLabel(QStringLiteral("UID / CUID 无法由 SAK 自动区分。仅自行确认的 Gen2 CUID 可尝试改写；保留原厂商字节并自动计算 BCC，不发送 Gen1 后门命令。"));note->setWordWrap(true);uf->addRow(note);
    gen2=new QCheckBox(QStringLiteral("我确认这是可直写 Block 0 的 Gen2 CUID 卡"));gen2->setObjectName("advancedGen2");uf->addRow(gen2);
    newUid=new QLineEdit;newUid->setMaxLength(8);newUid->setPlaceholderText("例如 11223344");uf->addRow(QStringLiteral("新 UID（4 字节 HEX）"),newUid);
    writeUid=new QPushButton(QStringLiteral("改写 UID / Block 0"));uf->addRow(writeUid);form->addWidget(uidOptions);
    ndefOptions=new QWidget;auto nf=new QFormLayout(ndefOptions);nf->setContentsMargins(0,12,0,0);
    ndefType=new QComboBox;ndefType->addItems({QStringLiteral("网页链接（http / https）"),QStringLiteral("纯文本（UTF-8）")});ndefContent=new QLineEdit;ndefContent->setPlaceholderText("https://example.com");
    nf->addRow(QStringLiteral("标准 NDEF"),ndefType);nf->addRow(QStringLiteral("内容"),ndefContent);writeNdef=new QPushButton(QStringLiteral("写入 NDEF 并验证"));nf->addRow(writeNdef);form->addWidget(ndefOptions);
    message=new QLabel;message->setObjectName("advancedStatus");message->setWordWrap(true);layout->addWidget(message);
    // Results have their own lifetime, independent of RF leases and form visibility.
    auto saved=new QGroupBox(QStringLiteral("读取结果 · 离场保留"));auto savedLayout=new QVBoxLayout(saved);savedLayout->setSpacing(10);outer->insertWidget(0,saved);
    snapshotIdentity=new QLabel(QStringLiteral("尚未读取数据。结果会保留至手动清空、开始新扫描或关闭程序。"));snapshotIdentity->setObjectName("advancedSnapshotIdentity");snapshotIdentity->setWordWrap(true);savedLayout->addWidget(snapshotIdentity);
    table=new QTableWidget(0,5);table->setObjectName("advancedResultTable");table->setHorizontalHeaderLabels({QStringLiteral("地址"),QStringLiteral("扇区 / 类型"),"HEX", "ASCII",QStringLiteral("读取状态")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setAlternatingRowColors(true);table->verticalHeader()->hide();table->setMinimumHeight(260);table->setMaximumHeight(460);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->setColumnWidth(0,58);table->setColumnWidth(1,125);table->setColumnWidth(2,440);table->setColumnWidth(3,145);table->setColumnWidth(4,180);savedLayout->addWidget(table);
    auto resultButtons=new QHBoxLayout;copyResult=new QPushButton(QStringLiteral("复制全部"));exportResult=new QPushButton(QStringLiteral("导出 CSV"));clearResult=new QPushButton(QStringLiteral("清空结果"));clearResult->setObjectName("advancedClearResults");resultButtons->addWidget(copyResult);resultButtons->addWidget(exportResult);resultButtons->addStretch();resultButtons->addWidget(clearResult);savedLayout->addLayout(resultButtons);
    auto scanButtons=new QHBoxLayout;scan=new QPushButton(QStringLiteral("扫描全部安全区域"));scan->setObjectName("advancedScan");stopScan=new QPushButton(QStringLiteral("停止扫描"));stopScan->setObjectName("advancedStopScan");retryScan=new QPushButton(QStringLiteral("重试失败 / 未读项"));retryScan->setObjectName("advancedRetryScan");scanButtons->addWidget(scan);scanButtons->addWidget(stopScan);scanButtons->addWidget(retryScan);savedLayout->insertLayout(1,scanButtons);
    auto scanNote=new QLabel(QStringLiteral("Classic 使用下方所选密钥按扇区认证，尾块密钥可能被隐藏。扫描只读；其他卡只扫描已确认的安全用户区。"));scanNote->setWordWrap(true);scanNote->setProperty("role","muted");savedLayout->insertWidget(2,scanNote);
    progress=new QProgressBar;progress->setObjectName("advancedScanProgress");progress->hide();savedLayout->addWidget(progress);scanStatus=new QLabel;scanStatus->setObjectName("advancedScanStatus");scanStatus->setWordWrap(true);savedLayout->addWidget(scanStatus);
    // Keep a text representation for integrations and copying; the table is the visible view.
    result=new QPlainTextEdit;result->setReadOnly(true);result->setObjectName("advancedResult");result->hide();savedLayout->addWidget(result);
    connect(scan,&QPushButton::clicked,this,[this]{startScan(false);});connect(retryScan,&QPushButton::clicked,this,[this]{startScan(true);});
    connect(stopScan,&QPushButton::clicked,this,[this]{haltScan(QStringLiteral("已停止；已读数据保留，可重新贴同一张卡后重试。正在执行的一组读取会返回后保留。"));});
    connect(clearResult,&QPushButton::clicked,this,[this]{cells.clear();snapshotTarget={};progress->hide();scanStatus->clear();refreshResults();render();});
    connect(copyResult,&QPushButton::clicked,this,[this]{QApplication::clipboard()->setText(resultText());});
    connect(exportResult,&QPushButton::clicked,this,[this]{
        auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出卡片读取结果"),"SKPass-"+uidText(snapshotTarget).remove(' ')+".csv","CSV (*.csv)");if(path.isEmpty())return;
        QSaveFile file(path);const auto bytes=QByteArray::fromHex("EFBBBF")+resultText(true).toUtf8();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())scanStatus->setText(QStringLiteral("导出失败：")+file.errorString());else scanStatus->setText(QStringLiteral("已导出：")+path);
    });
    connect(group,&QGroupBox::toggled,this,[this]{render();});
    connect(address,&QSpinBox::valueChanged,this,[this]{if(target.protocol==2 && target.last>=address->value())count->setMaximum(qMin(64,int(target.last)-address->value()+1));});
    connect(keyPreset,&QComboBox::currentIndexChanged,this,[this](int n){const QStringList keys={"FFFFFFFFFFFF","574343467632","6090D00632F5"};if(n<3)key->setText(keys[n]);});
    connect(read,&QPushButton::clicked,this,[this]{issue(ADV_READ);});connect(write,&QPushButton::clicked,this,[this]{issue(ADV_WRITE);});
    connect(writeUid,&QPushButton::clicked,this,[this]{issue(ADV_UID_WRITE);});connect(writeNdef,&QPushButton::clicked,this,[this]{issue(ADV_NDEF);});
    connect(gen2,&QCheckBox::toggled,this,[this]{render();});
    lease.setSingleShot(true);connect(&lease,&QTimer::timeout,this,[this]{target.available=0;render();});
    scanWait.setSingleShot(true);connect(&scanWait,&QTimer::timeout,this,[this]{haltScan(QStringLiteral("等待同一张卡重新识别超时；数据保留，请贴卡后重试未读项。"));});
    timeout.setSingleShot(true);connect(&timeout,&QTimer::timeout,this,[this]{
        if(pendingOperation==ADV_SCAN && pendingId){for(int x=0;x<pendingCount;x++)if(cells[pendingAddress+x].bytes.isEmpty())cells[pendingAddress+x].status=QStringLiteral("响应超时");refreshResults();haltScan(QStringLiteral("响应超时；扫描已停止，已读数据保留。"));}
        finish(QStringLiteral("设备未在时限内返回最终结果；不会自动重发，已有结果保留，请重新读卡核对"));});render();
}
void AdvancedPanel::connected(bool on,bool support){online=on;scanSupported=support;if(!on){haltScan(QStringLiteral("连接已断开；读取结果保留。"));target={};activeUid.clear();lease.stop();if(pendingId)finish(QStringLiteral("连接已断开，操作结果未知；不会自动重发"));}render();}
void AdvancedPanel::activated(const QByteArray &uid){
    if(uid==activeUid)return;
    activeUid=uid;if(uid.isEmpty() || QByteArray(reinterpret_cast<const char*>(target.uid),target.uid_len)!=uid){target.available=0;lease.stop();}
    if(scanning && (uid.isEmpty() || uid!=QByteArray(reinterpret_cast<const char*>(scanTarget.uid),scanTarget.uid_len)))haltScan(QStringLiteral("卡片已离场或更换；扫描停止，已有结果保留，请放回原卡后重试。"));
    trailer->setChecked(false);gen2->setChecked(false);render();
}
void AdvancedPanel::targetFrame(const QByteArray &p){
    advanced_target_t t;if(!ReaderProtocol::target(p,t)){emit logMessage(QStringLiteral("高级目标信息无效"));return;}
    bool changed=!sameCard(t,target);
    target=t;
    if(changed){operationMessage.clear();trailer->setChecked(false);gen2->setChecked(false);data->clear();address->setRange(t.first,t.last);address->setValue(t.first);count->setValue(1);}
    if(scanning && t.available && !sameCard(t,scanTarget))haltScan(QStringLiteral("识别到另一张卡或不同容量；扫描停止，原卡结果保留。"));
    if(t.available)lease.start(2200);else lease.stop();render();
    if(scanning && t.available)QTimer::singleShot(0,this,[this]{nextScan();});
}
void AdvancedPanel::render(){
    bool classic=target.protocol==0x80 && target.subtype>=1 && target.subtype<=3;
    bool t2=target.protocol==2 && target.subtype>=4 && target.subtype<=7;
    bool ready=online && target.available && target.subtype && !pendingId;
    bool usable=ready && !scanning;
    identity->setText(target.available?QStringLiteral("%1 · UID %2 · %3–%4 · 每单元 %5 字节").arg(ReaderProtocol::subtypeName(target.subtype),QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(target.uid),target.uid_len).toHex(' ').toUpper())).arg(target.first).arg(target.last).arg(target.unit):QStringLiteral("未放卡 / 在场身份已过期，请贴卡重新识别"));
    if(!pendingId && operationMessage.isEmpty())message->setText(!target.subtype?QStringLiteral("当前卡片为只读或受金融安全保护；未确认型号 / 容量时也不开放写入"):target.available?QStringLiteral("操作与当前 UID、协议、子类型绑定；写入需要确认并回读核对。密钥和保护状态以卡片实际设置为准。"):QStringLiteral("在场身份已过期，请贴卡重新识别后操作。"));
    body->setVisible((!group->isCheckable() || group->isChecked()) && target.subtype);
    classicOptions->setVisible(classic);uidOptions->setVisible(classic && target.uid_len==4);ndefOptions->setVisible(t2);
    address->setEnabled(usable);count->setRange(1,t2?qMin(64,qMax(1,int(target.last)-address->value()+1)):1);count->setEnabled(usable && t2);
    classicOptions->setEnabled(usable);data->setEnabled(usable);ndefOptions->setEnabled(usable);uidOptions->setEnabled(usable);
    read->setEnabled(usable);write->setEnabled(usable);writeNdef->setEnabled(usable && t2);writeUid->setEnabled(usable && gen2->isChecked());
    scan->setEnabled(usable && scanSupported);scan->setToolTip(scanSupported?QString():QStringLiteral("请更新到支持扫描的 SKPass 固件"));stopScan->setEnabled(scanning);
    bool failed=false;for(const auto &cell:cells)if(cell.bytes.isEmpty()){failed=true;break;}
    retryScan->setEnabled(usable && scanSupported && failed && sameCard(target,snapshotTarget));
    clearResult->setEnabled(!pendingId && !scanning && !cells.isEmpty());copyResult->setEnabled(!cells.isEmpty());exportResult->setEnabled(!cells.isEmpty());
}
QString AdvancedPanel::resultText(bool csv) const {
    QString out=csv?QStringLiteral("uid,card_type,address,sector,hex,ascii,status\n"):QString();
    auto esc=[](QString s){s.replace('"',"\"\"");return QString('"')+s+'"';};
    for(auto it=cells.cbegin();it!=cells.cend();++it){
        QString hex=QString::fromLatin1(it->bytes.toHex(' ').toUpper()),ascii;
        for(unsigned char b:it->bytes)ascii+=QChar(b>=32&&b<127?b:'.');
        int a=it.key(),sector=snapshotTarget.protocol==0x80?(a<128?a/4:32+(a-128)/16):-1;
        if(csv)out+=esc(uidText(snapshotTarget))+','+esc(ReaderProtocol::subtypeName(snapshotTarget.subtype))+','+QString::number(a)+','+(sector<0?QString():QString::number(sector))+','+esc(hex)+','+esc(ascii)+','+esc(it->status)+'\n';
        else out+=QStringLiteral("%1: %2  %3  [%4]\n").arg(a,3,10,QChar('0')).arg(hex,ascii,it->status);
    }
    return out;
}
void AdvancedPanel::refreshResults(){
    snapshotIdentity->setText(snapshotTarget.subtype?QStringLiteral("来源：%1 · UID %2\n这是已读取的数据快照；离场、重识别和断开连接不会清空。")
        .arg(ReaderProtocol::subtypeName(snapshotTarget.subtype),uidText(snapshotTarget)):QStringLiteral("尚未读取数据。结果保留至手动清空、开始新扫描或关闭程序。"));
    int good=0,row=0;table->setRowCount(cells.size());
    const auto font=QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for(auto it=cells.cbegin();it!=cells.cend();++it,++row){
        int a=it.key();QString ascii;for(unsigned char b:it->bytes)ascii+=QChar(b>=32&&b<127?b:'.');
        bool ok=!it->bytes.isEmpty();if(ok)good++;
        QString area=snapshotTarget.protocol==0x80?QStringLiteral("扇区 %1%2").arg(a<128?a/4:32+(a-128)/16).arg((a<128?a%4==3:a%16==15)?QStringLiteral(" · 尾块"):QString()):snapshotTarget.protocol==2?QStringLiteral("Page"):QStringLiteral("Block");
        const QStringList values={QString::number(a),area,QString::fromLatin1(it->bytes.toHex(' ').toUpper()),ascii,it->status};
        for(int col=0;col<5;col++){auto item=new QTableWidgetItem(values[col]);item->setToolTip(values[col]);if(col==2 || col==3)item->setFont(font);if(col==4)item->setForeground(QColor(ok?"#137044":"#a15c00"));table->setItem(row,col,item);}
    }
    result->setPlainText(resultText());progress->setRange(0,cells.size());progress->setValue(good);progress->setFormat(QStringLiteral("成功 %v / %m 个单元"));
}
void AdvancedPanel::prepareSnapshot(const advanced_target_t &t){if(!sameCard(snapshotTarget,t))cells.clear();snapshotTarget=t;}
void AdvancedPanel::haltScan(const QString &why){scanning=false;scanQueue.clear();scanWait.stop();scanStatus->setText(why);render();}
void AdvancedPanel::startScan(bool retry){
    if(!online || !scanSupported || !target.available || !target.subtype || pendingId || scanning)return;
    if(target.protocol==0x80 && !QRegularExpression("^[0-9a-fA-F]{12}$").match(key->text()).hasMatch()){scanStatus->setText(QStringLiteral("密钥须为 12 位十六进制"));return;}
    if(retry && !sameCard(target,snapshotTarget))return;
    scanTarget=target;scanKey=target.protocol==0x80?QByteArray::fromHex(key->text().toLatin1()):QByteArray(6,0);scanKeyType=keyType->currentIndex()?0x61:0x60;scanQueue.clear();
    if(retry){for(auto it=cells.cbegin();it!=cells.cend();++it)if(it->bytes.isEmpty())scanQueue.append({it.key(),1});}
    else {
        cells.clear();prepareSnapshot(target);int last=target.subtype==ADV_CLASSIC_4K?255:target.last;
        for(int a=target.first;a<=last;a++)cells[a]={{},QStringLiteral("未读取")};
        for(int a=target.first;a<=last;){int width=target.protocol==0x80?(a<128?4:16):target.protocol==2?16:4;int n=qMin(width,last-a+1);scanQueue.append({a,n});a+=n;}
    }
    if(scanQueue.isEmpty())return;
    scanning=true;progress->show();refreshResults();render();nextScan();
}
void AdvancedPanel::nextScan(){
    if(!scanning || pendingId)return;
    if(scanQueue.isEmpty()){haltScan(QStringLiteral("扫描结束：成功 %1 / %2。未读或失败项可放回同一张卡后重试。").arg(progress->value()).arg(cells.size()));return;}
    if(!online || !target.available){if(!scanWait.isActive())scanWait.start(12000);scanStatus->setText(QStringLiteral("等待原卡重新识别；已读数据保留。"));return;}
    if(!sameCard(target,scanTarget)){haltScan(QStringLiteral("卡片不匹配，扫描已停止；原卡结果保留。"));return;}
    scanWait.stop();auto chunk=scanQueue.takeFirst();QString error;quint32 id;do{id=QRandomGenerator::global()->generate();}while(!id);
    auto p=ReaderProtocol::request(target,id,ADV_SCAN,chunk.first,chunk.second,0,scanKeyType,scanKey,{},&error);
    if(p.isEmpty()){haltScan(error);return;}
    pendingId=id;pendingTarget=target;pendingOperation=ADV_SCAN;pendingAddress=chunk.first;pendingCount=chunk.second;pendingData.clear();
    scanStatus->setText(QStringLiteral("正在读取 %1–%2；剩余 %3 组").arg(chunk.first).arg(chunk.first+chunk.second-1).arg(scanQueue.size()));timeout.start(20000);render();emit request(p);
}
void AdvancedPanel::issue(int op){
    if(!online || !target.available || !target.subtype || pendingId || scanning){message->setText(QStringLiteral("当前没有可操作的在场卡片，或扫描正在进行"));return;}
    QString error;QByteArray bytes,k(6,0);int a=address->value(),n=op==ADV_READ?count->value():1,flags=0;
    if(target.protocol==0x80){if(!QRegularExpression("^[0-9a-fA-F]{12}$").match(key->text()).hasMatch()){message->setText(QStringLiteral("密钥须为 12 位十六进制"));return;}k=QByteArray::fromHex(key->text().toLatin1());}
    if(op==ADV_WRITE){QString text=data->toPlainText();text.remove(QRegularExpression("\\s"));if(!QRegularExpression("^(?:[0-9a-fA-F]{2})+$").match(text).hasMatch()){message->setText(QStringLiteral("请输入完整字节的十六进制数据"));return;}bytes=QByteArray::fromHex(text.toLatin1());if(trailer->isChecked())flags|=ADV_CONFIRM_TRAILER;}
    if(op==ADV_UID_WRITE){if(!gen2->isChecked() || !QRegularExpression("^[0-9a-fA-F]{8}$").match(newUid->text()).hasMatch()){message->setText(QStringLiteral("先确认 Gen2 卡并输入 8 位新 UID"));return;}bytes=QByteArray::fromHex(newUid->text().toLatin1());a=0;flags=ADV_CONFIRM_GEN2|ADV_CONFIRM_UID;}
    if(op==ADV_NDEF){bytes=ReaderProtocol::ndef(ndefContent->text(),ndefType->currentIndex()==0,(target.last-3)*4,&error);a=4;n=bytes.size()/4;if(bytes.isEmpty()){message->setText(error);return;}}
    quint32 id;do{id=QRandomGenerator::global()->generate();}while(!id);
    auto p=ReaderProtocol::request(target,id,op,a,n,flags,keyType->currentIndex()?0x61:0x60,k,bytes,&error);
    if(p.isEmpty()){message->setText(error);return;}
    if(op!=ADV_READ){
        QString warning=QStringLiteral("将写入当前卡 UID %1 的地址 %2。卡片受保护时会失败；失败可能留下部分数据，请自行备份。确认写入？").arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(target.uid),target.uid_len).toHex().toUpper())).arg(a);
        if(target.protocol==0x80 && a%4==3)warning+=QStringLiteral("\n这是扇区尾块，将改变密钥和访问位，可能永久锁死扇区。回读会隐藏 Key A，仅验证新 Key A 认证和访问位。");
        if(op==ADV_UID_WRITE)warning+=QStringLiteral("\n这是 Block 0 UID 改写。普通 Classic 不支持；仅保留原厂商区域，验证完成后须重新贴卡。");
        advanced_target_t selected=target;
        if(QMessageBox::warning(this,QStringLiteral("确认高级写卡"),warning,QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
        if(!Advanced_Target_Equal(&selected,&target) || !online){message->setText(ReaderProtocol::statusText(STATUS_CARD_MISMATCH));return;}
    }
    pendingId=id;pendingTarget=target;pendingOperation=op;pendingAddress=a;pendingCount=n;pendingData=bytes;
    message->setText(QStringLiteral("请求已发送，等待固件核对当前卡片；不会自动重发"));timeout.start(20000);render();emit request(p);
}
void AdvancedPanel::finish(const QString &text){pendingId=0;timeout.stop();operationMessage=text;message->setText(text);emit logMessage(text);render();}
void AdvancedPanel::sendFailed(const QString &text){if(scanning)haltScan(text);finish(text);}
void AdvancedPanel::response(const QByteArray &p){
    advanced_result_t r;if(!ReaderProtocol::result(p,r)){emit logMessage(QStringLiteral("高级读写响应无效"));return;}
    if(!pendingId || r.id!=pendingId || !Advanced_Target_Equal(&pendingTarget,&r.target) || r.operation!=pendingOperation || r.address!=pendingAddress){emit logMessage(QStringLiteral("忽略不匹配 / 过期的高级读写响应"));return;}
    if(r.status==ADV_ACCEPTED){message->setText(ReaderProtocol::statusText(r.status));return;}
    if(pendingOperation==ADV_SCAN){
        bool coherent=r.completed<=pendingCount && r.length==r.completed*pendingTarget.unit && (r.status!=ADV_OK || r.completed==pendingCount);
        if(!coherent){haltScan(QStringLiteral("扫描响应长度或完成数量异常；不采纳本次数据，已有结果保留。"));finish(QStringLiteral("扫描响应无效"));return;}
        for(int x=0;x<pendingCount;x++){
            auto &cell=cells[pendingAddress+x];
            if(x<r.completed){cell.bytes=QByteArray(reinterpret_cast<const char*>(r.data)+x*pendingTarget.unit,pendingTarget.unit);cell.status=QStringLiteral("读取成功");}
            else if(cell.bytes.isEmpty())cell.status=scanError(r.status)+QStringLiteral(" · 0x%1").arg(r.detail,2,16,QChar('0'));
        }
        refreshResults();target.available=0;lease.stop();
        if(r.status==STATUS_CARD_MISMATCH || r.status==ADV_DISCONNECTED || r.status==ADV_INVALID)haltScan(QStringLiteral("目标或连接无效；停止扫描，已有数据保留。"));
        finish(QStringLiteral("本组读取 %1 / %2；数据已保留").arg(r.completed).arg(pendingCount));
        QTimer::singleShot(0,this,[this]{nextScan();});return;
    }
    QString text=ReaderProtocol::statusText(r.status);
    if(r.status==ADV_OK){
        bool trailerWrite=pendingTarget.protocol==0x80 && pendingAddress%4==3 && pendingOperation==ADV_WRITE;
        QByteArray returned(reinterpret_cast<const char *>(r.data),r.length);
        int expected=pendingOperation==ADV_UID_WRITE?16:pendingOperation==ADV_READ?pendingCount*pendingTarget.unit:pendingData.size();
        if(r.completed!=pendingCount || r.length!=expected ||
           (pendingOperation!=ADV_READ && pendingOperation!=ADV_UID_WRITE && !trailerWrite && returned!=pendingData) ||
           (pendingOperation==ADV_UID_WRITE && (returned.left(4)!=pendingData || quint8(returned[4])!=(quint8(pendingData[0])^quint8(pendingData[1])^quint8(pendingData[2])^quint8(pendingData[3])))) ||
           (trailerWrite && returned.mid(6,4)!=pendingData.mid(6,4)))text=QStringLiteral("响应回读与本次请求不一致，不能确认成功");
        else{prepareSnapshot(pendingTarget);for(int offset=0;offset<returned.size();offset+=pendingTarget.unit)cells[pendingAddress+offset/pendingTarget.unit]={returned.mid(offset,pendingTarget.unit),QStringLiteral("读取成功")};refreshResults();
            if(pendingOperation==ADV_READ)text=QStringLiteral("读取成功");if(trailerWrite)text+=QStringLiteral("；Key A 已重新认证，回读隐藏密钥，仅核对访问位和 GPB");}
    }
    text+=QStringLiteral(" [completed=%1, detail=0x%2]").arg(r.completed).arg(r.detail,2,16,QChar('0'));finish(text);
}
