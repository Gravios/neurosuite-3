/***************************************************************************
 *                        bindingregistry_test.cpp                         *
 *                                                                         *
 *  Standalone test for the Qt-light input core (plan                      *
 *  claude/input-remapping-plan.md): Chord round-trip + equality, the      *
 *  BindingRegistry override layer, the Resolver's scope/layer precedence  *
 *  and enabled()/active() gating, fall-through, and within-scope conflict  *
 *  detection.  Exits 0 on success, non-zero on first failure.             *
 *                                                                         *
 *  No widgets — only Qt6 Core (QString/Qt flags) + Gui (QKeySequence, via  *
 *  Chord::displayString in chord.cpp).  See                               *
 *  src/klusters/tests/CMakeLists.txt (klusters_test_bindingregistry).     *
 ***************************************************************************/
#include "input/bindingregistry.h"
#include "input/chord.h"

#include <QKeySequence>
#include <cstdio>

using namespace input;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// Mutable scope/command gates the test lambdas close over (std::function needs them
// to outlive the registry, so file scope).
static bool g_viewActive  = true;
static bool g_modeActive  = false;
static bool g_transActive = false;
static bool g_cmdEnabled  = true;
static int  g_lastInvoked = 0;   // which command's invoke() last ran

int main()
{
    // ── Chord: round-trip (persistence) ──────────────────────────────────
    {
        const Chord k = Chord::key(Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        const Chord r = Chord::fromString(k.toString());
        CHECK(r == k);
        CHECK(r.device == Device::Key);
        CHECK(r.code == Qt::Key_S);
        CHECK(r.phase == Phase::Press);

        const Chord b = Chord::button(Qt::RightButton, Qt::ControlModifier);
        CHECK(Chord::fromString(b.toString()) == b);

        const Chord w = Chord::wheel(-1, Qt::ControlModifier);
        const Chord w2 = Chord::fromString(w.toString());
        CHECK(w2 == w);
        CHECK(w2.phase == Phase::Wheel);
        CHECK(w2.code == -1);

        // Malformed strings -> invalid chord.
        CHECK(!Chord::fromString(QStringLiteral("garbage")).isValid());
        CHECK(!Chord::fromString(QStringLiteral("1/2/3")).isValid());   // too few fields
        CHECK(!Chord::fromString(QString()).isValid());
    }

    // ── Chord: equality + modifier normalization ─────────────────────────
    {
        // Keypad is masked out, so a numpad modifier does not change identity.
        const Chord a = Chord::key(Qt::Key_Return, Qt::ControlModifier);
        const Chord b = Chord::key(Qt::Key_Return, Qt::ControlModifier | Qt::KeypadModifier);
        CHECK(a == b);
        // Different modifier -> different chord.
        CHECK(Chord::key(Qt::Key_A, Qt::ControlModifier) != Chord::key(Qt::Key_A, Qt::AltModifier));
        // An invalid (unbound) chord never equals a real one.
        CHECK(Chord{} != a);
        CHECK(!Chord{}.isValid());
    }

    // ── Registry: effectiveChord default vs override ─────────────────────
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        Command c;
        c.id = QStringLiteral("app.prefs");
        c.scopeId = QStringLiteral("app");
        c.defaultChord = Chord::key(Qt::Key_P, Qt::ControlModifier);
        reg.addCommand(c);

        CHECK(reg.effectiveChord(QStringLiteral("app.prefs")) == Chord::key(Qt::Key_P, Qt::ControlModifier));
        CHECK(!reg.hasOverride(QStringLiteral("app.prefs")));
        CHECK(!reg.effectiveChord(QStringLiteral("does.not.exist")).isValid());

        const Chord ov = Chord::key(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        reg.setOverride(QStringLiteral("app.prefs"), ov);
        CHECK(reg.hasOverride(QStringLiteral("app.prefs")));
        CHECK(reg.effectiveChord(QStringLiteral("app.prefs")) == ov);
        CHECK(reg.overrides().size() == 1);

        reg.clearOverride(QStringLiteral("app.prefs"));
        CHECK(!reg.hasOverride(QStringLiteral("app.prefs")));
        CHECK(reg.effectiveChord(QStringLiteral("app.prefs")) == Chord::key(Qt::Key_P, Qt::ControlModifier));

        reg.setOverride(QStringLiteral("app.prefs"), ov);
        reg.clearAllOverrides();
        CHECK(reg.overrides().isEmpty());
    }

    // ── Resolver: layer precedence, active() + enabled() gating, fall-through ──
    {
        g_viewActive = true; g_modeActive = false; g_transActive = false;
        g_cmdEnabled = true;  g_lastInvoked = 0;

        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"),   Layer::App,      nullptr });
        reg.addScope({ QStringLiteral("view"),  Layer::ViewType, [](const Ctx&){ return g_viewActive;  } });
        reg.addScope({ QStringLiteral("mode"),  Layer::ToolMode, [](const Ctx&){ return g_modeActive;  } });
        reg.addScope({ QStringLiteral("trans"), Layer::Transient,[](const Ctx&){ return g_transActive; } });

        const Chord left = Chord::button(Qt::LeftButton);

        auto mk = [](const char* id, const char* scope, Chord ch, int tag,
                     std::function<bool(const Ctx&)> en = nullptr) {
            Command c;
            c.id = QString::fromLatin1(id);
            c.scopeId = QString::fromLatin1(scope);
            c.defaultChord = ch;
            c.enabled = std::move(en);
            c.invoke = [tag](const Ctx&){ g_lastInvoked = tag; };
            return c;
        };
        reg.addCommand(mk("view.left",  "view",  left, 1));
        reg.addCommand(mk("mode.left",  "mode",  left, 2));
        reg.addCommand(mk("trans.left", "trans", left, 3));
        // An app command gated by enabled() we can turn off.
        reg.addCommand(mk("app.left",   "app",   Chord::button(Qt::LeftButton, Qt::AltModifier), 9,
                          [](const Ctx&){ return g_cmdEnabled; }));

        // Only view active -> view wins.
        const Command* r = reg.resolve(left);
        CHECK(r && r->id == QStringLiteral("view.left"));

        // Mode active -> shadows view.
        g_modeActive = true;
        r = reg.resolve(left);
        CHECK(r && r->id == QStringLiteral("mode.left"));

        // Transient active -> shadows mode + view (innermost wins).
        g_transActive = true;
        r = reg.resolve(left);
        CHECK(r && r->id == QStringLiteral("trans.left"));
        // invoke actually runs the resolved command.
        if (r) { Ctx ctx; r->invoke(ctx); }
        CHECK(g_lastInvoked == 3);

        // Turn the transient + mode off again -> back to view.
        g_transActive = false; g_modeActive = false;
        r = reg.resolve(left);
        CHECK(r && r->id == QStringLiteral("view.left"));

        // A chord nobody binds -> nullptr (the fall-through that preserves un-ported views).
        CHECK(reg.resolve(Chord::key(Qt::Key_F12)) == nullptr);

        // enabled()==false makes a command invisible to the resolver.
        CHECK(reg.resolve(Chord::button(Qt::LeftButton, Qt::AltModifier)) != nullptr);
        g_cmdEnabled = false;
        CHECK(reg.resolve(Chord::button(Qt::LeftButton, Qt::AltModifier)) == nullptr);
        g_cmdEnabled = true;
    }

    // ── Resolver: same-layer tie -> later registration shadows earlier ───
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("a"), Layer::ViewType, nullptr });
        reg.addScope({ QStringLiteral("b"), Layer::ViewType, nullptr });
        const Chord x = Chord::key(Qt::Key_X);
        Command ca; ca.id = QStringLiteral("a.x"); ca.scopeId = QStringLiteral("a"); ca.defaultChord = x;
        Command cb; cb.id = QStringLiteral("b.x"); cb.scopeId = QStringLiteral("b"); cb.defaultChord = x;
        reg.addCommand(ca);
        reg.addCommand(cb);
        const Command* r = reg.resolve(x);
        CHECK(r && r->id == QStringLiteral("b.x"));   // "b" registered later -> shadows "a"
    }

    // ── Conflicts: within a scope reported, across scopes not ────────────
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"),  Layer::App,      nullptr });
        reg.addScope({ QStringLiteral("view"), Layer::ViewType, nullptr });

        const Chord s = Chord::key(Qt::Key_S);
        Command a; a.id = QStringLiteral("app.a"); a.scopeId = QStringLiteral("app"); a.defaultChord = s;
        Command b; b.id = QStringLiteral("app.b"); b.scopeId = QStringLiteral("app"); b.defaultChord = s;
        Command v; v.id = QStringLiteral("view.v"); v.scopeId = QStringLiteral("view"); v.defaultChord = s;
        reg.addCommand(a);
        reg.addCommand(b);   // same chord, same scope as a -> a real conflict
        reg.addCommand(v);   // same chord, DIFFERENT scope -> legal, not reported

        QList<BindingRegistry::Conflict> cs = reg.conflicts();
        CHECK(cs.size() == 1);
        if (cs.size() == 1) {
            CHECK(cs[0].scopeId == QStringLiteral("app"));
            CHECK(cs[0].commandIds.size() == 2);
            CHECK(cs[0].commandIds.contains(QStringLiteral("app.a")));
            CHECK(cs[0].commandIds.contains(QStringLiteral("app.b")));
        }

        // Override b onto a different chord -> conflict resolves.
        reg.setOverride(QStringLiteral("app.b"), Chord::key(Qt::Key_D));
        CHECK(reg.conflicts().isEmpty());
    }

    // ── Chord <-> QKeySequence bridge (mirroring menu/toolbar QActions) ──
    {
        // Round-trip common shortcut shapes through a QKeySequence and back.
        const Chord ctrlS = Chord::key(Qt::Key_S, Qt::ControlModifier);
        CHECK(chordFromKeySequence(keySequenceFromChord(ctrlS)) == ctrlS);
        const Chord shiftDel = Chord::key(Qt::Key_Delete, Qt::ShiftModifier);
        CHECK(chordFromKeySequence(keySequenceFromChord(shiftDel)) == shiftDel);
        const Chord plainG = Chord::key(Qt::Key_G);
        CHECK(chordFromKeySequence(keySequenceFromChord(plainG)) == plainG);

        // Parse a real sequence string -> the expected Key chord.
        CHECK(chordFromKeySequence(QKeySequence(QStringLiteral("Ctrl+A")))
              == Chord::key(Qt::Key_A, Qt::ControlModifier));

        // Empty sequence -> invalid chord; a non-key chord -> empty sequence.
        CHECK(!chordFromKeySequence(QKeySequence()).isValid());
        CHECK(keySequenceFromChord(Chord::button(Qt::LeftButton)).isEmpty());
    }

    // ── external commands are mirrored but never resolver-dispatched ─────
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        Command c;
        c.id = QStringLiteral("app.ext");
        c.scopeId = QStringLiteral("app");
        c.defaultChord = Chord::key(Qt::Key_S, Qt::ControlModifier);
        c.external = true;                 // Qt dispatches it; resolve() must skip it
        bool ran = false;
        c.invoke = [&ran](const Ctx&){ ran = true; };
        reg.addCommand(c);
        CHECK(reg.resolve(Chord::key(Qt::Key_S, Qt::ControlModifier)) == nullptr);
        CHECK(!ran);
        // ...but it is still in the registry (visible to Preferences / cheat-sheet).
        CHECK(reg.command(QStringLiteral("app.ext")) != nullptr);

        // A non-external command on the same chord still resolves (it is not shadowed
        // by the external one — external simply drops out of resolution).
        Command live;
        live.id = QStringLiteral("app.live");
        live.scopeId = QStringLiteral("app");
        live.defaultChord = Chord::key(Qt::Key_S, Qt::ControlModifier);
        reg.addCommand(live);
        const Command* r = reg.resolve(Chord::key(Qt::Key_S, Qt::ControlModifier));
        CHECK(r && r->id == QStringLiteral("app.live"));
    }

    // ── modMatch: Exact vs AtLeast (for the mouse gestures) ──────────────
    {
        // Exact: Ctrl+Left matches only Ctrl+Left.
        const Chord exact = Chord::button(Qt::LeftButton, Qt::ControlModifier);
        CHECK(exact.matches(Chord::button(Qt::LeftButton, Qt::ControlModifier)));
        CHECK(!exact.matches(Chord::button(Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier)));
        CHECK(!exact.matches(Chord::button(Qt::LeftButton)));

        // AtLeast + Ctrl: matches Ctrl+Left and Ctrl+Shift+Left, not plain Left.
        const Chord ctrlish = Chord::button(Qt::LeftButton, Qt::ControlModifier,
                                            Phase::Press, ModMatch::AtLeast);
        CHECK(ctrlish.matches(Chord::button(Qt::LeftButton, Qt::ControlModifier)));
        CHECK(ctrlish.matches(Chord::button(Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier)));
        CHECK(!ctrlish.matches(Chord::button(Qt::LeftButton)));

        // AtLeast + NoModifier: matches ANY modifiers (the base-zoom "any Left").
        const Chord anyLeft = Chord::button(Qt::LeftButton, Qt::NoModifier,
                                            Phase::Press, ModMatch::AtLeast);
        CHECK(anyLeft.matches(Chord::button(Qt::LeftButton)));
        CHECK(anyLeft.matches(Chord::button(Qt::LeftButton, Qt::ShiftModifier)));
        CHECK(anyLeft.matches(Chord::button(Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier)));
        CHECK(!anyLeft.matches(Chord::button(Qt::RightButton)));   // device/code still must agree

        // Round-trip preserves modMatch; the old 4-field form loads as Exact.
        CHECK(Chord::fromString(ctrlish.toString()) == ctrlish);
        const Chord fromOld = Chord::fromString(QStringLiteral("2/1/0/0"));  // Button/Left/NoMod/Press
        CHECK(fromOld.isValid());
        CHECK(fromOld.modMatch == ModMatch::Exact);
    }

    // ── resolve() honors AtLeast ─────────────────────────────────────────
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        Command pan;
        pan.id = QStringLiteral("x.pan");
        pan.scopeId = QStringLiteral("app");
        pan.defaultChord = Chord::button(Qt::LeftButton, Qt::ControlModifier,
                                         Phase::Press, ModMatch::AtLeast);
        reg.addCommand(pan);
        // The resolver is handed a concrete (Exact) event chord; the AtLeast binding matches.
        CHECK(reg.resolve(Chord::button(Qt::LeftButton, Qt::ControlModifier)) != nullptr);
        CHECK(reg.resolve(Chord::button(Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier)) != nullptr);
        CHECK(reg.resolve(Chord::button(Qt::LeftButton)) == nullptr);   // no Ctrl -> no match
    }

    if (g_fail == 0) std::printf("bindingregistry_test: OK\n");
    return g_fail == 0 ? 0 : 1;
}
