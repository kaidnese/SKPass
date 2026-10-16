#include "MainWindow.h"
#include "AimeCode.h"
#include "Spad.h"
#include "CardTypes.h"
#include "WriteProtocol.h"
#include "CardManager.h"
#include "SegatoolsEditor.h"
#include "FirmwareUpdatePage.h"
#include "NfcPage.h"
#include "CardEmulationPanel.h"
#include "AdvancedPanel.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSerialPortInfo>
#include <QDateTime>
#include <QRegularExpression>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QInputDialog>
#include <QRandomGenerator>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QDir>
#include <QStorageInfo>
#include <QFileInfo>
#include <QFrame>
#include <QCloseEvent>

void MainWindow::renderConnection(SerialReader::ConnectionState state,const QString &detail) {
    QString title,foreground,background,border;
    if(state!=SerialReader::ConnectionState::Connected)nfcMode=0;
    switch(state) {
    case SerialReader::ConnectionState::Connected:
        if(nfcMode==1){
            title=QStringLiteral("已连接 · 正在模拟卡片");foreground="#6d28d9";background="#f3e8ff";border="#d8b4fe";
        }else{
            title=QStringLiteral("读卡器已连接");foreground="#137044";background="#eaf8f0";border="#a8d9be";
        }
        break;
    case SerialReader::ConnectionState::AwaitingHeartbeat:
        title=QStringLiteral("等待固件确认");foreground="#965800";background="#fff7e6";border="#efd092";break;
    case SerialReader::ConnectionState::Connecting:
        title=QStringLiteral("正在连接 / 重连");foreground="#965800";background="#fff7e6";border="#efd092";break;
    case SerialReader::ConnectionState::Disconnected:
        title=QStringLiteral("读卡器已断开");foreground="#b42332";background="#fff0f1";border="#efb6bd";break;
    }
    connectionPanel->setStyleSheet(QString("QFrame#connectionPanel{background:%1;border:1px solid %2;border-radius:10px;}").arg(background,border));
    connectionIndicator->setStyleSheet(QString("color:%1;font-size:22px;background:transparent;").arg(foreground));
    connection->setText(title);connection->setStyleSheet(QString("color:%1;font-size:16px;font-weight:700;background:transparent;").arg(foreground));
    connectionDetail->setText(detail);connectionDetail->setStyleSheet(QString("color:%1;font-size:12px;background:transparent;").arg(foreground));
    connectionPanel->setAccessibleName(title+QStringLiteral("，")+detail);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if(firmwarePage->updateInProgress()) {
        event->ignore();
        QMessageBox::information(this,QStringLiteral("固件更新进行中"),QStringLiteral("请等待更新操作结束后再关闭，以免中断固件写入。"));
        return;
    }
    QMainWindow::closeEvent(event);
}
MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("SKPass v1.0 · NFC 读取、读写与卡模拟"));resize(1280,920);setMinimumSize(1080,740);
    auto body=new QWidget;setCentralWidget(body);auto shell=new QVBoxLayout(body);
    shell->setContentsMargins(20,18,20,18);shell->setSpacing(12);
    auto top=new QHBoxLayout;ports=new QComboBox;ports->setMinimumWidth(360);
    auto refresh=new QPushButton(QStringLiteral("刷新设备"));
    auto connectButton=new QPushButton(QStringLiteral("连接"));auto disconnect=new QPushButton(QStringLiteral("断开"));
    connectButton->setProperty("role","primary");
    top->addWidget(ports,1);top->addWidget(refresh);top->addWidget(connectButton);top->addWidget(disconnect);shell->addLayout(top);
    connectionPanel=new QFrame;connectionPanel->setObjectName("connectionPanel");
    auto connectionLayout=new QVBoxLayout(connectionPanel);connectionLayout->setContentsMargins(16,10,16,10);connectionLayout->setSpacing(6);
    auto connectionRow=new QHBoxLayout;connectionRow->setSpacing(12);connectionLayout->addLayout(connectionRow);
    connectionIndicator=new QLabel(QString::fromUtf8("●"));connectionIndicator->setAlignment(Qt::AlignCenter);connectionIndicator->setFixedWidth(22);
    connection=new QLabel;connection->setObjectName("connectionStatus");
    connectionDetail=new QLabel;connectionDetail->setTextFormat(Qt::PlainText);connectionDetail->setWordWrap(true);
    connectionRow->addWidget(connectionIndicator);connectionRow->addWidget(connection);connectionRow->addWidget(connectionDetail,1);
    shell->addWidget(connectionPanel);
    renderConnection(SerialReader::ConnectionState::Disconnected,QStringLiteral("自动识别 USB 读卡器；游戏使用实体卡时请在这里断开"));
    disconnect->setEnabled(false);
    auto content=new QHBoxLayout;content->setSpacing(20);auto sidebar=new QWidget;sidebar->setObjectName("sidebar");sidebar->setFixedWidth(220);
    sidebar->setStyleSheet("QWidget#sidebar{background:#14243d;border-radius:12px;}");auto sideLayout=new QVBoxLayout(sidebar);sideLayout->setContentsMargins(12,22,12,16);sideLayout->setSpacing(10);
    auto brand=new QLabel("SKPass");brand->setStyleSheet("color:#ffffff;font-size:24px;font-weight:700;padding-left:12px;");sideLayout->addWidget(brand);
    auto tagline=new QLabel(QStringLiteral("PN7150 · 读卡工作台"));tagline->setStyleSheet("color:#b7c9e3;padding-left:12px;padding-bottom:16px;");sideLayout->addWidget(tagline);
    auto navigation=new QListWidget;navigation->setObjectName("navigation");navigation->addItems({QStringLiteral("读卡概览"),QStringLiteral("写卡与计算"),QStringLiteral("虚拟卡管理"),QStringLiteral("segatools 编辑"),QStringLiteral("NFC"),QStringLiteral("固件更新"),QStringLiteral("日志与原始数据")});navigation->setSpacing(6);navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation->setStyleSheet("QListWidget{border:0;background:#14243d;color:#e2eaf6;outline:0;}QListWidget::item{color:#e2eaf6;padding:14px 12px;border-radius:7px;font-size:15px;}QListWidget::item:hover{background:#233c5e;color:white;}QListWidget::item:selected,QListWidget::item:selected:!active{background:#2563eb;color:#ffffff;font-weight:600;}");
    sideLayout->addWidget(navigation,1);auto sideNote=new QLabel(QStringLiteral("实体读卡 · 虚拟卡 · 调试"));sideNote->setStyleSheet("color:#b7c9e3;padding-left:12px;font-size:12px;");sideLayout->addWidget(sideNote);
    auto pages=new QStackedWidget;content->addWidget(sidebar);content->addWidget(pages,1);shell->addLayout(content,1);
    auto heading=[](QVBoxLayout *l,const QString &title,const QString &description){l->setContentsMargins(0,8,0,0);l->setSpacing(14);auto h=new QLabel(title);h->setProperty("role","pageTitle");l->addWidget(h);auto sub=new QLabel(description);sub->setWordWrap(true);sub->setProperty("role","muted");l->addWidget(sub);};
    auto readPage=new QWidget;auto layout=new QVBoxLayout(readPage);pages->addWidget(readPage);
    heading(layout,QStringLiteral("读卡概览"),QStringLiteral("连接读卡器后刷卡，查看卡号、发行方和数据来源。"));
    auto writePage=new QWidget;auto writePageLayout=new QVBoxLayout(writePage);pages->addWidget(writePage);
    heading(writePageLayout,QStringLiteral("写卡与计算"),QStringLiteral("生成或输入卡号，确认目标卡后写入并回读验证。"));
    codeLabel=new QLabel(QStringLiteral("未取得卡号"));codeLabel->setObjectName("currentAccessCode");codeLabel->setAlignment(Qt::AlignCenter);
    codeLabel->setStyleSheet("color:#15396b;background:#eaf2ff;border:1px solid #cfe0fc;border-radius:12px;font-size:32px;font-family:Consolas;padding:36px 16px;");layout->addWidget(codeLabel);
    auto physicalRow=new QHBoxLayout;
    uidLabel=new QLabel(QStringLiteral("UID / IDm：—"));uidLabel->setObjectName("currentPhysicalId");
    uidLabel->setStyleSheet("color:#334b67;font-family:Consolas;font-size:14px;");uidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewHint=new QLabel(QStringLiteral("等待刷卡 · UID 不等于 Access Code"));previewHint->setProperty("role","muted");previewHint->setWordWrap(true);
    physicalRow->addWidget(uidLabel);physicalRow->addStretch();physicalRow->addWidget(previewHint);layout->addLayout(physicalRow);
    previewTimer=new QTimer(this);previewTimer->setSingleShot(true);previewTimer->setInterval(10000);previewTimer->setTimerType(Qt::PreciseTimer);
    connect(previewTimer,&QTimer::timeout,this,&MainWindow::clearPreview);
    auto actions=new QHBoxLayout;mask=new QCheckBox(QStringLiteral("隐藏中间卡号"));mask->setChecked(true);
    auto copy=new QPushButton(QStringLiteral("复制20位卡号"));copyCodeButton=copy;copy->setEnabled(false);auto csv=new QPushButton(QStringLiteral("导出CSV"));
    auto saveLog=new QPushButton(QStringLiteral("导出扫描日志"));
    actions->addWidget(mask);actions->addStretch();actions->addWidget(copy);actions->addWidget(csv);actions->addWidget(saveLog);layout->addLayout(actions);
    details=new QLabel;details->setWordWrap(true);layout->addWidget(details);
    errors=new QLabel;errors->setWordWrap(true);errors->setStyleSheet("color:#b15a00;");layout->addWidget(errors);
    auto manual=new QHBoxLayout;serial=new QLineEdit;serial->setMaxLength(8);serial->setPlaceholderText(QStringLiteral("输入8位明文Serial（不是UID）"));
    auto calculate=new QPushButton(QStringLiteral("计算Classic卡号"));auto reverse=new QPushButton(QStringLiteral("卡号反查Serial"));
    manual->addWidget(serial,1);manual->addWidget(calculate);manual->addWidget(reverse);writePageLayout->addLayout(manual);
    auto writerGroup=new QGroupBox(QStringLiteral("写入空白/自有 MIFARE Classic 1K（只写Block1、Block2）"));
    auto writerLayout=new QVBoxLayout(writerGroup);auto numberRow=new QHBoxLayout;
    writeCode=new QLineEdit;writeCode->setMaxLength(20);writeCode->setPlaceholderText(QStringLiteral("手动输入20位卡号，或用Serial生成"));
    auto generate=new QPushButton(QStringLiteral("用Serial生成（留空则随机）"));
    auto randomCode=new QPushButton(QStringLiteral("随机卡号"));
    numberRow->addWidget(writeCode,1);numberRow->addWidget(generate);numberRow->addWidget(randomCode);writerLayout->addLayout(numberRow);
    auto keyRow=new QHBoxLayout;keyPreset=new QComboBox;
    keyPreset->addItems({QStringLiteral("当前Key A：WCCFv2"),QStringLiteral("当前Key A：空白卡默认FF"),QStringLiteral("当前Key A：自定义")});
    keyPreset->setCurrentIndex(1);
    writeKey=new QLineEdit("FFFFFFFFFFFF");writeKey->setMaxLength(12);writeKey->setEchoMode(QLineEdit::Password);writeKey->setReadOnly(true);
    writeButton=new QPushButton(QStringLiteral("确认并写入目标卡"));writeButton->setEnabled(false);
    writeButton->setProperty("role","primary");
    keyRow->addWidget(keyPreset);keyRow->addWidget(writeKey,1);keyRow->addWidget(writeButton);writerLayout->addLayout(keyRow);
    writeSegaKey=new QCheckBox(QStringLiteral("写入 Block 3：将扇区 0 Key A 改为世嘉 Key（574343467632）"));
    writeSegaKey->setObjectName("writeSegaKey");writerLayout->addWidget(writeSegaKey);
    writeStatus=new QLabel(QStringLiteral("先连接写卡固件并读取目标卡；不改Block0/3，所以不会更换密钥。生成号不代表官方注册卡。"));
    writeStatus->setWordWrap(true);writerLayout->addWidget(writeStatus);writePageLayout->addWidget(writerGroup);writePageLayout->addStretch();
    cardManager=new CardManager;
    layout->addWidget(cardManager->createHistoryView(readPage));
    auto managerPage=new QWidget;auto managerLayout=new QVBoxLayout(managerPage);heading(managerLayout,QStringLiteral("虚拟卡管理"),QStringLiteral("勾选刷卡记录保存，添加备注，选择游戏中使用的虚拟卡。"));managerLayout->addWidget(cardManager,1);auto managerScroll=new QScrollArea;managerScroll->setWidgetResizable(true);managerScroll->setFrameShape(QFrame::NoFrame);managerScroll->setWidget(managerPage);pages->addWidget(managerScroll);
    auto saveGenerated=new QPushButton(QStringLiteral("保存当前输入/生成卡号到JSON"));writerLayout->addWidget(saveGenerated);
    connect(saveGenerated,&QPushButton::clicked,this,[this]{cardManager->saveNumber(writeCode->text());});
    rawPanel=new QPlainTextEdit;rawPanel->setReadOnly(true);
    logPanel=new QPlainTextEdit;logPanel->setReadOnly(true);logPanel->setMaximumBlockCount(2000);
    firmwarePage=new FirmwareUpdatePage(&reader);pages->addWidget(firmwarePage);
    connect(firmwarePage,&FirmwareUpdatePage::updateActivityChanged,this,[connectButton,disconnect,refresh,this](bool active){
        connectButton->setEnabled(!active);disconnect->setEnabled(!active);refresh->setEnabled(!active);ports->setEnabled(!active);
    });
    auto debugPage=new QWidget;auto debugLayout=new QVBoxLayout(debugPage);pages->addWidget(debugPage);
    heading(debugLayout,QStringLiteral("日志与原始数据"),QStringLiteral("查看通信报文和解析过程；日志可能包含完整卡号。"));
    auto rawGroup=new QGroupBox(QStringLiteral("原始数据（包含未掩码数据）"));rawGroup->setCheckable(true);rawGroup->setChecked(false);
    auto rawLayout=new QVBoxLayout(rawGroup);rawLayout->addWidget(rawPanel);rawPanel->hide();
    connect(rawGroup,&QGroupBox::toggled,rawPanel,&QWidget::setVisible);debugLayout->addWidget(rawGroup);
    auto logs=new QGroupBox(QStringLiteral("解析与扫描日志"));logs->setCheckable(true);logs->setChecked(true);
    auto logsLayout=new QVBoxLayout(logs);logsLayout->addWidget(logPanel);
    connect(logs,&QGroupBox::toggled,logPanel,&QWidget::setVisible);debugLayout->addWidget(logs,1);
    auto note=new QLabel(QStringLiteral("本地读出或计算的号码须与卡背核对；派生号不是卡背印号。CSV保留数据来源及未掩码卡号。"));
    note->setWordWrap(true);layout->addWidget(note);
    layout->addStretch();
    auto segatoolsPage=new SegatoolsEditor(cardManager); pages->insertWidget(3, segatoolsPage);
    nfcPage=new NfcPage;pages->insertWidget(4,nfcPage);
    connect(nfcPage->advanced,&AdvancedPanel::request,this,[this](const QByteArray &p){QString error;if(!reader.sendAdvanced(p,&error))nfcPage->advanced->sendFailed(error);});
    connect(nfcPage->advanced,&AdvancedPanel::logMessage,this,&MainWindow::log);
    connect(nfcPage->emulation,&CardEmulationPanel::request,this,[this](const QByteArray &p){QString error;if(!reader.sendCardEmulation(p,&error))nfcPage->emulation->sendFailed(error);});
    connect(nfcPage->emulation,&CardEmulationPanel::logMessage,this,&MainWindow::log);
    connect(nfcPage->emulation,&CardEmulationPanel::modeChanged,this,[this](int mode){
        nfcMode=mode;
        reader.setCardEmulationMode(mode);
        nfcPage->advanced->connected(reader.canAdvanced(),reader.firmwareId().contains("-scan-"));
        if(mode){targetUid.clear();clearPreview();}
        updateWriter();
        if(reader.isConnected()){
            renderConnection(SerialReader::ConnectionState::Connected,mode==1?QStringLiteral("正在模拟卡片 · 读卡与高级读写已暂停"):mode==2?QStringLiteral("正在切换 NFC 模式"):mode==3?QStringLiteral("NFC 模式未确认，请查询或切回读卡模式"):QStringLiteral("SKPass 读卡器模式"));
        }
    });
    connect(navigation,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);navigation->setCurrentRow(0);
    connect(refresh,&QPushButton::clicked,this,&MainWindow::refreshPorts);
    connect(connectButton,&QPushButton::clicked,this,[this]{if(!ports->currentData().toString().isEmpty())reader.open(ports->currentData().toString());});
    connect(disconnect,&QPushButton::clicked,this,[this]{reader.stop();log(QStringLiteral("已释放读卡器；游戏可自动连接"));});
    connect(mask,&QCheckBox::toggled,this,[this]{renderCode();});
    connect(copy,&QPushButton::clicked,this,[this]{if(code.size()==20)QApplication::clipboard()->setText(code);});
    connect(csv,&QPushButton::clicked,this,&MainWindow::exportCsv);
    connect(saveLog,&QPushButton::clicked,this,&MainWindow::exportLog);
    connect(calculate,&QPushButton::clicked,this,[this]{QString error;auto c=AimeCode::fromSerial(serial->text(),&error);
        rawPanel->clear();
        renderUid({});
        if(!c.isEmpty())writeCode->setText(c);
        showCode(c,QStringLiteral("手工计算，非自动读卡"),"SEGA", "Classic AiMe",error);});
    connect(keyPreset,&QComboBox::currentIndexChanged,this,[this](int index){
        writeKey->setReadOnly(index!=2);
        if(index<2)writeKey->setText(index==0 ? "574343467632" : "FFFFFFFFFFFF");
    });
    auto generateRandomCode=[this]{
        QString s,c;
        do {
            s=QString::number(QRandomGenerator::global()->bounded(100000000u)).rightJustified(8,'0');
            c=AimeCode::fromSerial(s);
        } while(c==writeCode->text());
        serial->setText(s);writeCode->setText(c);
    };
    connect(randomCode,&QPushButton::clicked,this,generateRandomCode);
    connect(generate,&QPushButton::clicked,this,[this,generateRandomCode]{
        QString s=serial->text();if(s.isEmpty()){generateRandomCode();return;}
        QString error;auto c=AimeCode::fromSerial(s,&error);
        if(c.isEmpty())QMessageBox::warning(this,QStringLiteral("生成失败"),error);else writeCode->setText(c);
    });
    connect(writeButton,&QPushButton::clicked,this,&MainWindow::startWrite);
    connect(&reader,&SerialReader::availabilityChanged,this,[this](bool available){
        if(!available){targetUid.clear();if(writeId){writeId=0;writeStatus->setText(QStringLiteral("连接中断，写卡结果未知；未自动重发，请重新读卡核对。"));}}
        updateWriter();
    });
    connect(reverse,&QPushButton::clicked,this,[this]{bool ok;auto text=QInputDialog::getText(this,QStringLiteral("反查Serial"),QStringLiteral("输入01036开头的20位卡号"),QLineEdit::Normal,code,&ok);
        if(ok){QString error;auto s=AimeCode::serialFromCode(text,&error);if(!s.isEmpty())serial->setText(s);else QMessageBox::warning(this,QStringLiteral("反查失败"),error);}});
    connect(&reader,&SerialReader::frame,this,&MainWindow::receive);
    connect(&reader,&SerialReader::state,this,[this](const QString &s){log(s);});
    connect(&reader,&SerialReader::connectionChanged,this,[this,disconnect](SerialReader::ConnectionState state,const QString &detail){
        renderConnection(state,detail);disconnect->setEnabled(state!=SerialReader::ConnectionState::Disconnected);
        if(state!=SerialReader::ConnectionState::Connected){clearPreview();nfcPage->resetSession();}
        else {nfcPage->advanced->connected(reader.canAdvanced(),reader.firmwareId().contains("-scan-"));nfcPage->emulation->connected(reader.canCardEmulation());}
    });
    connect(&reader,&SerialReader::badFramesChanged,this,[this](quint64 n){errors->setText(QStringLiteral("坏帧累计：%1").arg(n));});
    auto timer=new QTimer(this);connect(timer,&QTimer::timeout,this,&MainWindow::refreshPorts);timer->start(2000);
    refreshPorts();
    if(!Spad::tablesValid())errors->setText(QStringLiteral("SPAD表校验失败，禁止解码"));
}
void MainWindow::refreshPorts() {
    QString selected=ports->currentData().toString();ports->clear();
    ports->addItem(QStringLiteral("自动连接 SKPass USB（优先 HID）"),"auto");
    skpass_hid_device_t devices[16];size_t count=skpass_hid_list(devices,16);if(count>16)count=16;
    for(size_t i=0;i<count;++i) {
        const auto id=QString::fromWCharArray(devices[i].serial);
        ports->addItem(QStringLiteral("SKPass USB HID · ")+id,"hid:"+id);
    }
    for(const auto &p:QSerialPortInfo::availablePorts()) {
        bool ours=p.hasVendorIdentifier() && p.hasProductIdentifier() && p.vendorIdentifier()==0x1CCF && p.productIdentifier()==0x5252;
        ports->addItem(p.portName()+" · "+p.description()+" · "+p.serialNumber()+(ours ? " [SKPass]" : ""),p.portName());
    }
    int old=ports->findData(selected);ports->setCurrentIndex(old>=0?old:0);
}
void MainWindow::log(const QString &s) {logPanel->appendPlainText(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)+" "+s);}
void MainWindow::renderCode() {
    copyCodeButton->setEnabled(code.size()==20);
    if(code.isEmpty()){codeLabel->setText(QStringLiteral("未取得卡号"));return;}
    auto display=code;if(mask->isChecked())for(int i=4;i<16;++i)display[i]='*';
    QStringList groups;for(int i=0;i<20;i+=4)groups<<display.mid(i,4);codeLabel->setText(groups.join(' '));
}
void MainWindow::renderUid(const QByteArray &uid,const QString &label) {
    if(previewUid!=uid)previewResult.clear();
    previewUid=uid;
    const auto text=uid.isEmpty()?QStringLiteral("UID / IDm：—"):
        label+QStringLiteral("：")+QString::fromLatin1(uid.toHex(' ').toUpper());
    if(uidLabel->text()!=text)uidLabel->setText(text);
}
void MainWindow::clearPreview() {
    previewResult.clear();
    previewTimer->stop();code.clear();renderCode();renderUid({});
    details->clear();errors->clear();previewHint->setText(QStringLiteral("等待刷卡 · UID 不等于 Access Code"));
}
void MainWindow::showCode(const QString &value,const QString &source,const QString &issuer,const QString &type,const QString &message) {
    QString displayMessage=message;
    displayMessage.remove(QStringLiteral("；复用本地解析结果"));
    const QStringList result={QString::fromLatin1(previewUid.toHex()),value,source,issuer,type,displayMessage,QString::number(reader.badFrames())};
    // Only changed results update the overview. Repeated failures for a card
    // are still real scan events and must remain visible in the log/CSV.
    if(previewResult!=result) {
        previewResult=result;
        code=value;renderCode();details->setText(QStringLiteral("卡类型：%1　发行方：%2　数据来源：%3").arg(type,issuer,source));
        errors->setText(displayMessage+QStringLiteral("　坏帧累计：%1").arg(reader.badFrames()));
        previewHint->setText(value.isEmpty()?QStringLiteral("未取得 Access Code · UID 仅作识别 · 10 秒无新读卡后清空"):
            QStringLiteral("10 秒无新读卡后清空 · 历史记录保留"));
    }
    previewTimer->start(10000);
    log(QStringLiteral("结果：%1；发行方：%2；来源：%3；%4").arg(value.isEmpty()?QStringLiteral("未取得"):QStringLiteral("已取得20位号码"),issuer,source,message));
    rows.append({QDateTime::currentDateTime().toString(Qt::ISODateWithMs),type,issuer,source,value,message,rawPanel->toPlainText()});
    if(rows.size()>1000)rows.removeFirst();
}
void MainWindow::receive(quint8 msg,const QByteArray &payload,const QByteArray &raw) {
    if(msg==0x93){nfcPage->emulation->response(payload);return;}
    if(msg==0x14){nfcPage->emulation->event(payload);return;}
    /* Packets queued before the final mode reply must not repopulate a
     * physical-card preview while the device is listening as a tag. */
    if(reader.cardEmulationBlocksReading() && (msg==0x10 || msg==0x11 || msg==0x12 || msg==ADV_TARGET_MSG || msg==ADV_RESPONSE_MSG))return;
    if(msg==ADV_TARGET_MSG){nfcPage->advanced->targetFrame(payload);return;}
    if(msg==ADV_RESPONSE_MSG){nfcPage->advanced->response(payload);return;}
    if(msg==1 || msg==2 || msg==0xB1)return;
    if(msg==0x90) {
        WriteProtocol::Result r;
        if(!WriteProtocol::parse(payload,r)){log(QStringLiteral("写卡响应长度/字段无效"));return;}
        if(!writeId || r.id!=writeId){log(QStringLiteral("忽略过期写卡响应 id=%1 status=%2；不会重新发送").arg(r.id).arg(r.status));return;}
        if(((r.status==1 || r.status==5) && r.uid!=pendingUid) ||
           (!r.uid.isEmpty() && r.uid!=pendingUid)){log(QStringLiteral("写卡响应UID不匹配，未确认成功"));return;}
        QString message=WriteProtocol::statusText(r)+QStringLiteral(" [stage=%1, detail=0x%2]").arg(r.stage).arg(r.detail,2,16,QChar('0'));
        if(r.status==5 && ((r.verifiedMask&3)!=3 || r.data!=WriteProtocol::blocks(pendingCode))) {
            message=QStringLiteral("设备报告成功但回读数据不匹配，不能确认写入成功");r.status=9;
        }
        writeStatus->setText(message);log(message);
        if(r.status!=1){writeId=0;updateWriter();}
        if(r.status==5){
            renderUid(r.uid);
            decodedCards.clear();cardManager->record(pendingCode);
            rawPanel->setPlainText("WRITE VERIFIED UID: "+QString::fromLatin1(r.uid.toHex(' ').toUpper())+
                "\nBLOCK 1: "+QString::fromLatin1(r.data.left(16).toHex(' ').toUpper())+
                "\nBLOCK 2: "+QString::fromLatin1(r.data.mid(16).toHex(' ').toUpper()));
            showCode(pendingCode,QStringLiteral("来自写卡回读（非官方注册证明）"),issuerFromCode(pendingCode),"MIFARE Classic 1K",message);
        }
        return;
    }
    if(msg==0x11){nfcPage->activated({});log(QStringLiteral("CARD_REMOVED（已确认离场）"));return;}
    if(msg==SMARTCARD_DETAIL_MSG) {
        QString error;
        if(!nfcPage->receive(payload,&error))log(error);
        else rawPanel->setPlainText("NFC FRAME: "+QString::fromLatin1(raw.toHex(' ').toUpper())+
                                   "\nNFC JSON: "+QString::fromUtf8(payload));
        return;
    }
    if(msg==0x7F) {
        QString text=QString::fromLatin1(payload);log(text);
        auto tag=QRegularExpression("^TAG uid=([0-9A-F]{8}|[0-9A-F]{14}|[0-9A-F]{16}|[0-9A-F]{20}) mode=([0-9A-F]{2}) protocol=([0-9A-F]{2}) interface=([0-9A-F]{2}) sak=([0-9A-F]{2})$").match(text);
        if(tag.hasMatch()) {
            if(reader.cardEmulationBlocksReading())return;
            const auto uid=QByteArray::fromHex(tag.captured(1).toLatin1());
            nfcPage->activated(uid);
            const auto label=tag.captured(2)=="02"?QStringLiteral("IDm"):QStringLiteral("UID");
            const bool keepResult=previewUid==uid;
            // Keep both success and failure states during same-card retries.
            // Result changes are handled when the completed read arrives.
            if(!keepResult) {
                previewResult.clear();
                code.clear();renderCode();renderUid(uid,label);
                details->clear();errors->clear();previewHint->setText(QStringLiteral("已检测到卡片，正在读取 Access Code…"));
            }
            previewTimer->start(10000);
            targetUid=tag.captured(2)=="00" && tag.captured(3)=="80" && tag.captured(4)=="80" && tag.captured(5)=="08" ? QByteArray::fromHex(tag.captured(1).toLatin1()) : QByteArray();
            updateWriter();
        }
        auto start=QRegularExpression("^CARD idm=([0-9A-F]{16})").match(text);
        if(start.hasMatch())idBlocks.remove(start.captured(1));
        auto m=QRegularExpression("^FELICA idm=([0-9A-F]{16}).*block=82 .*result=ESP_OK data=([0-9A-F]{32})$").match(text);
        if(m.hasMatch()) {if(idBlocks.size()>128)idBlocks.clear();idBlocks.insert(m.captured(1),QByteArray::fromHex(m.captured(2).toLatin1()));}
        // Long FeliCa scans can exceed ten seconds. Matching diagnostic reads
        // are card activity too; don't expire the preview halfway through one.
        const auto activity=QRegularExpression("^FELICA idm=([0-9A-F]{16})").match(text);
        if(activity.hasMatch() && QByteArray::fromHex(activity.captured(1).toLatin1())==previewUid)
            previewTimer->start(10000);
        return;
    }
    if(msg!=0x10){log(QStringLiteral("未知消息0x%1").arg(msg,2,16,QChar('0')));return;}
    CardEvent e;QString error;
    if(!CardEvent::parse(payload,e,error)){log(error);return;}
    if(e.type==0x10) {
        // CardEvent carries an 8-byte padded/truncated ID; TAG carries its exact
        // length. Preserve only an ID matching this event, never the prior card.
        if(previewUid.isEmpty() || previewUid.left(8)!=e.idm.left(qMin(previewUid.size(),qsizetype(8))))
            renderUid(e.idm,QStringLiteral("UID（报文标识）"));
        else renderUid(previewUid);
    } else renderUid(e.idm,e.type==0x20?QStringLiteral("IDm"):QStringLiteral("UID"));
    const auto id=e.idm.toHex().toUpper();auto block=idBlocks.value(QString::fromLatin1(id));
    QByteArray plain;
    rawPanel->setPlainText("FRAME: "+QString::fromLatin1(raw.toHex(' ').toUpper())+
        "\nID/UID: "+QString::fromLatin1(e.idm.toHex(' ').toUpper())+
        "\nPMm: "+QString::fromLatin1(e.pmm.toHex(' ').toUpper())+
        "\nID block 82: "+QString::fromLatin1(block.toHex(' ').toUpper())+
        "\nS_PAD0: "+QString::fromLatin1(e.spad.toHex(' ').toUpper())+
        "\nMIFARE: "+QString::fromLatin1(e.mifare.toHex(' ').toUpper()));
    QString value,source,issuer,type,message=statusText(e.status);
    // Cache by full event data plus issuer block: a rewritten card changes the key.
    const auto cacheKey=payload+block;
    if(decodedCards.contains(cacheKey)) {
        const auto cached=decodedCards.value(cacheKey);
        if(e.type==0x10 || (e.type==0x20 && !cached[1].startsWith(QStringLiteral("派生号"))))cardManager->record(cached[0]);
        showCode(cached[0],cached[1],cached[2],cached[3],cached[4]+QStringLiteral("；复用本地解析结果"));return;
    }
    if(e.type==0x10) {
        type="MIFARE Classic";QString extraction;
        if(e.status==0)value=AimeCode::fromMifare(e.mifare,&extraction);
        source=QStringLiteral("来自卡内数据（未核对印号）");issuer=issuerFromCode(value);
        if(value.isEmpty()) {
            source=QStringLiteral("未取得");
            message+=e.status==0 ? QStringLiteral("；未发现有效卡号，可能为空白卡或数据格式不同；可在写卡区输入/生成号码") :
                       QStringLiteral("；未读到完整扇区数据，不能据此判断卡片为空白；检查当前密钥和连接");
        }
        if(!extraction.isEmpty())message+="；"+extraction;
    } else if(e.type==0x20) {
        type="FeliCa";issuer=issuerFromDfc(block.mid(8,2));
        QString decodeError;
        if(e.spad.size()==16)value=Spad::accessCode(e.spad,&plain,&decodeError);
        if(!value.isEmpty()) {
            source=QStringLiteral("来自 S_PAD0（未核对印号）");
            QString viaCode=issuerFromCode(value);
            if(block.isEmpty())issuer=viaCode;
            else if(viaCode!="未知前缀" && !issuer.contains(viaCode,Qt::CaseInsensitive))message+=QStringLiteral("；DFC与卡号前缀不一致，请核对");
        } else {
            value=AimeCode::derivedFromIdm(e.idm);
            source=QStringLiteral("派生号（非卡背印号）");
            message+=QStringLiteral("；未取得有效SPAD卡号，采用IDm派生号");
            if(!decodeError.isEmpty())message+="；"+decodeError;
            if(value.isEmpty())message+=QStringLiteral("；IDm无效，未生成号码");
        }
        rawPanel->appendPlainText("SPAD PLAIN: "+QString::fromLatin1(plain.toHex(' ').toUpper()));
    } else {
        type=e.type==0x30 ? "ISO15693" : QStringLiteral("其它卡片");
        source=QStringLiteral("仅物理标识");issuer=QStringLiteral("未知");
    }
    if(!value.isEmpty()) {
        if(decodedCards.size()>=128)decodedCards.clear();
        decodedCards.insert(cacheKey,{value,source,issuer,type,message});
        if(e.type==0x10 || (e.type==0x20 && !source.startsWith(QStringLiteral("派生号"))))cardManager->record(value);
    }
    showCode(value,source,issuer,type,message);
}
void MainWindow::exportCsv() {
    auto name=QFileDialog::getSaveFileName(this,QStringLiteral("导出本次会话（最多1000条）"),"card_log.csv","CSV (*.csv)");
    if(name.isEmpty())return;QFile file(name);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("保存失败"),file.errorString());return;}
    QTextStream out(&file);out.setGenerateByteOrderMark(true);
    out<<"timestamp,type,issuer,source,access_code,message,raw_data\n";
    for(const auto &row:rows) {
        QStringList escaped;
        for(auto field:row){field.replace('"',"\"\"");escaped<<'"'+field+'"';}
        out<<escaped.join(',')<<'\n';
    }
    log(QStringLiteral("CSV已保存，包含未掩码卡号和数据来源：")+name);
}
void MainWindow::exportLog() {
    auto name=QFileDialog::getSaveFileName(this,QStringLiteral("导出当前扫描日志（最多2000行）"),"felica_scan.log","Log (*.log *.txt)");
    if(name.isEmpty())return;
    QFile file(name);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,QStringLiteral("保存失败"),file.errorString());return;}
    file.write(logPanel->toPlainText().toUtf8());
    log(QStringLiteral("扫描日志已保存（包含原始卡数据）：")+name);
}
void MainWindow::updateWriter() {
    writeButton->setEnabled(reader.canWrite() && !targetUid.isEmpty() && !writeId);
    writeButton->setToolTip(!reader.canWrite() ? QStringLiteral("需要连接2.1-write固件；旧2.0只支持读取") :
        targetUid.isEmpty() ? QStringLiteral("需要先读取SAK=08、MIFARE Tag接口的Classic 1K卡") :
        QStringLiteral("目标UID：")+QString::fromLatin1(targetUid.toHex(' ').toUpper()));
}
void MainWindow::startWrite() {
    if(!reader.canWrite() || targetUid.isEmpty() || writeId)return;
    if(!QRegularExpression("^[0-9A-Fa-f]{12}$").match(writeKey->text()).hasMatch()) {
        QMessageBox::warning(this,QStringLiteral("密钥格式错误"),QStringLiteral("当前Key A必须是12位十六进制；它仅用于认证，不会写入Block3。"));return;
    }
    quint32 id;do{id=QRandomGenerator::global()->generate();}while(!id);
    const auto selectedUid=targetUid;const auto selectedCode=writeCode->text();
    QString error;auto request=WriteProtocol::request(id,selectedUid,QByteArray::fromHex(writeKey->text().toLatin1()),selectedCode,&error,writeSegaKey->isChecked());
    if(request.isEmpty()){QMessageBox::warning(this,QStringLiteral("写卡参数错误"),error);return;}
    QString warning=QStringLiteral("目标UID：%1\n卡号：%2\n\n将覆盖Block1全部16字节为00，Block2前6字节为00、后10字节为BCD卡号。%3\n生成号码不代表官方注册或游戏兼容。移走卡片/断电可能造成部分写入。\n\n只放这一张自有卡，确认写入吗？")
        .arg(QString::fromLatin1(selectedUid.toHex(' ').toUpper()),selectedCode,
             writeSegaKey->isChecked()?QStringLiteral("\n同时将 Block3 Key A 改为 574343467632，并设置标准可读写访问位；这会改变密钥。"):
                                      QStringLiteral("\n不改 Block0/3，不修改 UID、密钥或访问控制。"));
    if(QMessageBox::question(this,QStringLiteral("确认覆盖卡片数据"),warning,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    pendingUid=selectedUid;pendingCode=selectedCode;writeId=id;updateWriter();
    if(!reader.sendWrite(request,&error)){writeId=0;updateWriter();writeStatus->setText(error);return;}
    writeStatus->setText(QStringLiteral("命令已提交，等待固件确认；不会自动重发。请保持/重新放入目标卡。"));
    log(QStringLiteral("提交一次性写卡 id=%1，UID=%2；认证密钥不记入日志").arg(id).arg(QString::fromLatin1(pendingUid.toHex().toUpper())));
    QTimer::singleShot(20000,this,[this,id]{if(writeId==id){writeId=0;targetUid.clear();updateWriter();writeStatus->setText(QStringLiteral("未收到最终结果，写卡结果未知；请重新读卡，不要自动重试。"));}});
}
