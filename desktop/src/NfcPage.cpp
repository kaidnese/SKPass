#include "NfcPage.h"
#include "CardEmulationPanel.h"
#include "AdvancedPanel.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFormLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QDateTime>
#include <QTabBar>
#include <QStackedWidget>

namespace {
QString field(const QJsonObject &o,const char *key) {
    const auto v=o.value(key).toString();return v.isEmpty()?QStringLiteral("未取得"):v;
}
QString formattedDate(const QString &v) {
    if(v.isEmpty())return QStringLiteral("未取得");
    return v.size()==14?QDateTime::fromString(v,"yyyyMMddhhmmss").toString("yyyy-MM-dd hh:mm:ss"):
                       QDate::fromString(v,"yyyyMMdd").toString("yyyy-MM-dd");
}
QLabel *value(const QString &name) {
    auto label=new QLabel(QStringLiteral("未取得"));label->setObjectName(name);label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);label->setTextInteractionFlags(Qt::TextSelectableByMouse);return label;
}
}
NfcPage::NfcPage(QWidget *parent):QWidget(parent) {
    setObjectName("nfcPage");auto outer=new QVBoxLayout(this);outer->setContentsMargins(0,8,0,0);outer->setSpacing(14);
    auto title=new QLabel("NFC");title->setProperty("role","pageTitle");outer->addWidget(title);
    auto intro=new QLabel(QStringLiteral("选择 NFC 功能：识别公开信息、操作存储卡，或模拟 Type 4 NDEF / APDU 标签。"));
    intro->setProperty("role","muted");intro->setWordWrap(true);outer->addWidget(intro);
    auto functions=new QTabBar;functions->setObjectName("nfcFunctions");functions->setExpanding(true);functions->setDrawBase(false);
    functions->setAccessibleName(QStringLiteral("NFC 功能选择"));
    functions->addTab(QStringLiteral("交通卡识别"));functions->addTab(QStringLiteral("高级读写"));
    functions->addTab(QStringLiteral("卡模拟"));
    functions->setTabToolTip(2,QStringLiteral("Type 4 NDEF 虚拟标签、自定义 UID 与 APDU 应答"));
    functions->setTabToolTip(0,QStringLiteral("交通卡、银行卡公开信息与存储卡型号"));
    functions->setTabToolTip(1,QStringLiteral("按当前卡型提供块、页、NDEF 与 UID 操作"));
    functions->setStyleSheet("QTabBar{background:#e8eef7;border-radius:10px;padding:4px;}"
        "QTabBar::tab{background:transparent;color:#52647a;border:0;border-radius:7px;"
        "padding:12px 20px;font-size:15px;font-weight:600;min-height:22px;}"
        "QTabBar::tab:hover{background:#dce7f7;color:#15396b;}"
        "QTabBar::tab:selected{background:#2563eb;color:white;}");
    outer->addWidget(functions);
    auto pages=new QStackedWidget;pages->setObjectName("nfcFunctionPages");outer->addWidget(pages,1);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("nfcRecognitionPage");
    auto content=new QWidget;auto layout=new QVBoxLayout(content);layout->setContentsMargins(0,0,8,8);layout->setSpacing(16);scroll->setWidget(content);pages->addWidget(scroll);
    auto recognitionIntro=new QLabel(QStringLiteral("自动读取交通卡、银行卡的公开信息，并识别存储卡型号与容量。交通卡号与街机 Access Code 分开显示。"));
    recognitionIntro->setProperty("role","muted");recognitionIntro->setWordWrap(true);layout->addWidget(recognitionIntro);
    auto card=new QFrame;card->setObjectName("nfcSummaryCard");card->setStyleSheet("QFrame#nfcSummaryCard{background:#eaf2ff;border:1px solid #cfe0fc;border-radius:14px;}");
    auto cardLayout=new QVBoxLayout(card);cardLayout->setContentsMargins(24,20,24,20);cardLayout->setSpacing(12);
    type=value("nfcType");type->setText(QStringLiteral("等待刷卡"));type->setStyleSheet("font-size:24px;font-weight:700;color:#15396b;background:transparent;");cardLayout->addWidget(type);
    auto row=new QHBoxLayout;auto numberColumn=new QVBoxLayout;numberColumn->addWidget(new QLabel(QStringLiteral("卡片公开卡号")));
    number=value("nfcNumber");number->setStyleSheet("font-family:Consolas;font-size:22px;color:#15396b;background:transparent;");numberColumn->addWidget(number);row->addLayout(numberColumn,2);
    auto balanceColumn=new QVBoxLayout;balanceColumn->addWidget(new QLabel(QStringLiteral("余额")));balance=value("nfcBalance");balance->setStyleSheet("font-size:22px;font-weight:600;color:#137044;background:transparent;");balanceColumn->addWidget(balance);row->addLayout(balanceColumn,1);cardLayout->addLayout(row);
    physicalId=value("nfcPhysicalId");physicalId->setText(QStringLiteral("UID / IDm：未取得"));cardLayout->addWidget(physicalId);layout->addWidget(card);
    status=value("nfcStatus");status->setText(QStringLiteral("连接读卡器并贴卡；卡片未开放的字段显示“未取得”。"));layout->addWidget(status);
    meta=value("nfcMeta");meta->setProperty("role","muted");layout->addWidget(meta);
    auto historyTitle=new QLabel(QStringLiteral("最近交易明细"));historyTitle->setStyleSheet("font-size:17px;font-weight:600;");layout->addWidget(historyTitle);
    history=new QTableWidget(0,6);history->setObjectName("nfcHistory");history->setHorizontalHeaderLabels({QStringLiteral("日期 / 时间"),QStringLiteral("类型代码"),QStringLiteral("金额"),QStringLiteral("余额"),QStringLiteral("进站代码"),QStringLiteral("出站代码")});
    history->setEditTriggers(QAbstractItemView::NoEditTriggers);history->setSelectionBehavior(QAbstractItemView::SelectRows);history->setAlternatingRowColors(true);history->verticalHeader()->hide();
    history->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);history->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);history->setMinimumHeight(240);layout->addWidget(history,1);
    auto note=new QLabel(QStringLiteral("基础探测仅读取公开数据；部分卡不会开放卡号、余额或交易记录。站点与交易类型显示原始代码，未取得字段不作推测。Octopus 历史余额公式仅作估算。"));note->setProperty("role","muted");note->setWordWrap(true);layout->addWidget(note);
    layout->addStretch();
    auto advancedScroll=new QScrollArea;advancedScroll->setObjectName("nfcAdvancedPage");
    advancedScroll->setWidgetResizable(true);advancedScroll->setFrameShape(QFrame::NoFrame);
    auto advancedContent=new QWidget;auto advancedLayout=new QVBoxLayout(advancedContent);
    advancedLayout->setContentsMargins(0,0,8,8);advancedLayout->setSpacing(14);
    auto advancedIntro=new QLabel(QStringLiteral("贴卡后自动显示对应卡型的读写选项。每次操作绑定当前卡片，写入前确认，完成后回读核对。"));
    advancedIntro->setWordWrap(true);advancedIntro->setProperty("role","muted");advancedLayout->addWidget(advancedIntro);
    advanced=new AdvancedPanel(advancedContent,true);advancedLayout->addWidget(advanced);advancedLayout->addStretch();
    advancedScroll->setWidget(advancedContent);pages->addWidget(advancedScroll);
    auto ceScroll=new QScrollArea;ceScroll->setObjectName("nfcEmulationPage");
    ceScroll->setWidgetResizable(true);ceScroll->setFrameShape(QFrame::NoFrame);
    emulation=new CardEmulationPanel;ceScroll->setWidget(emulation);pages->addWidget(ceScroll);
    connect(functions,&QTabBar::currentChanged,pages,&QStackedWidget::setCurrentIndex);
}
void NfcPage::activated(const QByteArray &uid){assembler.activated(uid);advanced->activated(uid);}
void NfcPage::resetSession(){assembler.reset();advanced->connected(false);emulation->connected(false);}
bool NfcPage::receive(const QByteArray &payload,QString *error) {
    NfcDocument doc;const auto result=assembler.feed(payload,doc,error);
    if(result==NfcDetails::Result::Rejected)return false;
    if(result==NfcDetails::Result::Complete) {
        // Scan IDs change on every read; compare the contents to avoid flicker.
        auto comparable=doc;comparable.summary.remove("scan");
        for(auto &r:comparable.records)r.remove("scan");
        if(!(comparable==current)){current=comparable;display(doc);}
    }
    return true;
}
void NfcPage::display(const NfcDocument &doc) {
    const auto &o=doc.summary;const auto currency=o.value("currency").toString();
    type->setText(NfcDetails::typeName(o));number->setText(field(o,"number"));
    const bool estimate=o.value("estimate").toBool();
    balance->setText(o.contains("balance")?NfcDetails::money(qint32(o.value("balance").toDouble()),currency)+(estimate?QStringLiteral("（估算）"):QString()):QStringLiteral("未取得"));
    physicalId->setText(QStringLiteral("UID / IDm：%1").arg(QString::fromLatin1(QByteArray::fromHex(o.value("uid").toString().toLatin1()).toHex(' ').toUpper())));
    const auto state=o.value("status").toString();
    QString message=state=="ok"?QStringLiteral("公开信息读取完成"):state=="unrecognized"?QStringLiteral("已识别 RF 协议，未识别到可读取的具体应用"):state=="rf_error"?QStringLiteral("通信中断；展示本次已取得的信息"):QStringLiteral("已取得部分公开信息");
    const auto warning=NfcDetails::warningText(o.value("warning").toString());if(!warning.isEmpty())message+=QStringLiteral(" · ")+warning;
    status->setText(message);status->setStyleSheet(state=="ok"?"color:#137044;":"color:#965800;");
    QStringList lines={QStringLiteral("发卡机构：%1    应用标签：%2").arg(field(o,"issuer"),field(o,"label")),
        QStringLiteral("启用日期：%1    有效期：%2").arg(formattedDate(o.value("start").toString()),formattedDate(o.value("expiry").toString())),
        QStringLiteral("协议：0x%1    接口：0x%2").arg(o.value("protocol").toInt(),2,16,QChar('0')).arg(o.value("interface").toInt(),2,16,QChar('0'))};
    if(o.contains("blocks"))lines<<QStringLiteral("存储：%1 个块 × %2 字节    扇区：%3").arg(o.value("blocks").toInt()).arg(o.value("block_size").toInt()).arg(o.value("sectors").toInt());
    if(o.contains("pages"))lines<<QStringLiteral("存储：%1 页    用户区页范围：%2–%3（CC 范围，不代表可写权限）").arg(o.value("pages").toInt()).arg(o.value("first_page").toInt()).arg(o.value("last_page").toInt());
    meta->setText(lines.join('\n'));history->setRowCount(doc.records.size());
    for(qsizetype row=0;row<doc.records.size();row++) {
        const auto &r=doc.records[row];const bool japan=o.value("kind")=="transit_japan";
        const QStringList fields={formattedDate(r.value("date").toString()),QStringLiteral("0x%1").arg(r.value("type").toInt(),2,16,QChar('0')).toUpper(),
            r.contains("amount")?NfcDetails::money(qint32(r.value("amount").toDouble()),currency):QStringLiteral("未取得"),
            r.contains("balance")?NfcDetails::money(qint32(r.value("balance").toDouble()),currency):QStringLiteral("未取得"),
            japan && r.value("entry").toInt()?QStringLiteral("0x%1").arg(r.value("entry").toInt(),4,16,QChar('0')).toUpper():QStringLiteral("—"),
            japan && r.value("exit").toInt()?QStringLiteral("0x%1").arg(r.value("exit").toInt(),4,16,QChar('0')).toUpper():QStringLiteral("—")};
        for(int col=0;col<fields.size();col++){auto item=new QTableWidgetItem(fields[col]);item->setToolTip(r.value("raw").toString());history->setItem(row,col,item);}
    }
}
