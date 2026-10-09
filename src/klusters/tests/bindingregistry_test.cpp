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
static bool g_exclActive  = false;   // the Exclusive (modal-capture) scope's active() gate
static bool g_lowerActive = true;    // a lower Passive scope sitting under the modal one

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

    // ── resolveEx: Exclusive scope captures the keyboard (modal mechanism) ─
    {
        g_exclActive = false; g_lowerActive = true;
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        // A lower Passive view scope binds X; a higher Transient scope binds only Enter
        // and (below) is flipped between Exclusive and Passive capture.
        reg.addScope({ QStringLiteral("view"), Layer::ViewType,
                       [](const Ctx&){ return g_lowerActive; } });
        InputScope modal;
        modal.id      = QStringLiteral("modal");
        modal.layer   = Layer::Transient;
        modal.active  = [](const Ctx&){ return g_exclActive; };
        modal.capture = Capture::Exclusive;
        reg.addScope(modal);

        const Chord enter = Chord::key(Qt::Key_Return);
        const Chord x     = Chord::key(Qt::Key_X);
        Command vx; vx.id = QStringLiteral("view.x"); vx.scopeId = QStringLiteral("view");
        vx.defaultChord = x; reg.addCommand(vx);
        Command me; me.id = QStringLiteral("modal.enter"); me.scopeId = QStringLiteral("modal");
        me.defaultChord = enter; reg.addCommand(me);

        // Modal inactive -> ordinary resolution: X resolves + consumes; an unbound key
        // falls through (consume == false), exactly as before capture existed.
        {
            CHECK(!reg.hasActiveCapture());   // no Exclusive scope is active
            BindingRegistry::Resolution r = reg.resolveEx(x);
            CHECK(r.command && r.command->id == QStringLiteral("view.x") && r.consume);
            BindingRegistry::Resolution f = reg.resolveEx(Chord::key(Qt::Key_F12));
            CHECK(f.command == nullptr && !f.consume);
        }

        // Modal active + Exclusive -> it owns the keyboard.
        g_exclActive = true;
        {
            CHECK(reg.hasActiveCapture());    // the Exclusive modal is now active
            // Its own Enter fires.
            BindingRegistry::Resolution e = reg.resolveEx(enter);
            CHECK(e.command && e.command->id == QStringLiteral("modal.enter") && e.consume);
            // X is bound in the lower view scope, but the Exclusive modal shadows it:
            // no command, yet CONSUMED -> the view never sees it.
            BindingRegistry::Resolution sx = reg.resolveEx(x);
            CHECK(sx.command == nullptr && sx.consume);
            // An arbitrary unbound key is swallowed too.
            BindingRegistry::Resolution su = reg.resolveEx(Chord::key(Qt::Key_F12));
            CHECK(su.command == nullptr && su.consume);
            // A held-key auto-repeat reaches the resolver as an INVALID chord; the modal
            // still swallows it rather than leaking it to the view / palette below.
            BindingRegistry::Resolution sr = reg.resolveEx(Chord{});
            CHECK(sr.command == nullptr && sr.consume);
            // Back-compat: resolve() reports only the command, so the swallow cases look
            // like "nothing" to callers that ignore consume — their behavior is unchanged.
            CHECK(reg.resolve(x) == nullptr);
            CHECK(reg.resolve(enter) != nullptr);
        }

        // Same scope active but Passive capture -> X falls back through to the view,
        // proving the swallow is the capture policy, not merely a transient scope existing.
        {
            InputScope passiveModal = modal;
            passiveModal.capture = Capture::Passive;
            reg.addScope(passiveModal);                 // addScope replaces by id
            CHECK(!reg.hasActiveCapture());             // active, but no longer Exclusive
            BindingRegistry::Resolution sx = reg.resolveEx(x);
            CHECK(sx.command && sx.command->id == QStringLiteral("view.x") && sx.consume);
            BindingRegistry::Resolution su = reg.resolveEx(Chord::key(Qt::Key_F12));
            CHECK(su.command == nullptr && !su.consume);
        }
        g_exclActive = false;
    }

    // ── resolveEx: an active Exclusive scope outranks a later-registered Passive one ──
    // Regression for the watershed-vs-t-SNE bug: both are Transient scopes active at once,
    // and the Passive embedding scope was registered LATER, so the "later shadows earlier"
    // tie-break let it steal Up/Down from the Exclusive watershed modal.  An active
    // Exclusive scope must own input regardless of registration order.
    {
        BindingRegistry reg;
        reg.addScope({ QStringLiteral("app"), Layer::App, nullptr });
        // Exclusive modal registered FIRST, same layer as...
        InputScope modal;
        modal.id      = QStringLiteral("modal");
        modal.layer   = Layer::Transient;
        modal.active  = [](const Ctx&){ return true; };
        modal.capture = Capture::Exclusive;
        reg.addScope(modal);
        // ...a Passive scope registered LATER, also active.
        reg.addScope({ QStringLiteral("passive"), Layer::Transient,
                       [](const Ctx&){ return true; } });   // Passive (default)

        const Chord up = Chord::key(Qt::Key_Up);
        Command mUp;   mUp.id   = QStringLiteral("modal.up");     mUp.scopeId   = QStringLiteral("modal");
        mUp.defaultChord = up;                        reg.addCommand(mUp);
        Command pUp;   pUp.id   = QStringLiteral("passive.up");   pUp.scopeId   = QStringLiteral("passive");
        pUp.defaultChord = up;                        reg.addCommand(pUp);     // SAME chord, later scope
        Command pDown; pDown.id = QStringLiteral("passive.down"); pDown.scopeId = QStringLiteral("passive");
        pDown.defaultChord = Chord::key(Qt::Key_Down); reg.addCommand(pDown);

        // The Exclusive modal wins the shared chord, though the Passive scope registered later.
        BindingRegistry::Resolution ru = reg.resolveEx(up);
        CHECK(ru.command && ru.command->id == QStringLiteral("modal.up") && ru.consume);
        // A chord only the Passive scope binds is still SWALLOWED by the Exclusive modal —
        // it owns the keyboard, so the Passive scope is never consulted.
        BindingRegistry::Resolution rd = reg.resolveEx(Chord::key(Qt::Key_Down));
        CHECK(rd.command == nullptr && rd.consume);
    }

    if (g_fail == 0) std::printf("bindingregistry_test: OK\n");
    return g_fail == 0 ? 0 : 1;
}
