#pragma once
#include <QWidget>
#include <QTimer>
#include <QMap>
#include <QVector>
#include "core/ReaderProtocol.h"
class QSpinBox;class QLineEdit;class QPlainTextEdit;class QComboBox;class QCheckBox;class QLabel;class QPushButton;class QGroupBox;
class QTableWidget;class QProgressBar;
class AdvancedPanel:public QWidget {
    Q_OBJECT
public:
    explicit AdvancedPanel(QWidget *parent=nullptr, bool standalone=false);
    void connected(bool, bool scanSupported=false);
    void activated(const QByteArray &);
    void targetFrame(const QByteArray &);
    void response(const QByteArray &);
    void sendFailed(const QString &);
signals:
    void request(QByteArray payload);
    void logMessage(QString);
private:
    advanced_target_t target={}, pendingTarget={};
    bool online=false;QByteArray activeUid;QString operationMessage;
    quint32 pendingId=0;int pendingOperation=0,pendingAddress=0,pendingCount=0;
    QByteArray pendingData;QTimer lease,timeout;
    QWidget *body,*classicOptions,*ndefOptions,*uidOptions;
    QGroupBox *group;QLabel *identity,*message;QSpinBox *address,*count;
    QLineEdit *key,*newUid,*ndefContent;QPlainTextEdit *data,*result;
    QComboBox *keyType,*keyPreset,*ndefType;QCheckBox *trailer,*gen2;
    QPushButton *read,*write,*writeUid,*writeNdef;
    struct Cell { QByteArray bytes; QString status; };
    QMap<int,Cell> cells;
    advanced_target_t snapshotTarget={},scanTarget={};
    bool scanSupported=false,scanning=false;
    QVector<QPair<int,int>> scanQueue;
    QByteArray scanKey;int scanKeyType=0x60;
    QTimer scanWait;
    QTableWidget *table;QLabel *snapshotIdentity,*scanStatus;QProgressBar *progress;
    QPushButton *scan,*stopScan,*retryScan,*clearResult,*copyResult,*exportResult;
    void startScan(bool retry);void nextScan();void haltScan(const QString &);
    void refreshResults();void prepareSnapshot(const advanced_target_t &);
    QString resultText(bool csv=false) const;
    void render();void issue(int operation);void finish(const QString &);
};
