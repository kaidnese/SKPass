#pragma once
#include <QWidget>
#include <QJsonObject>
class SerialReader;
class QLabel;
class QPushButton;
class QProgressBar;
class QDragEnterEvent;
class QDropEvent;
class QDragLeaveEvent;
class QMimeData;
class QTimer;
class QFrame;
class RomDownloadPanel;

class FirmwareUpdatePage : public QWidget {
    Q_OBJECT
public:
    explicit FirmwareUpdatePage(SerialReader *reader, QWidget *parent=nullptr);
    bool updateInProgress() const;
signals:
    void updateFinished(bool success);
    void updateActivityChanged(bool active);
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private slots:
    void showDeviceInformation(QJsonObject info);
private:
    friend class FirmwareUpdatePageTestAccess;
    SerialReader *reader;
    QLabel *status;
    QLabel *dropTitle, *dropDetails, *dropAction;
    QFrame *dropZone;
    QPushButton *enter, *choose;
    QPushButton *queryInfo;
    QLabel *infoStatus, *infoDetails;
    QProgressBar *progress;
    QTimer *requestTimeout;
    bool busy=false, requested=false;
    bool infoQueryPending=false;
    bool dragActive=false;
    bool romBusy=false;
    int selectedMethod=0;
    RomDownloadPanel *rom;
    QString selectedFile;
    void install();
    void installFile(const QString &path);
    void finishUpdate(const QString &error,bool automaticRestart=false);
    void refreshButtons();
    void refreshDropZone();
    void beginDeviceQuery();
    static QString droppedFile(const QMimeData *mime);
};
