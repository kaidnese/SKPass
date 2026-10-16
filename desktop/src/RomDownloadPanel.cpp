#include "RomDownloadPanel.h"
#include "RomDownload.h"
#include "FirmwareImage.h"
#include "SerialReader.h"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSerialPortInfo>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <memory>

namespace {
bool confirm(QWidget *parent,const QString &title,const QString &text,const QString &action) {
    QDialog dialog(parent);dialog.setWindowTitle(title);dialog.setMinimumWidth(520);
    auto layout=new QVBoxLayout(&dialog);layout->setContentsMargins(24,24,24,24);layout->setSpacing(18);
    auto heading=new QLabel(title);heading->setStyleSheet("font-size:19px;font-weight:600;color:#203b61;");layout->addWidget(heading);
    auto note=new QLabel(text);note->setWordWrap(true);note->setTextFormat(Qt::PlainText);layout->addWidget(note);
    auto buttons=new QDialogButtonBox;auto ok=buttons->addButton(action,QDialogButtonBox::AcceptRole);
    ok->setProperty("role","primary");buttons->addButton(QStringLiteral("取消"),QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);return dialog.exec()==QDialog::Accepted;
}
}
RomDownloadPanel::RomDownloadPanel(SerialReader *r,QWidget *parent):QWidget(parent),reader(r) {
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,16);layout->setSpacing(14);
    auto note=new QLabel(QStringLiteral("ROM 串口下载 · 适合更新应用、首次烧录和恢复固件\n进入后停止读卡，CardIO / HID / CDC 暂时退出，由芯片 ROM 接管 USB 和 UART0。烧录完成后请重新插拔 USB。请勿把应用 BIN 当作完整 Flash 镜像。"));
    note->setWordWrap(true);note->setStyleSheet("background:#fff7e6;color:#854d0e;padding:14px;border-radius:10px;");layout->addWidget(note);
    auto form=new QFormLayout;
    transport=new QComboBox;transport->setObjectName("romTransport");transport->addItems({QStringLiteral("原生 USB 下载"),QStringLiteral("外接 UART0（TXD0 / RXD0）")});
    form->addRow(QStringLiteral("连接方式"),transport);
    mode=new QComboBox;mode->setObjectName("romFlashMode");mode->addItems({QStringLiteral("应用更新 · 保留原启动分区"),QStringLiteral("首次烧录／修复 · 重写启动文件")});
    form->addRow(QStringLiteral("烧录范围"),mode);
    auto portRow=new QHBoxLayout;port=new QComboBox;port->setObjectName("romPort");port->setMinimumContentsLength(24);
    scan=new QPushButton(QStringLiteral("刷新端口"));portRow->addWidget(port,1);portRow->addWidget(scan);form->addRow(QStringLiteral("下载串口"),portRow);layout->addLayout(form);
    auto wiring=new QLabel(QStringLiteral("USB：连接原生 USB 数据口，进入后串口可能换号；单台设备可自动查找。\nROM 可能同时出现 DFU 未安装驱动的提示；只要「USB 串行设备 (COM…)」正常，就能通过串口下载。\nUART0：GPIO43/TXD0 → 转串口 RX，GPIO44/RXD0 ← 转串口 TX，GND 共地，使用 3.3V 电平。\n旧固件或无法连接时：按住 BOOT（GPIO0），按一下 RESET，再松开 BOOT；GPIO46 应悬空或为低电平。"));
    wiring->setWordWrap(true);wiring->setProperty("role","muted");layout->addWidget(wiring);
    auto actions=new QHBoxLayout;
    enter=new QPushButton(QStringLiteral("1. 进入 ROM 下载模式"));enter->setObjectName("enterRomDownload");
    actions->addWidget(enter);actions->addStretch();layout->addLayout(actions);
    file=new QLabel(QStringLiteral("未选择固件 · 支持 SKPass 应用 .BIN / .UF2"));file->setObjectName("romFirmwareFile");file->setWordWrap(true);layout->addWidget(file);
    auto files=new QHBoxLayout;choose=new QPushButton(QStringLiteral("2. 选择固件"));choose->setObjectName("chooseRomFirmware");
    flash=new QPushButton(QStringLiteral("3. 开始烧录"));flash->setObjectName("flashRomFirmware");flash->setProperty("role","primary");
    files->addWidget(choose);files->addWidget(flash);files->addStretch();layout->addLayout(files);
    progress=new QProgressBar;progress->setObjectName("romProgress");progress->setRange(0,100);progress->setValue(0);layout->addWidget(progress);
    status=new QLabel(QStringLiteral("请先选择连接方式。首次使用软件进入 ROM 需要 2.7.0 固件；也可手动 BOOT + RESET 进入。"));status->setObjectName("romStatus");status->setWordWrap(true);layout->addWidget(status);
    log=new QPlainTextEdit;log->setObjectName("romLog");log->setReadOnly(true);log->setMaximumBlockCount(500);log->setMinimumHeight(160);layout->addWidget(log);layout->addStretch();
    requestTimer=new QTimer(this);requestTimer->setSingleShot(true);requestTimer->setInterval(8000);
    connect(requestTimer,&QTimer::timeout,this,[this]{requested=false;status->setText(QStringLiteral("未收到 ROM 确认；请核对是否已出现 Espressif 下载串口，或手动 BOOT + RESET。"));refresh();emit busyChanged(false);});
    connect(reader,&SerialReader::frame,this,[this](quint8 msg,const QByteArray &p,const QByteArray &){
        if(msg!=0xB4 || p.size()!=1 || !requested)return;
        requestTimer->stop();requested=false;
        if(p[0]==0) {reader->stop();status->setText(QStringLiteral("设备已确认 ROM 切换，读卡暂停。等待下载串口出现，选择固件后点击烧录。"));}
        else status->setText(p[0]==1?QStringLiteral("正在写卡，不能进入 ROM 下载。 "):p[0]==3?QStringLiteral("设备 eFuse 禁用了软件 ROM 下载；未修改任何 eFuse。 "):QStringLiteral("设备拒绝进入 ROM 下载，请重连后重试。"));
        refreshPorts();refresh();emit busyChanged(false);
    });
    connect(reader,&SerialReader::availabilityChanged,this,[this](bool){
        if(waitingForReader && reader->isConnected()) {
            waitingForReader=false;status->setStyleSheet("color:#16803d;");
            status->setText(QStringLiteral("串口烧录与校验完成；读卡器已重新连接，可继续刷卡。"));
        }
        refresh();
    });
    connect(enter,&QPushButton::clicked,this,&RomDownloadPanel::requestEntry);
    connect(scan,&QPushButton::clicked,this,&RomDownloadPanel::refreshPorts);
    connect(transport,&QComboBox::currentIndexChanged,this,[this]{refreshPorts();refresh();});
    connect(choose,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择 SKPass 应用固件"),{},QStringLiteral("应用固件 (*.bin *.uf2)"));if(!path.isEmpty())selectFirmware(path);});
    connect(flash,&QPushButton::clicked,this,&RomDownloadPanel::startFlash);
    connect(port,&QComboBox::currentIndexChanged,this,[this]{refresh();});
    portTimer=new QTimer(this);portTimer->setInterval(1500);connect(portTimer,&QTimer::timeout,this,[this]{if(!busy)refreshPorts();});portTimer->start();
    refreshPorts();refresh();
}
void RomDownloadPanel::setExternalBusy(bool value) {externalBusy=value;refresh();}
void RomDownloadPanel::refresh() {
    const bool idle=!busy && !requested && !externalBusy;
    transport->setEnabled(idle);port->setEnabled(idle);mode->setEnabled(idle);scan->setEnabled(idle);choose->setEnabled(idle);
    const bool target=transport->currentIndex()==0 || (port->currentIndex()>=0 && !port->currentData().toString().isEmpty());
    enter->setEnabled(idle && reader->canRomDownload());
    flash->setEnabled(idle && !reader->isConnected() && !selectedFile.isEmpty() && target);
}
void RomDownloadPanel::refreshPorts() {
    const auto selected=port->currentData().toString();const auto previousIndex=port->currentIndex();port->clear();
    if(transport->currentIndex()==0)port->addItem(QStringLiteral("自动查找原生 USB 下载串口"),QString());
    const auto native=RomDownload::nativePorts();
    for(const auto &p:QSerialPortInfo::availablePorts()) {
        if(transport->currentIndex()==0 && !native.contains(p.portName()))continue;
        if(p.hasVendorIdentifier() && p.vendorIdentifier()==0x1CCF)continue; // application CDC is not a ROM port
        port->addItem(p.portName()+" · "+p.description(),p.portName());
    }
    const int index=port->findData(selected);if(index>=0)port->setCurrentIndex(index);
    else if(previousIndex<0 || transport->currentIndex()==1)port->setCurrentIndex(-1);
    refresh();
}
void RomDownloadPanel::selectFirmware(const QString &path) {
    if(isBusy() || externalBusy)return;
    QFile f(path);QString error;
    if(!f.open(QIODevice::ReadOnly) || f.size()>FirmwareImage::MaxSize*2)error=QStringLiteral("无法读取固件或文件过大。");
    else FirmwareImage::prepare(f.readAll(),path.endsWith(".uf2",Qt::CaseInsensitive),&error);
    if(!error.isEmpty()){status->setStyleSheet("color:#b42318;");status->setText(error);return;}
    selectedFile=path;file->setText(QFileInfo(path).fileName());file->setToolTip(path);status->setStyleSheet({});
    status->setText(QStringLiteral("固件校验通过；确认目标端口后点击烧录。"));refresh();
}
void RomDownloadPanel::requestEntry() {
    if(isBusy() || externalBusy)return;
    const QString details=QStringLiteral("即将停止读卡并重启进入芯片 ROM。\n\n正常的 CardIO、HID 和 CDC 将消失；USB 可能出现新的 COM 号，不会同时保留读卡功能。DFU 缺驱动不影响正常 COM 串口下载。\n\nUART0 需连接 3.3V 转串口模块。只进入模式不会擦写 Flash。烧录完成或放弃下载时，请重新插拔 USB。");
    if(!confirm(this,QStringLiteral("进入 ROM 下载模式"),details,QStringLiteral("确认进入")))return;
    previousNativePorts=RomDownload::nativePorts();
    QString error;if(!reader->enterRomDownload(transport->currentIndex()==1,&error)){status->setText(error);return;}
    requested=true;status->setText(QStringLiteral("等待设备确认 ROM 下载请求…"));requestTimer->start();refresh();emit busyChanged(true);
}
void RomDownloadPanel::startFlash() {
    if(isBusy() || externalBusy || selectedFile.isEmpty())return;
    if(transport->currentIndex()==1 && (port->currentIndex()<0 || port->currentData().toString().isEmpty())) {status->setText(QStringLiteral("请选择实际连接 UART0 的转串口端口。"));return;}
    const bool first=mode->currentIndex()==1;
    const auto target=port->currentData().toString();
    const QString details=QStringLiteral("目标：%1\n固件：%2\n\n%3\n\n请确认设备已进入 ROM 下载模式，松开 BOOT，烧录期间不要断电或关闭程序。烧录完成后请重新插拔 USB。此操作不会自动重复写入。")
        .arg(target.isEmpty()?QStringLiteral("自动查找单台 Espressif USB 下载设备"):target,QFileInfo(selectedFile).fileName(),first?
             QStringLiteral("首次烧录／修复将覆盖 bootloader、分区表、OTA 启动记录和 ota_0。配套启动文件来自此版本 SKPass 发行包。现有启动记录会被重置，写入中断可能需要重新完整烧录。"):
             QStringLiteral("应用更新将检查设备分区表，写入备用应用分区并校验摘要，成功后才切换启动记录；保留原应用分区。"));
    if(!confirm(this,first?QStringLiteral("确认首次烧录／修复"):QStringLiteral("确认串口烧录"),details,QStringLiteral("开始烧录")))return;
    startOperation();
}
void RomDownloadPanel::startOperation() {
    const auto target=port->currentData().toString();
    waitingForReader=false;
    RomDownload::Options options;options.firmware=selectedFile;options.port=target;options.firstFlash=mode->currentIndex()==1;
    options.tool=RomDownload::toolPath();options.firstFlashDirectory=RomDownload::firstFlashPath();
    if(target.isEmpty())options.previousNativePorts=previousNativePorts;
    reader->stop();busy=true;refresh();emit busyChanged(true);log->clear();progress->setValue(0);status->setStyleSheet({});
    QPointer<RomDownloadPanel> guard(this);auto result=std::make_shared<RomDownload::Result>();
    auto worker=QThread::create([options,guard,result]{
        const auto reporter=[guard](int value,const QString &text){if(guard)QMetaObject::invokeMethod(guard,[guard,value,text]{if(guard){if(value>=0)guard->progress->setValue(value);guard->log->appendPlainText(text);if(value>=0)guard->status->setText(text);}},Qt::QueuedConnection);};
        *result=RomDownload::run(options,reporter);
    });
    connect(worker,&QThread::finished,this,[this,result]{complete(result->error,result->committed);});
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();
}
void RomDownloadPanel::complete(const QString &error,bool committed) {
    busy=false;requested=false;requestTimer->stop();waitingForReader=error.isEmpty() && committed;refresh();emit busyChanged(false);
    status->setStyleSheet(error.isEmpty()?"color:#16803d;":"color:#b42318;");
    status->setText(error.isEmpty()?QStringLiteral("串口烧录与校验完成。请重新插拔 USB，上位机会自动重新连接读卡器。"):
        error+(committed?QStringLiteral("\n新固件已经选定，请重新插拔 USB 后核对固件版本。"):QStringLiteral("\n请查看下载日志。未确认完成，不能视为更新成功。退出下载模式请重新插拔 USB。")));
    if(waitingForReader){progress->setValue(100);reader->waitForFirmwareReconnect();}
}
