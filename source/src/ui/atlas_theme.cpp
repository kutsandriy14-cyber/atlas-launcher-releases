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
        QFrame#sidebar { background: #111821; border-right: 1px solid #283442; }
        QFrame#topBar { background: #121a24; border-bottom: 1px solid #283442; }
        QLabel#brand { color: #f5f8fc; font-size: 18pt; font-weight: 700; }
        QLabel#brandMark { color: #34d4c0; font-size: 20pt; font-weight: 700; }
        QLabel#pageTitle { color: #f5f8fc; font-size: 20pt; font-weight: 700; }
        QLabel#pageSubtitle { color: #a5b3c3; font-size: 10pt; }
        QLabel#muted { color: #9baabd; }
        QLabel#accent { color: #65decf; font-weight: 700; }
        QLabel#heroVersion { color: #b3c0cf; font-size: 10pt; }
        QPushButton { background: #202b38; border: 1px solid #354457; border-radius: 9px; padding: 9px 14px; color: #eaf0f7; }
        QPushButton:hover { background: #2b3949; border-color: #52677f; }
        QPushButton:pressed { background: #17212b; }
        QPushButton:focus { border: 1px solid #65decf; }
        QPushButton:disabled { color: #657487; background: #171e27; border-color: #27313e; }
        QPushButton#navButton { text-align: left; background: transparent; border: 0; color: #9cabbc; padding: 11px 14px; border-radius: 7px; }
        QPushButton#navButton:hover { color: #f1f6fb; background: #1b2734; }
        QPushButton#navButton[active="true"] { color: #ecfffc; background: #174541; border-left: 3px solid #34d4c0; }
        QPushButton#primaryButton { background: #34d4c0; color: #071513; border: 1px solid #34d4c0; font-weight: 700; padding: 11px 20px; border-radius: 9px; }
        QPushButton#primaryButton:hover { background: #65e3d3; border-color: #65e3d3; }
        QPushButton#primaryButton:focus { border: 2px solid #c7fff6; }
        QPushButton#primaryButton:disabled { background: #1b302f; color: #79938f; border: 1px solid #2c4b48; }
        QPushButton#dangerButton { background: #482630; border-color: #75404c; color: #ffbdc8; }
        QFrame#card { background: #17202b; border: 1px solid #2a394a; border-radius: 12px; }
        QFrame#heroCard { background: #172b32; border: 1px solid #34706f; border-radius: 14px; }
        QFrame#statCard { background: #17202b; border: 1px solid #2a394a; border-radius: 10px; }
        QLabel#cardTitle { color: #e6eef7; font-size: 11pt; font-weight: 700; }
        QLabel#statValue { color: #f7faff; font-size: 17pt; font-weight: 700; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit { background: #0f161f; border: 1px solid #334458; border-radius: 8px; padding: 8px; color: #eaf0f7; selection-background-color: #2d9d92; }
        QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QPlainTextEdit:hover { border-color: #4d6279; }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border: 2px solid #34d4c0; padding: 7px; }
        QComboBox QAbstractItemView { background: #17202b; color: #eaf0f7; selection-background-color: #246e68; }
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #4b6075; border-radius: 4px; background: #0f161f; }
        QCheckBox::indicator:hover { border-color: #65decf; }
        QCheckBox::indicator:checked { background: #34d4c0; border-color: #34d4c0; }
        QProgressBar { background: #0d141c; border: 1px solid #263746; border-radius: 5px; height: 8px; text-align: center; color: transparent; }
        QProgressBar::chunk { background: #34d4c0; border-radius: 4px; }
        QListWidget, QTreeWidget, QTableWidget { background: #111923; border: 1px solid #2d3d50; border-radius: 9px; outline: 0; }
        QListWidget::item, QTreeWidget::item, QTableWidget::item { padding: 9px; border-bottom: 1px solid #202c39; }
        QListWidget::item:hover, QTreeWidget::item:hover, QTableWidget::item:hover { background: #1b2835; }
        QListWidget::item:selected, QTreeWidget::item:selected, QTableWidget::item:selected { background: #1e625b; color: #f1fffd; }
        QHeaderView::section { background: #1b2734; color: #b3c0cf; border: 0; padding: 8px; }
        QScrollBar:vertical { background: #0f161f; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #405368; border-radius: 5px; min-height: 25px; }
        QScrollBar::handle:vertical:hover { background: #5a7188; }
        QToolTip { background: #202c39; color: #eaf0f7; border: 1px solid #4b6178; padding: 6px; }
        QStatusBar { background: #111821; color: #9baabd; border-top: 1px solid #283442; }
    )QSS");
}

} // namespace atlas
