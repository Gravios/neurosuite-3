/***************************************************************************
 *  theme_test.cpp  -  regression test for the shared theming module.
 *
 *  Self-contained, assert-style (own main). Built only with NS_BUILD_TESTS;
 *  run via ctest. Needs an offscreen QPA platform (set by the test ENVIRONMENT)
 *  because it constructs a QApplication and inspects the applied palette.
 ***************************************************************************/

#include <klustersshared/theme.h>

#include <QApplication>
#include <QPalette>

#include <cmath>
#include <cstdio>

using namespace neurosuite;

static int fails = 0;
static double lum(const QColor& c)
{
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}
static void chk(bool cond, const char* msg)
{
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) ++fails;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Token round-trip.
    chk(themeFromString("dark") == Theme::Dark, "parse dark");
    chk(themeFromString("LIGHT") == Theme::Light, "parse light (case-insensitive)");
    chk(themeFromString("garbage") == Theme::System, "parse fallback to system");
    chk(themeToString(Theme::Dark) == "dark", "emit dark token");
    chk(themeToString(Theme::System) == "system", "emit system token");

    // Dark appearance.
    applyTheme(Theme::Dark);
    chk(isDark(Theme::Dark), "isDark(Dark)");
    chk(currentTheme() == Theme::Dark, "currentTheme tracks applyTheme");
    {
        const QPalette d = app.palette();
        const double win = lum(d.color(QPalette::Window));
        const double txt = lum(d.color(QPalette::WindowText));
        chk(win < 128.0, "dark: window is dark");
        chk(txt > 128.0, "dark: window text is light");
        chk(txt > win, "dark: text brighter than background");
        chk(lum(d.color(QPalette::ButtonText)) > 128.0, "dark: button text is light");
        const double dis = lum(d.color(QPalette::Disabled, QPalette::Text));
        chk(dis > win && dis < txt, "dark: disabled text sits between bg and text");
    }

    // Light appearance.
    applyTheme(Theme::Light);
    chk(!isDark(Theme::Light), "isDark(Light) is false");
    {
        const QPalette l = app.palette();
        const double win = lum(l.color(QPalette::Window));
        const double txt = lum(l.color(QPalette::WindowText));
        chk(win > 128.0, "light: window is light");
        chk(txt < 128.0, "light: window text is dark");
        chk(win > txt, "light: background brighter than text");
        const double hl = lum(l.color(QPalette::Highlight));
        const double hlt = lum(l.color(QPalette::HighlightedText));
        chk(std::abs(hl - hlt) > 60.0, "light: highlighted text contrasts highlight");
    }

    // Preference persistence (shared settings scope).
    saveThemePreference(Theme::Dark);
    chk(loadThemePreference() == Theme::Dark, "persist/load dark");
    saveThemePreference(Theme::System);
    chk(loadThemePreference() == Theme::System, "persist/load system");

    std::printf(fails ? "FAILURES: %d\n" : "ALL PASS (0 failures)\n", fails);
    return fails ? 1 : 0;
}
