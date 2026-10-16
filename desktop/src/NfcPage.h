#pragma once
#include <QWidget>
#include "NfcDetails.h"
class QLabel;
class QTableWidget;
class AdvancedPanel;
class CardEmulationPanel;
class NfcPage : public QWidget {
public:
    explicit NfcPage(QWidget *parent=nullptr);
    bool receive(const QByteArray &payload, QString *error=nullptr);
    void activated(const QByteArray &uid);
    void resetSession();
    AdvancedPanel *advanced;
    CardEmulationPanel *emulation;
private:
    NfcDetails assembler;
    NfcDocument current;
    QLabel *type,*number,*physicalId,*balance,*status,*meta;
    QTableWidget *history;
    void display(const NfcDocument &doc);
};
