#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
class SerialReader;
class QComboBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QTimer;
class RomDownloadPanel : public QWidget {
    Q_OBJECT
public:
    explicit RomDownloadPanel(SerialReader *reader,QWidget *parent=nullptr);
    void setExternalBusy(bool busy);
    bool isBusy() const {return busy || requested;}
    void selectFirmware(const QString &path);
signals:
    void busyChanged(bool busy);
private:
    friend class RomDownloadPanelTestAccess;
    SerialReader *reader;
    QComboBox *transport,*port,*mode;
    QLabel *file,*status;
    QPushButton *enter,*choose,*flash,*scan;
    QProgressBar *progress;
    QPlainTextEdit *log;
    QTimer *requestTimer,*portTimer;
    bool busy=false,requested=false,externalBusy=false,waitingForReader=false;
    QString selectedFile;
    QStringList previousNativePorts;
    void refresh();
    void refreshPorts();
    void requestEntry();
    void startFlash();
    void startOperation();
    void complete(const QString &error,bool committed);
};
