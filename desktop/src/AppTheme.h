#pragma once
#include <QApplication>
#include <QFont>
#include <QPalette>

inline void applyAppTheme(QApplication &app) {
    app.setStyle("Fusion");
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"),10));
    QPalette palette;
    palette.setColor(QPalette::Window,QColor("#f3f6fb"));
    palette.setColor(QPalette::WindowText,QColor("#17263c"));
    palette.setColor(QPalette::Base,Qt::white);
    palette.setColor(QPalette::AlternateBase,QColor("#f6f9fd"));
    palette.setColor(QPalette::Text,QColor("#17263c"));
    palette.setColor(QPalette::Button,Qt::white);
    palette.setColor(QPalette::ButtonText,QColor("#17263c"));
    palette.setColor(QPalette::Highlight,QColor("#2563eb"));
    palette.setColor(QPalette::HighlightedText,Qt::white);
    palette.setColor(QPalette::PlaceholderText,QColor("#64748b"));
    palette.setColor(QPalette::ToolTipBase,Qt::white);
    palette.setColor(QPalette::ToolTipText,QColor("#17263c"));
    palette.setColor(QPalette::Disabled,QPalette::Text,QColor("#8593a5"));
    palette.setColor(QPalette::Disabled,QPalette::ButtonText,QColor("#8593a5"));
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QWidget { color: #17263c; }
        QMainWindow, QDialog { background: #f3f6fb; }
        QLabel { background: transparent; }
        QLabel[role="pageTitle"] { font-size: 23px; font-weight: 700; }
        QLabel[role="muted"] { color: #52647a; }
        QGroupBox { background: white; border: 1px solid #dce4ef; border-radius: 12px;
                    margin-top: 16px; padding: 22px 14px 14px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 16px; top: 5px; padding: 0 6px; color: #243952; }
        QPushButton { background: white; color: #243952; border: 1px solid #cbd6e5;
                      border-radius: 7px; padding: 8px 13px; min-height: 20px; }
        QPushButton:hover { background: #edf4ff; border-color: #7fa7ed; }
        QPushButton:pressed { background: #dceafe; }
        QPushButton:focus { border: 1px solid #2563eb; }
        QPushButton:disabled { color: #8593a5; background: #edf1f6; border-color: #dce4ef; }
        QPushButton[role="primary"] { background: #2563eb; color: white; border-color: #2563eb; font-weight: 600; }
        QPushButton[role="primary"]:hover { background: #1d4ed8; }
        QPushButton[role="primary"]:disabled { background: #e0e7f2; color: #73839a; border-color: #dce4ef; }
        QLineEdit, QComboBox { background: white; color: #17263c; border: 1px solid #cbd6e5;
                              border-radius: 7px; padding: 8px 10px; min-height: 20px; }
        QLineEdit:focus, QComboBox:focus { border-color: #2563eb; }
        QLineEdit:read-only { background: #f1f5f9; }
        QComboBox QAbstractItemView { background: white; color: #17263c; selection-background-color: #2563eb; selection-color: white; }
        QTableWidget { background: white; alternate-background-color: #f6f9fd; color: #17263c;
                       border: 1px solid #dce4ef; border-radius: 8px; gridline-color: #edf1f6;
                       selection-background-color: #dbeafe; selection-color: #122c52; }
        QTableWidget::item { padding: 7px; }
        QTableWidget::item:selected { background: #dbeafe; color: #122c52; }
        QHeaderView::section { background: #edf2f9; color: #334b67; font-weight: 600;
                               padding: 9px 12px; border: none; border-bottom: 1px solid #dce4ef; }
        QPlainTextEdit { background: white; color: #243952; border: 1px solid #dce4ef; border-radius: 8px; padding: 10px; }
        QScrollArea { background: transparent; }
        QScrollBar:vertical { background: #edf1f6; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: #b9c8db; border-radius: 5px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
        QCheckBox { spacing: 7px; }
    )");
}
