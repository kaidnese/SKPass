#pragma once
#include <QMainWindow>
#include <QHash>
#include <QStringList>
#include "SerialReader.h"
class QComboBox;
class QLabel;
class QCheckBox;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class CardManager;
class QFrame;
class QTimer;
class QCloseEvent;
class FirmwareUpdatePage;
class NfcPage;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    void connectAutomatically() {reader.open("auto");}
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend class MainWindowTestAccess;
    SerialReader reader;
    CardManager *cardManager;
    FirmwareUpdatePage *firmwarePage;
    NfcPage *nfcPage;
    QHash<QByteArray,QStringList> decodedCards;
    QComboBox *ports;
    QLabel *codeLabel,*details,*errors,*connection;
    QLabel *uidLabel,*previewHint;
    QPushButton *copyCodeButton;
    QTimer *previewTimer;
    QByteArray previewUid;
    QStringList previewResult;
    QLabel *connectionDetail,*connectionIndicator;
    QFrame *connectionPanel;
    int nfcMode=0;
    QCheckBox *mask;
    QCheckBox *writeSegaKey;
    QPlainTextEdit *rawPanel,*logPanel;
    QLineEdit *serial;
    QLineEdit *writeCode,*writeKey;
    QComboBox *keyPreset;
    QPushButton *writeButton;
    QLabel *writeStatus;
    QByteArray targetUid,pendingUid;
    QString pendingCode;
    quint32 writeId=0;
    QHash<QString,QByteArray> idBlocks;
    QString code;
    QList<QStringList> rows;
    void refreshPorts();
    void receive(quint8 msg,const QByteArray &payload,const QByteArray &raw);
    void showCode(const QString &value,const QString &source,const QString &issuer,
                  const QString &type,const QString &message);
    void renderCode();
    void renderUid(const QByteArray &uid,const QString &label=QStringLiteral("UID"));
    void clearPreview();
    void log(const QString &text);
    void exportCsv();
    void exportLog();
    void updateWriter();
    void startWrite();
private slots:
    void renderConnection(SerialReader::ConnectionState state,const QString &detail);
};
