/***************************************************************************
 *  theme.cpp  -  see theme.h
 ***************************************************************************/

#include "theme.h"

#include <QtGlobal>
#include <QApplication>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QColor>
#include <QSettings>
#include <QStyleFactory>

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

namespace neurosuite {

namespace {

bool  g_captured = false;   // has the OS appearance been sampled yet?
bool  g_systemDark = false; // sampled OS appearance (fallback for Qt < 6.5)
Theme g_current = Theme::System;

double luminance(const QColor& c)
{
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}

// Sample the OS appearance once, before any palette override, so System can be
// resolved even on Qt versions without a colour-scheme query.
void captureSystem()
{
    if (g_captured) return;
    g_captured = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()) {
        g_systemDark =
            QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        return;
    }
#endif
    if (qApp) {
        const QColor w =
            QGuiApplication::palette().color(QPalette::Active, QPalette::Window);
        g_systemDark = luminance(w) < 128.0;
    }
}

// A coherent light or dark palette with sensible text and disabled colours.
QPalette buildPalette(bool dark)
{
    QColor window, windowText, base, altBase, text, button, buttonText, brightText,
        highlight, highlightedText, tooltipBase, tooltipText, link, placeholder,
        disabledText, disabledHighlight, disabledHighlightedText;

    if (dark) {
        window                 = QColor( 53,  53,  53);
        windowText             = QColor(220, 220, 220);
        base                   = QColor( 35,  35,  35);
        altBase                = QColor( 46,  46,  46);
        text                   = QColor(220, 220, 220);
        button                 = QColor( 60,  60,  60);
        buttonText             = QColor(220, 220, 220);
        brightText             = QColor(255,  90,  90);
        highlight              = QColor( 42, 130, 218);
        highlightedText        = QColor(255, 255, 255);
        tooltipBase            = QColor( 46,  46,  46);
        tooltipText            = QColor(220, 220, 220);
        link                   = QColor( 86, 156, 214);
        placeholder            = QColor(150, 150, 150);
        disabledText           = QColor(120, 120, 120);
        disabledHighlight      = QColor( 70,  70,  70);
        disabledHighlightedText= QColor(150, 150, 150);
    } else {
        window                 = QColor(239, 239, 239);
        windowText             = QColor( 20,  20,  20);
        base                   = QColor(255, 255, 255);
        altBase                = QColor(247, 247, 247);
        text                   = QColor( 20,  20,  20);
        button                 = QColor(239, 239, 239);
        buttonText             = QColor( 20,  20,  20);
        brightText             = QColor(200,   0,   0);
        highlight              = QColor( 48, 140, 198);
        highlightedText        = QColor(255, 255, 255);
        tooltipBase            = QColor(255, 255, 225);
        tooltipText            = QColor( 20,  20,  20);
        link                   = QColor(  0, 102, 204);
        placeholder            = QColor(120, 120, 120);
        disabledText           = QColor(160, 160, 160);
        disabledHighlight      = QColor(205, 205, 205);
        disabledHighlightedText= QColor(255, 255, 255);
    }

    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      windowText);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   altBase);
    p.setColor(QPalette::ToolTipBase,     tooltipBase);
    p.setColor(QPalette::ToolTipText,     tooltipText);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          button);
    p.setColor(QPalette::ButtonText,      buttonText);
    p.setColor(QPalette::BrightText,      brightText);
    p.setColor(QPalette::Link,            link);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::PlaceholderText, placeholder);

    p.setColor(QPalette::Disabled, QPalette::WindowText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight,       disabledHighlight);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledHighlightedText);

    return p;
}

} // namespace

bool systemIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints())
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    captureSystem();
    return g_systemDark;
}

bool isDark(Theme theme)
{
    switch (theme) {
        case Theme::Dark:  return true;
        case Theme::Light: return false;
        case Theme::System:
        default:           return systemIsDark();
    }
}

void applyTheme(Theme theme)
{
    if (!qApp) return;
    const bool dark = isDark(theme);
    // Fusion honours a custom QPalette uniformly across platforms, where the
    // native styles may ignore it (notably for dark mode).
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(buildPalette(dark));
    g_current = theme;
}

Theme currentTheme()
{
    return g_current;
}

QString themeToString(Theme theme)
{
    switch (theme) {
        case Theme::Light: return QStringLiteral("light");
        case Theme::Dark:  return QStringLiteral("dark");
        case Theme::System:
        default:           return QStringLiteral("system");
    }
}

Theme themeFromString(const QString& token)
{
    const QString t = token.trimmed().toLower();
    if (t == QLatin1String("light")) return Theme::Light;
    if (t == QLatin1String("dark"))  return Theme::Dark;
    return Theme::System;
}

QString themeDisplayName(Theme theme)
{
    switch (theme) {
        case Theme::Light: return QCoreApplication::translate("neurosuite::Theme", "Light");
        case Theme::Dark:  return QCoreApplication::translate("neurosuite::Theme", "Dark");
        case Theme::System:
        default:           return QCoreApplication::translate("neurosuite::Theme", "Match system");
    }
}

Theme loadThemePreference()
{
    QSettings s(QStringLiteral("neurosuite"), QStringLiteral("appearance"));
    return themeFromString(
        s.value(QStringLiteral("theme"), QStringLiteral("system")).toString());
}

void saveThemePreference(Theme theme)
{
    QSettings s(QStringLiteral("neurosuite"), QStringLiteral("appearance"));
    s.setValue(QStringLiteral("theme"), themeToString(theme));
}

void initThemeFromSettings()
{
    if (!qApp) return;
    captureSystem();                 // sample the OS before overriding the palette
    applyTheme(loadThemePreference());

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    static bool connected = false;
    if (!connected) {
        connected = true;
        QObject::connect(QGuiApplication::styleHints(),
                         &QStyleHints::colorSchemeChanged, qApp,
                         [](Qt::ColorScheme) {
                             if (currentTheme() == Theme::System)
                                 applyTheme(Theme::System);
                         });
    }
#endif
}

} // namespace neurosuite
