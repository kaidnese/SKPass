#pragma once
#include <QWidget>
#include <QStringList>
#include <QHash>
#include <QSet>
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QPushButton;
class QTimer;
class CardManager : public QWidget {
public:
    explicit CardManager(QWidget *parent=nullptr);
    void record(const QString &code);
    void saveNumber(const QString &code);
    QWidget *createHistoryView(QWidget *parent=nullptr);
    QString jsonPath() const {return path;}
    bool exportToGame(const QString &directory);
private:
    QPushButton *keyButton;
    unsigned keyVk=13;
    QList<QTableWidget*> historyTables;
    QSet<QString> checkedHistory;
    QTableWidget *savedTable;
    QPushButton *deleteSaved;
    QLineEdit *manual;
    QPushButton *saveManual;
    QTimer *saveNoticeTimer;
    QLabel *status,*pathLabel,*validation,*saveStatus;
    QStringList history,cards;
    QHash<QString,QString> historyTimes;
    QHash<QString,QString> notes;
    QString active,path;
    bool loadValid=true;
    void load();
    bool persist();
    void refresh();
    static bool valid(const QString &code,QString *error);
    void validateInput(const QString &text);
    void saveNumbers(const QStringList &numbers);
    void deleteSelectedCard();
    void refreshHistory();
    void chooseExportDirectory();
};
