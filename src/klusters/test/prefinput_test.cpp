// prefinput_test.cpp — the Input preferences page's per-row Reset button (input/prefinput.h).
//
// One button, two steps: on a row that is OFF its shipped default a press restores the
// default; on a row that is AT its default a press unbinds the command.  So the first
// press resets and the second clears, and the button's label says which it will do.
// Checked on the real page, over a private registry state and an in-memory store:
//   - key, mouse-button and wheel rows each go custom -> default -> unbound -> default;
//   - an unbound row is committed as an override (the invalid chord), stored as
//     "unbound", and is still unbound when the page is loaded again -- it used to be
//     dropped, so the default came back;
//   - a row reset to its default commits as no override at all;
//   - a command that ships unbound: its button clears a custom key and is then disabled;
//   - "Save As" semantics: a captured layout carries the unbound command.
// Own main; run via ctest with QT_QPA_PLATFORM=offscreen.

#include "input/prefinput.h"
#include "input/bindingregistry.h"
#include "input/buttonchordedit.h"
#include "input/wheelchordedit.h"
#include "input/inputdispatcher.h"
#include "input/inputprefsstore.h"

#include <QApplication>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QToolButton>

#include <cstdio>
#include <string>

using namespace input;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

namespace {
struct Store : InputPrefsStore {
    QMap<QString, QString> overrides, profiles;
    const QMap<QString, QString>& getInputBindingOverrides() const override { return overrides; }
    void setInputBindingOverrides(const QMap<QString, QString>& m) override { overrides = m; }
    const QMap<QString, QString>& getInputProfiles() const override { return profiles; }
    void setInputProfiles(const QMap<QString, QString>& m) override { profiles = m; }
};

// The row's widgets, found by the command's label: the name QLabel, its editor and its
// Reset button are siblings in one QHBoxLayout, so they are consecutive among the
// body's children in creation order: label, editor, button.
struct RowWidgets {
    QKeySequenceEdit* key = nullptr;
    ButtonChordEdit*  button = nullptr;
    WheelChordEdit*   wheel = nullptr;
    QToolButton*      reset = nullptr;
};
RowWidgets rowOf(PrefInput& page, const QString& label) {
    RowWidgets out;
    QLabel* name = nullptr;
    for (QLabel* l : page.findChildren<QLabel*>())
        if (l->text() == label) { name = l; break; }
    if (!name) return out;
    const QObjectList kids = name->parent()->children();
    const int at = static_cast<int>(kids.indexOf(name));
    for (int i = at + 1; i < kids.size() && !out.reset; ++i) {
        if (auto* k = qobject_cast<QKeySequenceEdit*>(kids.at(i))) out.key = k;
        else if (auto* b = qobject_cast<ButtonChordEdit*>(kids.at(i))) out.button = b;
        else if (auto* w = qobject_cast<WheelChordEdit*>(kids.at(i))) out.wheel = w;
        else if (auto* t = qobject_cast<QToolButton*>(kids.at(i))) out.reset = t;
        else if (qobject_cast<QLabel*>(kids.at(i))) break;             // the next row
    }
    return out;
}
std::string s(const QString& q) { return q.toStdString(); }
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    BindingRegistry& reg = registry();
    InputScope scope; scope.id = QStringLiteral("test");
    reg.addScope(scope);
    auto add = [&](const char* id, const char* label, const Chord& def) {
        Command c; c.id = QString::fromLatin1(id); c.scopeId = scope.id;
        c.label = QString::fromLatin1(label); c.defaultChord = def;
        reg.addCommand(c);
    };
    const Chord keyDef   = Chord::key(Qt::Key_K, Qt::ControlModifier);
    const Chord btnDef   = Chord::button(Qt::LeftButton, Qt::ControlModifier, Phase::Press, ModMatch::AtLeast);
    const Chord wheelDef = Chord::wheel(+1, Qt::ControlModifier, ModMatch::AtLeast);
    add("t.key",   "Key command",    keyDef);
    add("t.btn",   "Button command", btnDef);
    add("t.wheel", "Wheel command",  wheelDef);
    add("t.none",  "Unbound command", Chord{});

    Store store;
    PrefInput page(store);
    page.updateFromRegistry();
    int changes = 0;
    QObject::connect(&page, &PrefInput::changed, [&]{ ++changes; });

