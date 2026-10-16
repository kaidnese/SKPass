#pragma once
#include <QWidget>
class CardManager;
class QLineEdit;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class SegatoolsEditor : public QWidget {
public:
    explicit SegatoolsEditor(CardManager *manager,QWidget *parent=nullptr);
private:
    CardManager *manager;
    QLineEdit *directory,*dllName;
    QComboBox *architecture;
    QLabel *status;
    QPlainTextEdit *preview;
    QByteArray original,modified;
    QString iniPath;
    bool prepare();
    void apply();
    void restore();
};
