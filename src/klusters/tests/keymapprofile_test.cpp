/***************************************************************************
 *                         keymapprofile_test.cpp                          *
 *                                                                         *
 *  Standalone test for the keymap-profile core (plan                      *
 *  claude/input-remapping-plan.md, "Keymap profiles"): serialize <-> parse *
 *  round-trip, parse robustness (comments / blanks / missing name / bad    *
 *  binding lines), capture of a registry's override set into a profile,    *
 *  and apply() replacing the override set (clear prior + set profile; the  *
 *  empty "Default" profile clears everything).  Exits 0 on success,        *
 *  non-zero on first failure.                                             *
 *                                                                         *
 *  No widgets — Qt6 Core + Gui only (QString/Qt flags; QKeySequence via    *
 *  Chord::displayString in chord.cpp).  See                               *
 *  src/klusters/tests/CMakeLists.txt (klusters_test_keymapprofile).        *
 ***************************************************************************/
#include "input/keymapprofile.h"
#include "input/bindingregistry.h"
#include "input/chord.h"

#include <QTemporaryDir>
#include <QFile>
#include <QIODevice>
#include <cstdio>

using namespace input;

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

int main()
{
    // ── serialize -> parse round-trip (key / button / wheel chords) ──────
    {
        KeymapProfile p;
        p.name        = QStringLiteral("AZERTY");
        p.description = QStringLiteral("French layout");
        p.bindings.insert(QStringLiteral("cluster.pan"),
            Chord::button(Qt::LeftButton, Qt::ControlModifier, Phase::Press, ModMatch::AtLeast));
        p.bindings.insert(QStringLiteral("app.prefs"),
            Chord::key(Qt::Key_P, Qt::ControlModifier));
        p.bindings.insert(QStringLiteral("cluster.wheelZoomIn"),
            Chord::wheel(+1, Qt::ControlModifier, ModMatch::AtLeast));

        bool ok = false;
        const KeymapProfile r = parseKeymap(serializeKeymap(p), &ok);
        CHECK(ok);
        CHECK(r.name == p.name);
        CHECK(r.description == p.description);
        CHECK(r.bindings.size() == p.bindings.size());
        CHECK(r.bindings.value(QStringLiteral("cluster.pan"))
              == p.bindings.value(QStringLiteral("cluster.pan")));
        CHECK(r.bindings.value(QStringLiteral("app.prefs"))
              == p.bindings.value(QStringLiteral("app.prefs")));
        CHECK(r.bindings.value(QStringLiteral("cluster.wheelZoomIn"))
              == p.bindings.value(QStringLiteral("cluster.wheelZoomIn")));
    }

    // ── parse robustness: comments, blanks, trimming, bad lines ──────────
    {
        const QString text =
            QStringLiteral("# a comment\n")
          + QStringLiteral("\n")
          + QStringLiteral("name = Mix\n")
          + QStringLiteral("   description =   spaced out   \n")
          + QStringLiteral("app.prefs = ") + Chord::key(Qt::Key_P, Qt::ControlModifier).toString() + QStringLiteral("\n")
          + QStringLiteral("bogus.line.without.equals\n")          // no '=' -> skipped
          + QStringLiteral("bad.chord = not-a-chord\n")            // invalid chord -> skipped
          + QStringLiteral("# trailing comment\n");
        bool ok = false;
        const KeymapProfile r = parseKeymap(text, &ok);
        CHECK(ok);
        CHECK(r.name == QStringLiteral("Mix"));
        CHECK(r.description == QStringLiteral("spaced out"));       // leading/trailing trimmed
        CHECK(r.bindings.size() == 1);                             // only app.prefs survived
        CHECK(r.bindings.contains(QStringLiteral("app.prefs")));
        CHECK(!r.bindings.contains(QStringLiteral("bad.chord")));

        // No `name` line -> ok false, but any valid bindings still parse.
        bool ok2 = true;
        const KeymapProfile noName =
            parseKeymap(QStringLiteral("app.prefs = ") + Chord::key(Qt::Key_P).toString(), &ok2);
        CHECK(!ok2);
        CHECK(noName.bindings.size() == 1);
    }

    // ── capture the registry's current override set into a profile ───────
    {
        BindingRegistry reg;
        reg.setOverride(QStringLiteral("app.prefs"), Chord::key(Qt::Key_P, Qt::ControlModifier));
        reg.setOverride(QStringLiteral("cluster.pan"),
            Chord::button(Qt::RightButton, Qt::ControlModifier, Phase::Press, ModMatch::AtLeast));

        const KeymapProfile p = captureKeymap(reg, QStringLiteral("Mine"), QStringLiteral("my layout"));
        CHECK(p.name == QStringLiteral("Mine"));
        CHECK(p.description == QStringLiteral("my layout"));
        CHECK(p.bindings.size() == 2);
        CHECK(p.bindings.value(QStringLiteral("app.prefs")) == Chord::key(Qt::Key_P, Qt::ControlModifier));
    }

    // ── apply REPLACES the override set (clear prior, set profile) ───────
    {
        BindingRegistry reg;
        // A pre-existing override the profile does NOT mention must be cleared.
        reg.setOverride(QStringLiteral("stale.cmd"), Chord::key(Qt::Key_Z));
        CHECK(reg.hasOverride(QStringLiteral("stale.cmd")));

        KeymapProfile p;
        p.name = QStringLiteral("New");
        p.bindings.insert(QStringLiteral("app.prefs"), Chord::key(Qt::Key_P, Qt::ControlModifier));
        applyKeymap(reg, p);

        CHECK(!reg.hasOverride(QStringLiteral("stale.cmd")));        // prior override gone
        CHECK(reg.hasOverride(QStringLiteral("app.prefs")));         // profile override applied
        CHECK(reg.effectiveChord(QStringLiteral("app.prefs"))
              == Chord::key(Qt::Key_P, Qt::ControlModifier));

        // The empty-bindings "Default" profile clears every override.
        KeymapProfile def;
        def.name = QStringLiteral("Default");
        applyKeymap(reg, def);
        CHECK(!reg.hasOverride(QStringLiteral("app.prefs")));
        CHECK(reg.overrides().isEmpty());
    }

    // ── loadKeymapsFromDir: scan a directory, parse the *.keymap files ───
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        auto write = [&](const QString& fn, const QString& content) {
            QFile f(tmp.filePath(fn));
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(content.toUtf8());
            f.close();
        };
        write(QStringLiteral("Default.keymap"),
              QStringLiteral("name = Default\ndescription = shipped\n"));                 // valid, no bindings
        write(QStringLiteral("AZERTY.keymap"),
              QStringLiteral("name = AZERTY\napp.prefs = ")
                  + Chord::key(Qt::Key_P, Qt::ControlModifier).toString() + QStringLiteral("\n"));  // valid, 1 binding
        write(QStringLiteral("notes.txt"),
              QStringLiteral("name = NotAKeymap\n"));                                     // wrong extension -> ignored
        write(QStringLiteral("broken.keymap"),
              QStringLiteral("app.prefs = ") + Chord::key(Qt::Key_P).toString() + QStringLiteral("\n")); // no name -> skipped

        const QList<KeymapProfile> loaded = loadKeymapsFromDir(tmp.path());
        CHECK(loaded.size() == 2);                      // Default + AZERTY only
        bool haveDefault = false, haveAzerty = false;
        for (const KeymapProfile& p : loaded) {
            if (p.name == QStringLiteral("Default")) { haveDefault = true; CHECK(p.bindings.isEmpty()); }
            if (p.name == QStringLiteral("AZERTY"))  { haveAzerty  = true; CHECK(p.bindings.size() == 1); }
        }
        CHECK(haveDefault);
        CHECK(haveAzerty);

        // A directory with no .keymap files yields an empty list (not an error).
        QTemporaryDir empty;
        CHECK(empty.isValid());
        CHECK(loadKeymapsFromDir(empty.path()).isEmpty());
    }

    if (g_fail == 0)
        std::printf("keymapprofile: all checks passed\n");
    return g_fail == 0 ? 0 : 1;
}
