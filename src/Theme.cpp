#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

namespace mviewer::ui
{

namespace
{

QPalette buildDarkPalette()
{
    QPalette pal;
    const QColor window(30, 30, 32);
    const QColor windowText(228, 228, 231);
    const QColor base(20, 20, 22);
    const QColor alternateBase(28, 28, 32);
    const QColor toolTipBase(39, 39, 42);
    const QColor toolTipText(244, 244, 245);
    const QColor text(228, 228, 231);
    const QColor button(39, 39, 42);
    const QColor buttonText(228, 228, 231);
    const QColor brightText(255, 255, 255);
    const QColor link(59, 130, 246);
    const QColor highlight(37, 99, 235);
    const QColor highlightedText(255, 255, 255);
    const QColor mid(56, 56, 62);
    const QColor midlight(48, 48, 54);
    const QColor dark(18, 18, 20);
    const QColor shadow(10, 10, 12);

    for (const auto group : {QPalette::Active, QPalette::Inactive})
    {
        pal.setColor(group, QPalette::Window, window);
        pal.setColor(group, QPalette::WindowText, windowText);
        pal.setColor(group, QPalette::Base, base);
        pal.setColor(group, QPalette::AlternateBase, alternateBase);
        pal.setColor(group, QPalette::ToolTipBase, toolTipBase);
        pal.setColor(group, QPalette::ToolTipText, toolTipText);
        pal.setColor(group, QPalette::Text, text);
        pal.setColor(group, QPalette::Button, button);
        pal.setColor(group, QPalette::ButtonText, buttonText);
        pal.setColor(group, QPalette::BrightText, brightText);
        pal.setColor(group, QPalette::Link, link);
        pal.setColor(group, QPalette::Highlight, highlight);
        pal.setColor(group, QPalette::HighlightedText, highlightedText);
        pal.setColor(group, QPalette::Mid, mid);
        pal.setColor(group, QPalette::Midlight, midlight);
        pal.setColor(group, QPalette::Dark, dark);
        pal.setColor(group, QPalette::Shadow, shadow);
    }

    const QColor disabledText(113, 113, 122);
    const QColor disabledButton(32, 32, 36);
    pal.setColor(QPalette::Disabled, QPalette::Window, window);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Base, base);
    pal.setColor(QPalette::Disabled, QPalette::AlternateBase, alternateBase);
    pal.setColor(QPalette::Disabled, QPalette::ToolTipBase, toolTipBase);
    pal.setColor(QPalette::Disabled, QPalette::ToolTipText, toolTipText);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Button, disabledButton);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(63, 63, 70));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    pal.setColor(QPalette::Disabled, QPalette::Mid, mid);
    pal.setColor(QPalette::Disabled, QPalette::Dark, dark);

    return pal;
}

