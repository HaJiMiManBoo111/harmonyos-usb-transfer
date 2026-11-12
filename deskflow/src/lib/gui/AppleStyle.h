/*
 * Flux -- keyboard, mouse and file sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

/*
 * Apple-inspired design language for the Flux desktop UI.
 * Light, calm surfaces, generous spacing, rounded controls and a single
 * warm accent colour.  Kept as a header-only inline stylesheet so no extra
 * resource or build step is required.
 */

#pragma once

#include <QApplication>
#include <QString>

namespace deskflow::gui {

inline void applyAppleStyle(QApplication &app)
{
  app.setStyleSheet(QStringLiteral(R"FLUXQSS(
/* ---- Base -------------------------------------------------------------- */
QWidget {
  color: #1C1C1E;
  font-family: "Segoe UI", "SF Pro Text", "Helvetica Neue", Arial, sans-serif;
  font-size: 13px;
}
QMainWindow,
QDialog,
#topLevelWidget,
#FileTransferPage,
#tabSettings,
#tabTransfer {
  background-color: #F2F2F7;
}

/* ---- Labels ------------------------------------------------------------ */
QLabel { background: transparent; }
QLabel#lblTitle {
  font-size: 26px;
  font-weight: 700;
  color: #1C1C1E;
  padding: 4px 2px 10px 2px;
}
QLabel[errorState="true"] { color: #FF3B30; }

/* ---- Group boxes as cards --------------------------------------------- */
QGroupBox {
  background-color: #FFFFFF;
  border: 1px solid #E5E5EA;
  border-radius: 14px;
  margin-top: 8px;
  padding: 12px 14px 10px 14px;
  font-weight: 600;
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 14px;
  top: 2px;
  padding: 0 4px;
  color: #1C1C1E;
  background: transparent;
}

/* ---- Buttons ----------------------------------------------------------- */
QPushButton {
  background-color: #FFFFFF;
  border: 1px solid #D1D1D6;
  border-radius: 8px;
  padding: 6px 16px;
  min-height: 20px;
  color: #1C1C1E;
}
QPushButton:hover  { background-color: #F7F7FA; }
QPushButton:pressed{ background-color: #ECECF1; }
QPushButton:disabled { color: #AEAEB2; border-color: #E5E5EA; }
QPushButton:default,
QPushButton#btnSend,
QPushButton#btnSendViaKvm,
QPushButton#btnToggleCore {
  background-color: #FF6B4A;
  border: 1px solid #FF6B4A;
  color: #FFFFFF;
  font-weight: 600;
}
QPushButton:default:hover,
QPushButton#btnSend:hover,
QPushButton#btnSendViaKvm:hover,
QPushButton#btnToggleCore:hover { background-color: #F2603F; border-color: #F2603F; }
QPushButton:default:pressed,
QPushButton#btnSend:pressed,
QPushButton#btnSendViaKvm:pressed,
QPushButton#btnToggleCore:pressed { background-color: #E05536; border-color: #E05536; }
QPushButton:default:disabled,
QPushButton#btnSend:disabled,
QPushButton#btnSendViaKvm:disabled,
QPushButton#btnToggleCore:disabled { background-color: #FFD2C6; border-color: #FFD2C6; color: #FFFFFF; }

/* ---- Mode-row buttons -------------------------------------------------- */
/* Generic 6px/16px padding and 20px min-height clipped the fixed 32px
   mode rows and squeezed the theme icons down to nothing. */
QPushButton#btnConfigureServer,
QPushButton#btnToggleCore { padding: 2px 12px; min-height: 16px; }
QPushButton#btnSaveServerConfig,
QPushButton#btnRestartCore { padding: 0; min-height: 0; background-color: #F2F2F7; }

/* ---- Permanently visible server-log toggle (menu-bar corner) ----------- */
QPushButton#btnToggleServerLog { padding: 2px 10px; min-height: 0; font-size: 12px; border-radius: 6px; }
QPushButton#btnToggleServerLog:checked { background-color: #FFE3DC; border-color: #FF6B4A; color: #E05536; }

/* ---- Line edits & combo boxes ------------------------------------------ */
QLineEdit, QComboBox, QSpinBox, QTextEdit, QPlainTextEdit {
  background-color: #FFFFFF;
  border: 1px solid #D1D1D6;
  border-radius: 8px;
  padding: 6px 10px;
  selection-background-color: #FF6B4A;
  selection-color: #FFFFFF;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus,
QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid #FF6B4A; }
QLineEdit[errorState="true"] { border: 1px solid #FF3B30; border-radius: 8px; padding: 6px 10px; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView {
  background: #FFFFFF;
  border: 1px solid #D1D1D6;
  border-radius: 8px;
  selection-background-color: #FFE3DC;
  selection-color: #1C1C1E;
}
QPlainTextEdit#textLog {
  font-family: "Cascadia Mono", "SF Mono", Consolas, monospace;
  font-size: 12px;
  background-color: #FAFAFC;
}

/* ---- Lists & trees ----------------------------------------------------- */
QTreeWidget, QListWidget, QTableView {
  background-color: #FFFFFF;
  border: 1px solid #E5E5EA;
  border-radius: 10px;
  padding: 4px;
  alternate-background-color: #FAFAFC;
}
QTreeWidget::item, QListWidget::item { padding: 5px 6px; border-radius: 6px; }
QTreeWidget::item:selected, QListWidget::item:selected { background: #FFE3DC; color: #1C1C1E; }
QHeaderView::section {
  background: #FAFAFC;
  border: 0;
  border-bottom: 1px solid #E5E5EA;
  padding: 6px 8px;
  color: #6E6E73;
  font-weight: 600;
}

/* ---- Tabs -------------------------------------------------------------- */
QTabWidget::pane {
  background: #FFFFFF;
  border: 1px solid #E5E5EA;
  border-radius: 14px;
  top: -1px;
  padding: 8px;
}
QTabBar::tab {
  background: transparent;
  color: #8E8E93;
  padding: 9px 22px;
  margin-right: 4px;
  border-radius: 8px;
  font-weight: 600;
}
QTabBar::tab:selected { background: #FFE3DC; color: #E05536; }
QTabBar::tab:hover:!selected { background: #F2F2F7; }

/* ---- Menu bar & menus -------------------------------------------------- */
QMenuBar, QMenuBar::item { background: #F2F2F7; color: #1C1C1E; }
QMenuBar::item:selected { background: #FFE3DC; border-radius: 6px; }
QMenu {
  background: #FFFFFF;
  border: 1px solid #E5E5EA;
  border-radius: 10px;
  padding: 6px;
}
QMenu::item { padding: 6px 24px; border-radius: 6px; }
QMenu::item:selected { background: #FFE3DC; color: #1C1C1E; }
QMenu::separator { height: 1px; background: #E5E5EA; margin: 4px 8px; }
QDockWidget { background: #FFFFFF; border: 1px solid #E5E5EA; border-radius: 12px; }
QDockWidget::title { background: #FAFAFC; padding: 6px 10px; border-radius: 10px; }

/* ---- Progress ---------------------------------------------------------- */
QProgressBar {
  background: #E5E5EA;
  border: 0;
  border-radius: 6px;
  height: 10px;
  text-align: center;
  color: transparent;
}
QProgressBar::chunk { background: #FF6B4A; border-radius: 6px; }

/* ---- Radio / check / group -------------------------------------------- */
QRadioButton, QCheckBox { spacing: 8px; background: transparent; }
QRadioButton::indicator, QCheckBox::indicator { width: 16px; height: 16px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #C7C7CC; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #AEAEB2; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QSplitter::handle { background: #E5E5EA; border-radius: 3px; }
QSplitter#splitter::handle { width: 6px; }
QSplitter#mainSplitter::handle { height: 8px; }
QSplitter::handle:hover { background: #FF6B4A; }
QSplitter::handle:pressed { background: #E05536; }
QStatusBar { background: transparent; color: #8E8E93; }
QToolTip { background: #1C1C1E; color: #FFFFFF; border: 0; padding: 5px 8px; border-radius: 6px; }
)FLUXQSS"));
}

} // namespace deskflow::gui