    RowWidgets key = rowOf(page, QStringLiteral("Key command"));
    RowWidgets btn = rowOf(page, QStringLiteral("Button command"));
    RowWidgets whl = rowOf(page, QStringLiteral("Wheel command"));
    RowWidgets non = rowOf(page, QStringLiteral("Unbound command"));
    check(key.key && key.reset && btn.button && btn.reset && whl.wheel && whl.reset && non.key && non.reset,
          "the four rows and their buttons are on the page");
    if (g_fail) { std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran); return 1; }

    // ── at the default: the button offers to unbind ──────────────────────
    check(key.reset->text() == QStringLiteral("Unbind"), "key row at its default: the button reads Unbind (got " + s(key.reset->text()) + ")");
    check(non.reset->text() == QStringLiteral("Reset") && !non.reset->isEnabled(),
          "a command that ships unbound and is unbound: nothing to do, the button is disabled");

    // ── key row: custom -> Reset -> default -> Unbind -> unbound -> Reset -> default ──
    key.key->setKeySequence(QKeySequence(QStringLiteral("Ctrl+J")));          // a user edit
    check(key.reset->text() == QStringLiteral("Reset"), "key row off its default: the button reads Reset");
    changes = 0;
    key.reset->click();
    check(chordFromKeySequence(key.key->keySequence()) == keyDef, "first press: back to the shipped default");
    check(key.reset->text() == QStringLiteral("Unbind"), "and the button now reads Unbind");
    check(changes == 1, "the press lights Apply");
    key.reset->click();
    check(key.key->keySequence().isEmpty(), "second press: the binding is cleared");
    check(key.reset->text() == QStringLiteral("Reset"), "and the button reads Reset again");
    key.reset->click();
    check(chordFromKeySequence(key.key->keySequence()) == keyDef, "third press: the default again");
    key.reset->click();                                                       // leave it unbound

    // ── button row ────────────────────────────────────────────────────────
    btn.reset->click();
    check(!btn.button->chord().isValid(), "button row at its default: a press unbinds it");
    btn.reset->click();
    check(btn.button->chord() == btnDef, "and the next restores the default, phase and modifier policy included");
    btn.reset->click();                                                       // leave it unbound

    // ── wheel row: leave it at the default ───────────────────────────────
    whl.reset->click();
    check(!whl.wheel->chord().isValid(), "wheel row at its default: a press unbinds it");
    whl.reset->click();
    check(whl.wheel->chord() == wheelDef, "and the next restores the default");

    // ── a command that ships unbound ─────────────────────────────────────
    non.key->setKeySequence(QKeySequence(QStringLiteral("Ctrl+U")));
    check(non.reset->isEnabled() && non.reset->text() == QStringLiteral("Reset"), "ships unbound, given a key: Reset is offered");
    non.reset->click();
    check(non.key->keySequence().isEmpty() && !non.reset->isEnabled(), "one press clears it; there is no second step");

    // ── commit: unbound is an override; the default is none ──────────────
    page.commitToRegistry();
    check(reg.hasOverride(QStringLiteral("t.key")) && !reg.effectiveChord(QStringLiteral("t.key")).isValid(),
          "commit: the unbound key command is an override with no chord");
    check(reg.hasOverride(QStringLiteral("t.btn")) && !reg.effectiveChord(QStringLiteral("t.btn")).isValid(),
          "commit: the unbound button command likewise");
    check(!reg.hasOverride(QStringLiteral("t.wheel")) && reg.effectiveChord(QStringLiteral("t.wheel")) == wheelDef,
          "commit: the row left at its default has no override");
    check(!reg.hasOverride(QStringLiteral("t.none")), "commit: ships unbound, is unbound: no override");
    check(store.overrides.value(QStringLiteral("t.key")) == QStringLiteral("unbound")
          && store.overrides.value(QStringLiteral("t.btn")) == QStringLiteral("unbound")
          && store.overrides.size() == 2,
          "stored as \"unbound\" for exactly the two commands");
    Ctx ctx;
    check(reg.resolve(keyDef, ctx) == nullptr, "the old default key no longer resolves to the command");

    // ── reopen: still unbound (it used to come back as the default) ──────
    PrefInput again(store);
    again.updateFromRegistry();
    RowWidgets key2 = rowOf(again, QStringLiteral("Key command"));
    RowWidgets btn2 = rowOf(again, QStringLiteral("Button command"));
    check(key2.key && key2.key->keySequence().isEmpty(), "reopened: the key row is still unbound");
    check(btn2.button && !btn2.button->chord().isValid(), "reopened: the button row is still unbound");
    check(key2.reset && key2.reset->text() == QStringLiteral("Reset"), "reopened: its button offers the default");
    btn2.reset->click();
    check(btn2.button->chord() == btnDef,
          "reopened unbound, then Reset: the command's own gesture (phase, modifier policy) comes back");
    key2.reset->click();
    again.commitToRegistry();
    check(!reg.hasOverride(QStringLiteral("t.key")) && !reg.hasOverride(QStringLiteral("t.btn"))
          && store.overrides.isEmpty(), "reset and committed: no overrides are left");

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