constexpr const char s_darkStyleSheet[] = R"(
        QScrollBar:vertical {
            background: #18181b;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #3f3f46;
            min-height: 24px;
            border-radius: 4px;
            margin: 2px;
        }
        QScrollBar::handle:vertical:hover {
            background: #52525b;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
            border: none;
            background: none;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
        QScrollBar:horizontal {
            background: #18181b;
            height: 10px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal {
            background: #3f3f46;
            min-width: 24px;
            border-radius: 4px;
            margin: 2px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #52525b;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
            border: none;
            background: none;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: none;
        }
        QMenuBar {
            background: #1e1e20;
            color: #e4e4e7;
            border-bottom: 1px solid #2d2d32;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 8px;
        }
        QMenuBar::item:selected {
            background: #2f3542;
            border-radius: 4px;
        }
        QMenu {
            background: #222226;
            color: #e4e4e7;
            border: 1px solid #38383e;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            padding: 5px 24px 5px 20px;
            border-radius: 3px;
        }
        QMenu::item:selected {
            background: #2563eb;
            color: #ffffff;
        }
        QMenu::separator {
            height: 1px;
            background: #38383e;
            margin: 4px 6px;
        }
        QToolBar {
            background: #1e1e20;
            border: none;
            spacing: 3px;
            padding: 2px 4px;
        }
        QToolButton {
            border: 1px solid transparent;
            border-radius: 4px;
            padding: 3px;
            color: #e4e4e7;
        }
        QToolButton:hover {
            background: #2d2d34;
            border: 1px solid #3f3f46;
        }
        QToolButton:pressed {
            background: #2563eb;
            color: #ffffff;
        }
        QToolButton:checked {
            background: #1e3a8a;
            border: 1px solid #3b82f6;
        }
        QLineEdit, QComboBox, QSpinBox {
            background: #141416;
            border: 1px solid #38383e;
            border-radius: 4px;
            padding: 3px 6px;
            color: #e4e4e7;
            selection-background-color: #2563eb;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #3b82f6;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QTabWidget::pane {
            border: 1px solid #38383e;
            background: #1e1e20;
            border-radius: 4px;
        }
        QTabBar::tab {
            background: #27272a;
            color: #a1a1aa;
            border: 1px solid #38383e;
            padding: 6px 14px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background: #1e1e20;
            color: #ffffff;
            border-bottom-color: #1e1e20;
            font-weight: bold;
        }
        QTabBar::tab:hover:!selected {
            background: #2d2d32;
            color: #e4e4e7;
        }
        QSplitter::handle {
            background: #27272a;
        }
        QSplitter::handle:hover {
            background: #3b82f6;
        }
        QStatusBar {
            background: #18181b;
            color: #a1a1aa;
            border-top: 1px solid #27272a;
        }
        QStatusBar::item {
            border: none;
        }
        QToolTip {
            background: #27272a;
            color: #f4f4f5;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QPushButton {
            background: #27272a;
            color: #e4e4e7;
            border: 1px solid #38383e;
            border-radius: 4px;
            padding: 4px 12px;
            min-height: 20px;
        }
        QPushButton:hover {
            background: #323238;
            border: 1px solid #4a4a54;
        }
        QPushButton:pressed {
            background: #2563eb;
            color: #ffffff;
        }
        QPushButton:disabled {
            background: #202024;
            color: #71717a;
            border-color: #2e2e34;
        }
        QGroupBox {
            border: 1px solid #38383e;
            border-radius: 6px;
            margin-top: 12px;
            padding-top: 10px;
            font-weight: bold;
            color: #e4e4e7;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 4px;
            left: 8px;
        }
    )";

static ThemeMode s_currentTheme = ThemeMode::Dark;

} // namespace

void Theme::initTheme()
{
    QSettings settings;
    const QString mode = settings.value(QStringLiteral("uiTheme"), QStringLiteral("dark")).toString();
    s_currentTheme = (mode.toLower() == QStringLiteral("system")) ? ThemeMode::System : ThemeMode::Dark;
    applyTheme(s_currentTheme);
}

void Theme::applyTheme(ThemeMode mode)
{
    s_currentTheme = mode;
    if (mode == ThemeMode::Dark)
    {
        if (auto *style = QStyleFactory::create(QStringLiteral("Fusion")))
            QApplication::setStyle(style);
        QApplication::setPalette(buildDarkPalette());
        if (qApp)
            qApp->setStyleSheet(QString::fromUtf8(s_darkStyleSheet));
    }
    else
    {
        if (auto *style = QStyleFactory::create(QStringLiteral("windowsvista")))
            QApplication::setStyle(style);
        else if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            QApplication::setStyle(fusion);
        if (qApp && qApp->style())
            QApplication::setPalette(qApp->style()->standardPalette());
        if (qApp)
            qApp->setStyleSheet(QString());
    }

    QSettings settings;
    settings.setValue(QStringLiteral("uiTheme"),
                      mode == ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("system"));
}

ThemeMode Theme::currentTheme()
{
    return s_currentTheme;
}

QString Theme::themeModeName(ThemeMode mode)
{
    return mode == ThemeMode::Dark ? QStringLiteral("深色模式") : QStringLiteral("系统默认");
}

} // namespace mviewer::ui
