#include "CardEmulationPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDateTime>
#include <QSettings>
#include <QtEndian>
#include <cstring>
extern "C" {
#include "ce_protocol.h"
}
static bool hexBytes(const QString &text,QByteArray &out,int minimum,int maximum){
    QString value=text;value.remove(QRegularExpression("\\s"));
    if(value.size()%2 || !QRegularExpression("^[0-9a-fA-F]*$").match(value).hasMatch())return false;
    out=QByteArray::fromHex(value.toLatin1());return out.size()>=minimum && out.size()<=maximum;
}
static QString ndefText(const QByteArray &data){
    if(data.isEmpty())return QStringLiteral("标签已清空（NLEN=0）");
    if(data.size()<5 || (quint8(data[0])&0x17)!=0x11 || data[1]!=1)return QStringLiteral("NDEF：")+QString::fromLatin1(data.toHex(' ').toUpper());
    const int size=quint8(data[2]);if(data.size()<4+size || !size)return QStringLiteral("NDEF：")+QString::fromLatin1(data.toHex(' ').toUpper());
    if(data[3]=='U'){
        const QStringList prefixes={"","http://www.","https://www.","http://","https://"};
        int prefix=quint8(data[4]);return QStringLiteral("URL：")+(prefix<prefixes.size()?prefixes[prefix]:QStringLiteral("[URI 前缀 %1]").arg(prefix))+QString::fromUtf8(data.mid(5,size-1));
    }
    if(data[3]=='T'){
        int language=quint8(data[4])&63;
        if(!(quint8(data[4])&128) && size>=1+language)return QStringLiteral("文本：")+QString::fromUtf8(data.mid(5+language,size-1-language));
    }
    return QStringLiteral("NDEF：")+QString::fromLatin1(data.toHex(' ').toUpper());
}
CardEmulationPanel::CardEmulationPanel(QWidget *parent):QWidget(parent){
    setObjectName("cardEmulationPanel");auto layout=new QVBoxLayout(this);layout->setSpacing(16);
    state=new QLabel(QStringLiteral("未连接 · 当前模式尚未确认"));state->setObjectName("ceState");
    state->setWordWrap(true);state->setStyleSheet("padding:16px;border-radius:10px;background:#eaf2ff;font-size:19px;font-weight:600;color:#15396b;");layout->addWidget(state);
    auto note=new QLabel(QStringLiteral("把 SKPass 作为 NFC Forum Type 4 标签：外部手机读取 URL / 文本，或用 NFC Tools 写入新 NDEF。卡模拟期间暂停实体卡读取、CardIO 刷卡输出与高级读写。"));note->setWordWrap(true);note->setProperty("role","muted");layout->addWidget(note);
    auto config=new QGroupBox(QStringLiteral("虚拟标签配置"));auto form=new QFormLayout(config);form->setVerticalSpacing(12);
    auto uidRow=new QHBoxLayout;uid=new QLineEdit;uid->setObjectName("ceUid");uid->setPlaceholderText("4 字节，如 12345678");uid->setMaxLength(11);auto random=new QPushButton(QStringLiteral("随机 UID"));uidRow->addWidget(uid,1);uidRow->addWidget(random);form->addRow(QStringLiteral("NFC-A UID"),uidRow);
    kind=new QComboBox;kind->setObjectName("ceNdefType");kind->addItems({QStringLiteral("网页链接 URL"),QStringLiteral("UTF-8 纯文本")});form->addRow(QStringLiteral("NDEF 类型"),kind);
    content=new QPlainTextEdit;content->setObjectName("ceNdefContent");content->setMaximumHeight(100);form->addRow(QStringLiteral("标签内容"),content);
    writable=new QCheckBox(QStringLiteral("允许外部设备写入 NDEF（最多 240 字节）"));writable->setChecked(true);form->addRow(writable);
    validation=new QLabel;validation->setWordWrap(true);form->addRow(validation);layout->addWidget(config);
    auto apduGroup=new QGroupBox(QStringLiteral("自定义 APDU 应答（最多 4 条，按表格顺序匹配）"));auto apduLayout=new QVBoxLayout(apduGroup);
    auto ruleNote=new QLabel(QStringLiteral("填写十六进制字节。匹配最多 16 字节；应答最多 32 字节，末尾包含 SW1 SW2。标准 NDEF 指令由内置引擎处理。留空的规则不启用。"));ruleNote->setWordWrap(true);ruleNote->setProperty("role","muted");apduLayout->addWidget(ruleNote);
    rules=new QTableWidget(4,3);rules->setObjectName("ceRules");rules->setHorizontalHeaderLabels({QStringLiteral("匹配方式"),QStringLiteral("APDU 匹配字节"),QStringLiteral("应答字节（含状态码）")});rules->verticalHeader()->hide();rules->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);rules->setMinimumHeight(170);
    rules->verticalHeader()->setDefaultSectionSize(46);
    rules->setMinimumHeight(235);
    for(int i=0;i<4;i++){auto type=new QComboBox;type->addItems({QStringLiteral("前缀"),QStringLiteral("完整匹配")});rules->setCellWidget(i,0,type);rules->setItem(i,1,new QTableWidgetItem);rules->setItem(i,2,new QTableWidgetItem);}
    apduLayout->addWidget(rules);auto fallbackRow=new QHBoxLayout;fallbackRow->addWidget(new QLabel(QStringLiteral("未匹配指令的兜底应答")));fallback=new QComboBox;fallback->addItems({"6A 82","90 00"});fallbackRow->addWidget(fallback);fallbackRow->addStretch();apduLayout->addLayout(fallbackRow);layout->addWidget(apduGroup);
    auto controls=new QHBoxLayout;apply=new QPushButton(QStringLiteral("应用配置并开启卡模拟"));apply->setObjectName("ceApply");apply->setProperty("role","primary");readerMode=new QPushButton(QStringLiteral("切回读卡器模式"));readerMode->setObjectName("ceReader");query=new QPushButton(QStringLiteral("查询当前模式"));controls->addWidget(apply);controls->addWidget(readerMode);controls->addWidget(query);controls->addStretch();layout->insertLayout(1,controls);
    received=new QLabel(QStringLiteral("外部写入的 NDEF：尚未收到"));received->setObjectName("ceReceivedNdef");received->setWordWrap(true);received->setTextInteractionFlags(Qt::TextSelectableByMouse);received->setTextFormat(Qt::PlainText);layout->addWidget(received);
    auto logRow=new QHBoxLayout;logRow->addWidget(new QLabel(QStringLiteral("外部交互 / APDU 抓包")));logRow->addStretch();auto clear=new QPushButton(QStringLiteral("清空日志"));logRow->addWidget(clear);layout->addLayout(logRow);
    capture=new QPlainTextEdit;capture->setObjectName("ceCapture");capture->setReadOnly(true);capture->setMaximumBlockCount(2000);capture->setMinimumHeight(240);capture->setStyleSheet("font-family:Consolas;font-size:13px;");layout->addWidget(capture);
    QSettings settings;uid->setText(settings.value("ce/uid","12345678").toString());kind->setCurrentIndex(settings.value("ce/kind",0).toInt());content->setPlainText(settings.value("ce/content","https://example.com").toString());
    connect(random,&QPushButton::clicked,this,[this]{uid->setText(QString::number(QRandomGenerator::global()->generate(),16).rightJustified(8,'0').toUpper());});
    connect(apply,&QPushButton::clicked,this,[this]{issue(CE_APPLY);});connect(readerMode,&QPushButton::clicked,this,[this]{issue(CE_READER);});connect(query,&QPushButton::clicked,this,[this]{issue(CE_QUERY);});connect(clear,&QPushButton::clicked,capture,&QPlainTextEdit::clear);
    auto validate=[this]{QByteArray bytes;uint8_t ndef[CARD_EMU_MAX_NDEF];auto text=content->toPlainText().toUtf8();size_t n=CardEmu_Build_Ndef(kind->currentIndex(),text.constData(),ndef,sizeof(ndef));bool valid=hexBytes(uid->text(),bytes,4,4) && n && (kind->currentIndex()!=0 || QRegularExpression("^https?://\\S+$").match(content->toPlainText()).hasMatch());validation->setText(valid?QStringLiteral("配置格式有效 · NDEF %1 / 240 字节").arg(n):QStringLiteral("请输入 4 字节 UID，以及长度不超限的 HTTP(S) URL 或文本。"));validation->setStyleSheet(valid?"color:#137044;":"color:#b42318;");};
    connect(uid,&QLineEdit::textChanged,this,validate);connect(content,&QPlainTextEdit::textChanged,this,validate);connect(kind,&QComboBox::currentIndexChanged,this,validate);validate();
    timeout.setSingleShot(true);connect(&timeout,&QTimer::timeout,this,[this]{pending=0;mode=CE_MODE_FAULT;state->setText(QStringLiteral("模式切换未确认，请查询当前模式；不会自动重发"));emit modeChanged(mode);render();});render();
}
void CardEmulationPanel::render(){apply->setEnabled(online && !pending);readerMode->setEnabled(online && !pending);query->setEnabled(online && !pending);}
void CardEmulationPanel::connected(bool available){online=available;pending=0;timeout.stop();state->setText(available?QStringLiteral("已连接 · 等待模式查询"):QStringLiteral("未连接支持卡模拟的 SKPass；交互记录保留"));state->setStyleSheet("padding:16px;border-radius:10px;background:#eaf2ff;color:#15396b;font-size:19px;font-weight:600;");render();if(available)issue(CE_QUERY);}
void CardEmulationPanel::issue(int action){
    if(!online || pending)return;
    ce_request_t request={};request.id=nextId++;if(!request.id)request.id=nextId++;request.action=uint8_t(action);
    if(action==CE_APPLY){
        QByteArray bytes;if(!hexBytes(uid->text(),bytes,4,4)){sendFailed(QStringLiteral("UID 必须是 4 字节十六进制"));return;}memcpy(request.config.uid,bytes.constData(),4);
        auto text=content->toPlainText().toUtf8();if(text.contains('\0') || (kind->currentIndex()==0 && !QRegularExpression("^https?://\\S+$").match(content->toPlainText()).hasMatch())){sendFailed(QStringLiteral("URL 必须以 http:// 或 https:// 开头且不含空格"));return;}
        request.config.ndef_len=uint16_t(CardEmu_Build_Ndef(kind->currentIndex(),text.constData(),request.config.ndef,sizeof(request.config.ndef)));if(!request.config.ndef_len){sendFailed(QStringLiteral("NDEF 内容超过 240 字节容量"));return;}
        request.config.write_enabled=writable->isChecked();bytes=QByteArray::fromHex(fallback->currentText().toLatin1());memcpy(request.config.fallback,bytes.constData(),2);
        for(int row=0;row<4;row++){
            QString match=rules->item(row,1)->text(),reply=rules->item(row,2)->text();if(match.trimmed().isEmpty() && reply.trimmed().isEmpty())continue;
            QByteArray m,r;if(!hexBytes(match,m,1,16) || !hexBytes(reply,r,2,32)){sendFailed(QStringLiteral("规则 %1：匹配需 1–16 字节，应答需 2–32 字节十六进制").arg(row+1));return;}
            auto &rule=request.config.rules[request.config.rule_count++];rule.prefix=qobject_cast<QComboBox *>(rules->cellWidget(row,0))->currentIndex()==0;rule.match_len=m.size();rule.response_len=r.size();memcpy(rule.match,m.constData(),m.size());memcpy(rule.response,r.constData(),r.size());
        }
        QSettings settings;settings.setValue("ce/uid",uid->text());settings.setValue("ce/kind",kind->currentIndex());settings.setValue("ce/content",content->toPlainText());
    }
    uint8_t bytes[512];size_t n=CE_Encode_Request(&request,bytes,sizeof(bytes));if(!n){sendFailed(QStringLiteral("卡模拟请求配置无效"));return;}
    pending=request.id;timeout.start(15000);if(action!=CE_QUERY){mode=CE_MODE_PENDING;state->setText(QStringLiteral("正在切换 NFC 模式…"));emit modeChanged(mode);}render();emit this->request(QByteArray(reinterpret_cast<const char *>(bytes),n));
}
void CardEmulationPanel::sendFailed(const QString &error){pending=0;timeout.stop();state->setText(error);emit logMessage(error);render();}
void CardEmulationPanel::response(const QByteArray &p){
    if(p.size()!=CE_REPLY_BYTES || quint8(p[4])>CE_DISCONNECTED || quint8(p[5])>CE_MODE_FAULT)return;
    quint32 id=qFromLittleEndian<quint32>(p.constData());if((id && id!=pending) || (!id && pending))return;
    if(quint8(p[4])==CE_ACCEPTED)return;
    pending=0;timeout.stop();mode=quint8(p[5]);
    QString text=mode==CE_MODE_LISTEN?QStringLiteral("正在模拟卡片 · NFC-A UID %1").arg(QString::fromLatin1(p.mid(6,4).toHex(' ').toUpper())):mode==CE_MODE_RW?QStringLiteral("读卡器模式 · 可以读取实体卡片"):QStringLiteral("卡模拟初始化失败 / 模式未就绪");
    if(quint8(p[4])!=CE_OK)text+=QStringLiteral(" · 设备状态 %1，错误 0x%2").arg(quint8(p[4])).arg(qFromLittleEndian<quint16>(p.constData()+10),4,16,QChar('0'));
    state->setText(text);state->setStyleSheet(mode==CE_MODE_LISTEN?"padding:16px;border-radius:10px;background:#f3e8ff;color:#6d28d9;font-size:19px;font-weight:600;":"padding:16px;border-radius:10px;background:#eaf2ff;color:#15396b;font-size:19px;font-weight:600;");emit modeChanged(mode);emit logMessage(text);render();
}
void CardEmulationPanel::event(const QByteArray &p){
    if(p.size()<CE_EVENT_HEADER || p[0]!=1 || quint8(p[1])<1 || quint8(p[1])>6)return;
    auto data=p.mid(CE_EVENT_HEADER);QString label;
    switch(quint8(p[1])){case CE_EVT_ACTIVATED:label=QStringLiteral("外部读卡器激活");break;case CE_EVT_DEACTIVATED:label=QStringLiteral("外部读卡器离场 · 继续监听");break;case CE_EVT_RX:label="RX APDU  "+QString::fromLatin1(data.toHex(' ').toUpper());break;case CE_EVT_TX:label="TX APDU  "+QString::fromLatin1(data.toHex(' ').toUpper());break;case CE_EVT_NDEF:label=QStringLiteral("外部写入 ")+ndefText(data);received->setText(label);break;case CE_EVT_ERROR:label=QStringLiteral("控制器错误：")+QString::fromUtf8(data);break;}
    capture->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")+QStringLiteral(" #%1 ").arg(qFromLittleEndian<quint32>(p.constData()+2))+label);
}
