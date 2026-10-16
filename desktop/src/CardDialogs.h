#pragma once
#include <QApplication>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>

inline bool supportedCardKey(unsigned key) {
    return key==8 || key==9 || key==13 || key==32 ||
        (key>=0x21 && key<=0x28) || key==0x2D || key==0x2E ||
        (key>=0x30 && key<=0x39) || (key>=0x41 && key<=0x5A) ||
        (key>=0x60 && key<=0x6F && key!=0x6C) || (key>=0x70 && key<=0x87) ||
        (key>=0xBA && key<=0xC0) || (key>=0xDB && key<=0xDF) || key==0xE2;
}
inline QString cardKeyName(unsigned key) {
    if(key>=0x41 && key<=0x5A)return QString(QChar(key));
    if(key>=0x30 && key<=0x39)return QString(QChar(key));
    if(key>=0x70 && key<=0x87)return QString("F%1").arg(key-0x70+1);
    if(key>=0x60 && key<=0x69)return QStringLiteral("小键盘 %1").arg(key-0x60);
    switch(key) {
    case 8:return "Backspace";case 9:return "Tab";case 13:return "Enter";case 32:return "Space";
    case 0x21:return "Page Up";case 0x22:return "Page Down";case 0x23:return "End";case 0x24:return "Home";
    case 0x25:return QStringLiteral("← 左");case 0x26:return QStringLiteral("↑ 上");case 0x27:return QStringLiteral("→ 右");case 0x28:return QStringLiteral("↓ 下");
    case 0x2D:return "Insert";case 0x2E:return "Delete";
    case 0x6A:return "Num *";case 0x6B:return "Num +";case 0x6D:return "Num -";case 0x6E:return "Num .";case 0x6F:return "Num /";
    case 0xBA:return ";";case 0xBB:return "=";case 0xBC:return ",";case 0xBD:return "-";case 0xBE:return ".";case 0xBF:return "/";case 0xC0:return "`";
    case 0xDB:return "[";case 0xDC:return "\\";case 0xDD:return "]";case 0xDE:return "'";
    default:return QString("VK 0x%1").arg(key,2,16,QChar('0'));
    }
}

inline QVBoxLayout *cardDialogLayout(QDialog *dialog,const QString &badge,const QString &title,const QString &subtitle) {
    dialog->setWindowFlags(Qt::Dialog|Qt::FramelessWindowHint);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->setFixedWidth(480);
    auto outer=new QVBoxLayout(dialog);outer->setContentsMargins(0,0,0,0);
    auto panel=new QFrame;panel->setObjectName("dialogCard");
    panel->setStyleSheet("QFrame#dialogCard{background:white;border:1px solid #cfdaea;border-radius:16px;}");outer->addWidget(panel);
    auto layout=new QVBoxLayout(panel);layout->setContentsMargins(28,26,28,24);layout->setSpacing(16);
    auto top=new QHBoxLayout;auto icon=new QLabel(badge);icon->setFixedSize(48,48);icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("background:#e9f3ff;color:#2563eb;border-radius:12px;font-size:25px;font-weight:700;");top->addWidget(icon);top->addStretch();
    auto close=new QPushButton(QStringLiteral("×"));close->setFixedSize(32,32);close->setToolTip(QStringLiteral("关闭（Esc）"));close->setStyleSheet("border:0;background:#f1f5f9;padding:0;font-size:20px;");
    QObject::connect(close,&QPushButton::clicked,dialog,&QDialog::reject);top->addWidget(close,0,Qt::AlignTop);layout->addLayout(top);
    auto heading=new QLabel(title);heading->setStyleSheet("font-size:22px;font-weight:700;color:#17263c;");layout->addWidget(heading);
    auto description=new QLabel(subtitle);description->setWordWrap(true);description->setStyleSheet("color:#52647a;");layout->addWidget(description);
    return layout;
}

class CardSavedDialog : public QDialog {
public:
    CardSavedDialog(QWidget *parent,int total,int added,const QString &path):QDialog(parent) {
        setWindowTitle(QStringLiteral("保存成功"));
        auto layout=cardDialogLayout(this,QStringLiteral("✓"),QStringLiteral("卡号已保存"),QStringLiteral("JSON文件已更新，保存的卡片可在列表中选择和添加备注。"));
        auto summary=new QLabel(QStringLiteral("新增 %1 张  ·  已有 %2 张  ·  共 %3 张").arg(added).arg(total-added).arg(total));
        summary->setStyleSheet("background:#edf8f0;color:#17643a;border-radius:8px;padding:12px;");layout->addWidget(summary);
        auto file=new QLabel(path);file->setWordWrap(true);file->setTextFormat(Qt::PlainText);file->setTextInteractionFlags(Qt::TextSelectableByMouse);file->setStyleSheet("color:#52647a;font-size:12px;");layout->addWidget(file);
        auto done=new QPushButton(QStringLiteral("完成"));done->setProperty("role","primary");done->setDefault(true);QObject::connect(done,&QPushButton::clicked,this,&QDialog::accept);layout->addWidget(done);
    }
};

class CardKeyDialog : public QDialog {
public:
    unsigned chosen=0;
    explicit CardKeyDialog(QWidget *parent,unsigned current):QDialog(parent) {
        setWindowTitle(QStringLiteral("修改刷卡键"));
        auto layout=cardDialogLayout(this,QStringLiteral("⌨"),QStringLiteral("按下新的刷卡键"),QStringLiteral("按一个键即可设置。游戏内按住该键呈现卡片，松开移卡。"));
        auto key=new QLabel(QStringLiteral("当前键位  %1").arg(cardKeyName(current)));key->setAlignment(Qt::AlignCenter);key->setStyleSheet("background:#edf4ff;color:#2563eb;border-radius:10px;padding:20px;font-size:20px;font-weight:600;");layout->addWidget(key);
        hint=new QLabel(QStringLiteral("支持字母、数字、功能键、方向键和常用符号。\nEsc取消；不支持组合键。"));hint->setWordWrap(true);hint->setStyleSheet("color:#52647a;");layout->addWidget(hint);
        auto cancel=new QPushButton(QStringLiteral("取消 · Esc"));QObject::connect(cancel,&QPushButton::clicked,this,&QDialog::reject);layout->addWidget(cancel);
        qApp->installEventFilter(this);
    }
    ~CardKeyDialog() override {qApp->removeEventFilter(this);}
protected:
    bool eventFilter(QObject *object,QEvent *event) override {
        auto widget=qobject_cast<QWidget*>(object);
        if(!isVisible() || !widget || (widget!=this && !isAncestorOf(widget)))return QDialog::eventFilter(object,event);
        if(event->type()==QEvent::ShortcutOverride){event->accept();return true;}
        if(event->type()!=QEvent::KeyPress)return QDialog::eventFilter(object,event);
        auto key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape){reject();return true;}
        if(key->isAutoRepeat())return true;
        if(key->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier|Qt::ShiftModifier)) {
            hint->setText(QStringLiteral("请松开Ctrl、Alt、Shift或Win后按一个键。\nEsc取消，原键位不变。"));return true;
        }
        unsigned vk=key->nativeVirtualKey();
        if(!supportedCardKey(vk)){hint->setText(QStringLiteral("这个键暂不支持，请按字母、数字或功能键。\nEsc取消，原键位不变。"));return true;}
        chosen=vk;accept();return true;
    }
private:
    QLabel *hint;
};
