#pragma once
#include <QWidget>
#include <QTimer>
class QLabel;class QLineEdit;class QComboBox;class QCheckBox;class QPlainTextEdit;class QTableWidget;class QPushButton;
class CardEmulationPanel:public QWidget {
    Q_OBJECT
public:
    explicit CardEmulationPanel(QWidget *parent=nullptr);
    void connected(bool available);
    void response(const QByteArray &);
    void event(const QByteArray &);
    void sendFailed(const QString &);
signals:
    void request(QByteArray);
    void modeChanged(int mode);
    void logMessage(QString);
private:
    QLabel *state,*validation,*received;
    QLineEdit *uid;QComboBox *kind,*fallback;QCheckBox *writable;
    QPlainTextEdit *content,*capture;QTableWidget *rules;
    QPushButton *apply,*readerMode,*query;
    bool online=false;quint32 nextId=1,pending=0;int mode=0;
    QTimer timeout;
    void issue(int action);void render();
};
