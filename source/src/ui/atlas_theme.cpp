#include "ui/atlas_theme.h"

#include <QApplication>
#include <QPalette>

namespace atlas {

void AtlasTheme::apply(QApplication &application)
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#0e1117")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e7edf5")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#151a23")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#1b222d")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#202a36")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e7edf5")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e7edf5")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#202936")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e7edf5")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2bc7b5")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#071110")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#56d7c8")));
    application.setPalette(palette);
    application.setStyleSheet(styleSheet());
}

QString AtlasTheme::styleSheet()
{
    return QStringLiteral(R"QSS(
        * { font-family: "Segoe UI", "Arial"; font-size: 10pt; }
        QMainWindow { background: #0e1117; }
        QWidget { color: #e7edf5; }
        QScrollArea { background: #0e1117; border: 0; }
        QScrollArea > QWidget > QWidget { background: #0e1117; }
        QFrame#sidebar { background: #11161e; border-right: 1px solid #242e3a; }
        QFrame#topBar { background: #121821; border-bottom: 1px solid #242e3a; }
        QLabel#brand { color: #f3f7fb; font-size: 18pt; font-weight: 700; }
        QLabel#brandMark { color: #2bc7b5; font-size: 20pt; font-weight: 700; }
        QLabel#pageTitle { color: #f3f7fb; font-size: 20pt; font-weight: 700; }
        QLabel#pageSubtitle { color: #8998aa; font-size: 10pt; }
        QLabel#muted { color: #8998aa; }
        QLabel#accent { color: #56d7c8; font-weight: 600; }
        QLabel#heroVersion { color: #9eacbd; font-size: 10pt; }
        QPushButton { background: #202936; border: 1px solid #2d3948; border-radius: 8px; padding: 9px 14px; color: #e7edf5; }
        QPushButton:hover { background: #2b3746; border-color: #3c4d61; }
        QPushButton:pressed { background: #18212b; }
        QPushButton:disabled { color: #5e6a79; background: #171d25; border-color: #202833; }
        QPushButton#navButton { text-align: left; background: transparent; border: 0; color: #94a3b5; padding: 11px 14px; border-radius: 7px; }
        QPushButton#navButton:hover { color: #e7edf5; background: #1b2530; }
        QPushButton#navButton[active="true"] { color: #eafffb; background: #163b3a; border-left: 3px solid #2bc7b5; }
        QPushButton#primaryButton { background: #2bc7b5; color: #071110; border: 0; font-weight: 700; padding: 11px 20px; border-radius: 8px; }
        QPushButton#primaryButton:hover { background: #5ee0d1; }
        QPushButton#primaryButton:disabled { background: #1b292d; color: #71818b; border: 1px solid #294147; }
        QPushButton#dangerButton { background: #41232b; border-color: #653440; color: #ffb5c1; }
        QFrame#card { background: #151b24; border: 1px solid #242f3c; border-radius: 12px; }
        QFrame#heroCard { background: #17252c; border: 1px solid #29505a; border-radius: 14px; }
        QFrame#statCard { background: #151b24; border: 1px solid #242f3c; border-radius: 10px; }
        QLabel#cardTitle { color: #dfe8f1; font-size: 11pt; font-weight: 600; }
        QLabel#statValue { color: #f3f7fb; font-size: 17pt; font-weight: 700; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit { background: #10151d; border: 1px solid #2b3745; border-radius: 7px; padding: 8px; color: #e7edf5; selection-background-color: #2bc7b5; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border-color: #2bc7b5; }
        QComboBox QAbstractItemView { background: #161e28; color: #e7edf5; selection-background-color: #246e68; }
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #3b4a5b; border-radius: 4px; background: #10151d; }
        QCheckBox::indicator:checked { background: #2bc7b5; border-color: #2bc7b5; }
        QProgressBar { background: #0e141b; border: 0; border-radius: 4px; height: 8px; text-align: center; color: transparent; }
        QProgressBar::chunk { background: #2bc7b5; border-radius: 4px; }
        QListWidget, QTreeWidget, QTableWidget { background: #111720; border: 1px solid #263241; border-radius: 8px; outline: 0; }
        QListWidget::item, QTreeWidget::item, QTableWidget::item { padding: 8px; border-bottom: 1px solid #1c2530; }
        QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected { background: #1b504d; color: #f1fffd; }
        QHeaderView::section { background: #1b232e; color: #9eacbd; border: 0; padding: 8px; }
        QScrollBar:vertical { background: #10151d; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #354354; border-radius: 5px; min-height: 25px; }
        QScrollBar::handle:vertical:hover { background: #4a5b70; }
        QToolTip { background: #202936; color: #e7edf5; border: 1px solid #3d4d60; padding: 5px; }
        QStatusBar { background: #11161e; color: #8493a5; border-top: 1px solid #242e3a; }
    )QSS");
}

} // namespace atlas
