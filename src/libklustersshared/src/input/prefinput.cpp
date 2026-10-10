// prefinput.cpp — see prefinput.h.

#include "prefinput.h"

#include "buttonchordedit.h"
#include "wheelchordedit.h"
#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"   // input::registry()
#include "input/keymapprofile.h"     // bundled / saved keymap layouts

#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>       // conflict tint: QKeySequenceEdit paints through an internal QLineEdit
#include <QToolButton>
#include <QComboBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>
#include <QMap>
#include <QHash>

#include <algorithm>

// Out-of-line anchor for the InputPrefsStore vtable (exported polymorphic base).
input::InputPrefsStore::~InputPrefsStore() = default;

PrefInput::PrefInput(input::InputPrefsStore& store, QWidget* parent)
    : QWidget(parent), store_(store)
{
    build();
    updateFromRegistry();
}

input::Chord PrefInput::rowChord(const Row& r) const
{
    if (r.edit)       return input::chordFromKeySequence(r.edit->keySequence());
    if (r.buttonEdit) return r.buttonEdit->chord();
    if (r.wheelEdit)  return r.wheelEdit->chord();
    return r.defaultChord;   // read-only rows keep their default
}

void PrefInput::build()
{
    using namespace input;
    const BindingRegistry& reg = registry();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto* body = new QWidget(scroll);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(2);

    // ── Keymap layout (profile) bar ──────────────────────────────────────────
    // Load a built-in or saved layout into the rows (Apply), or capture the current rows as a
    // new user layout (Save As).  Everything here is transactional with the bindings below —
    // nothing persists until the dialog's Apply/OK (commitToRegistry -> commitProfiles).
    {
        auto* bar = new QHBoxLayout;
        bar->addWidget(new QLabel(tr("Keymap layout:"), body));
        profileCombo_ = new QComboBox(body);
        profileCombo_->setMinimumWidth(200);
        profileCombo_->setToolTip(tr("Built-in layouts are read-only; your saved layouts can be "
                                     "renamed or deleted."));
        bar->addWidget(profileCombo_);
        applyProfileBtn_ = new QToolButton(body); applyProfileBtn_->setText(tr("Apply"));
        applyProfileBtn_->setToolTip(tr("Load the selected layout into the fields below."));
        saveAsBtn_ = new QToolButton(body);       saveAsBtn_->setText(tr("Save As…"));
        saveAsBtn_->setToolTip(tr("Save the current fields as a new keymap layout."));
        renameBtn_ = new QToolButton(body);       renameBtn_->setText(tr("Rename"));
        deleteBtn_ = new QToolButton(body);       deleteBtn_->setText(tr("Delete"));
        resetAllBtn_ = new QToolButton(body);     resetAllBtn_->setText(tr("Reset all to defaults"));
        resetAllBtn_->setToolTip(tr("Clear every custom key and mouse binding and restore the "
                                    "shipped defaults (applied when you press Apply/OK)."));
        bar->addWidget(applyProfileBtn_);
        bar->addWidget(saveAsBtn_);
        bar->addWidget(renameBtn_);
        bar->addWidget(deleteBtn_);
        bar->addStretch(1);
        bar->addWidget(resetAllBtn_);   // far right, apart from the per-layout controls
        v->addLayout(bar);

        connect(profileCombo_, &QComboBox::currentIndexChanged, this,
                [this](int){ updateProfileButtons(); });

        // Reset all: clear every custom binding back to the shipped defaults.  Reuses the same
        // restoreDefaults() the dialog's Defaults button triggers (resets every row + emits
        // changed()); the overrides are actually cleared on the dialog's Apply/OK via
        // commitToRegistry().  Confirmed first, since it discards all the user's rebindings.
        connect(resetAllBtn_, &QToolButton::clicked, this, [this]{
            if (QMessageBox::question(this, tr("Reset Input Bindings"),
                    tr("Reset every key and mouse binding to its shipped default?  Your custom "
                       "bindings will be cleared when you apply."))
                != QMessageBox::Yes)
                return;
            restoreDefaults();
        });

        // Apply: load the selected layout's overrides into the editor rows.
        connect(applyProfileBtn_, &QToolButton::clicked, this, [this]{
            const int i = profileCombo_->currentIndex();
            if (i < 0) return;
            const QString name    = profileCombo_->itemData(i, Qt::UserRole).toString();
            const bool    bundled = profileCombo_->itemData(i, Qt::UserRole + 1).toBool();
            if (bundled) {
                for (const input::KeymapProfile& b : bundled_)
                    if (b.name == name) { applyProfileToRows(b); return; }
            } else {
                auto it = userProfiles_.constFind(name);
                if (it != userProfiles_.constEnd()) applyProfileToRows(it.value());
            }
        });

        // Save As: capture the current rows (the diff from defaults) as a user layout.
        connect(saveAsBtn_, &QToolButton::clicked, this, [this]{
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Save Keymap Layout"),
                tr("Layout name:"), QLineEdit::Normal, QString(), &ok).trimmed();
            if (!ok || name.isEmpty()) return;
            for (const input::KeymapProfile& b : bundled_)
                if (b.name == name) {
                    QMessageBox::warning(this, tr("Save Keymap Layout"),
                        tr("\"%1\" is a built-in layout name; please choose another.").arg(name));
                    return;
                }
            if (userProfiles_.contains(name) &&
                QMessageBox::question(this, tr("Save Keymap Layout"),
                    tr("Replace the existing layout \"%1\"?").arg(name)) != QMessageBox::Yes)
                return;
            userProfiles_.insert(name, captureRowsAsProfile(name));
            rebuildProfileCombo(name);
            Q_EMIT changed();   // persisted on the dialog's Apply/OK
        });

        // Rename / Delete act on user layouts only (the buttons are disabled for built-ins).
        connect(renameBtn_, &QToolButton::clicked, this, [this]{
            const int i = profileCombo_->currentIndex();
            if (i < 0 || profileCombo_->itemData(i, Qt::UserRole + 1).toBool()) return;
            const QString oldName = profileCombo_->itemData(i, Qt::UserRole).toString();
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Rename Keymap Layout"),
                tr("New name:"), QLineEdit::Normal, oldName, &ok).trimmed();
            if (!ok || name.isEmpty() || name == oldName) return;
            for (const input::KeymapProfile& b : bundled_)
                if (b.name == name) {
                    QMessageBox::warning(this, tr("Rename Keymap Layout"),
                        tr("\"%1\" is a built-in layout name; please choose another.").arg(name));
                    return;
                }
            if (userProfiles_.contains(name) &&
                QMessageBox::question(this, tr("Rename Keymap Layout"),
                    tr("Replace the existing layout \"%1\"?").arg(name)) != QMessageBox::Yes)
                return;
            input::KeymapProfile p = userProfiles_.take(oldName);
            p.name = name;
            userProfiles_.insert(name, p);
            rebuildProfileCombo(name);
            Q_EMIT changed();
        });
        connect(deleteBtn_, &QToolButton::clicked, this, [this]{
            const int i = profileCombo_->currentIndex();
            if (i < 0 || profileCombo_->itemData(i, Qt::UserRole + 1).toBool()) return;
            const QString name = profileCombo_->itemData(i, Qt::UserRole).toString();
            if (QMessageBox::question(this, tr("Delete Keymap Layout"),
                    tr("Delete the layout \"%1\"?").arg(name)) != QMessageBox::Yes)
                return;
            userProfiles_.remove(name);
            rebuildProfileCombo();
            Q_EMIT changed();
        });

        auto* sep = new QFrame(body);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        v->addWidget(sep);
    }

    auto* intro = new QLabel(
        tr("Editable keyboard shortcuts and mouse buttons / wheel.  For a key, click the "
           "field and press the new combination; for a mouse button or wheel, choose it and "
           "its modifiers.  Reset restores the shipped default.  Fields that clash within "
           "the same context are highlighted."), body);
    intro->setWordWrap(true);
    v->addWidget(intro);

    // Iterate the registry in scope-registration order; within a scope, group the
    // commands by category (then label).  Headers are drawn for each scope + category
    // so a newly-registered command/scope slots in with no edits here.
    for (const InputScope& s : reg.scopes()) {
        // collect this scope's commands
        QList<const Command*> cmds;
        for (const Command& c : reg.commands())
            if (c.scopeId == s.id) cmds.append(&c);
        if (cmds.isEmpty()) continue;

        std::stable_sort(cmds.begin(), cmds.end(), [](const Command* a, const Command* b){
            if (a->category != b->category) return a->category < b->category;
            return a->label < b->label;
        });

        auto* scopeHdr = new QLabel(tr("Scope: %1").arg(s.id), body);
        QFont f = scopeHdr->font(); f.setBold(true); scopeHdr->setFont(f);
        scopeHdr->setContentsMargins(0, 8, 0, 0);
        v->addWidget(scopeHdr);

        QString lastCategory;
        for (const Command* c : cmds) {
            if (c->category != lastCategory) {
                lastCategory = c->category;
                if (!lastCategory.isEmpty()) {
                    auto* catHdr = new QLabel(QStringLiteral("  ") + lastCategory, body);
                    catHdr->setContentsMargins(0, 4, 0, 0);
                    QFont cf = catHdr->font(); cf.setItalic(true); catHdr->setFont(cf);
                    v->addWidget(catHdr);
                }
            }

            auto* rowLayout = new QHBoxLayout;
            auto* nameLabel = new QLabel(QStringLiteral("    ") + c->label, body);
            nameLabel->setMinimumWidth(240);
            rowLayout->addWidget(nameLabel);
            rowLayout->addStretch(1);

            Row row;
            row.commandId    = c->id;
            row.scopeId      = c->scopeId;
            row.defaultChord = c->defaultChord;

            // Pick the editor by device: a key (or as-yet-unbound) chord gets a
            // QKeySequenceEdit; a button chord gets the ButtonChordEdit; a wheel chord gets
            // the WheelChordEdit; anything else falls back to a read-only label.
            const Device dev = c->defaultChord.device;
            bool editable = true;
            if (c->kind == Kind::Locked) {
                // Modal commands (a live watershed / lasso capture) are shown for
                // discoverability but cannot be rebound — their trigger is fixed by the
                // mode.  Reuse the read-only label path.
                const QString disp = c->defaultChord.displayString();
                auto* fixed = new QLabel(
                    (disp.isEmpty() ? tr("(modal)") : disp) + tr("  (modal — not rebindable)"),
                    body);
                fixed->setEnabled(false);
                rowLayout->addWidget(fixed);
                row.fixed = fixed;
                editable = false;
            } else if (dev == Device::Key || dev == Device::None) {
                auto* edit = new QKeySequenceEdit(body);
                edit->setMaximumWidth(220);
                rowLayout->addWidget(edit);
                row.edit = edit;
                connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence&){
                    recomputeConflicts();
                    Q_EMIT changed();
                });
            } else if (dev == Device::Button) {
                auto* be = new ButtonChordEdit(body);
                rowLayout->addWidget(be);
                row.buttonEdit = be;
                connect(be, &ButtonChordEdit::chordChanged, this, [this]{
                    recomputeConflicts();
                    Q_EMIT changed();
                });
            } else if (dev == Device::Wheel) {
                auto* we = new WheelChordEdit(body);
                rowLayout->addWidget(we);
                row.wheelEdit = we;
                connect(we, &WheelChordEdit::chordChanged, this, [this]{
                    recomputeConflicts();
                    Q_EMIT changed();
                });
            } else {
                auto* fixed = new QLabel(c->defaultChord.displayString()
                                         + tr("  (not editable)"), body);
                fixed->setEnabled(false);
                rowLayout->addWidget(fixed);
                row.fixed = fixed;
                editable = false;
            }

            if (editable) {
                auto* reset = new QToolButton(body);
                reset->setText(tr("Reset"));
                reset->setToolTip(tr("Restore the shipped default for this command."));
                rowLayout->addWidget(reset);
                const int idx = rows_.size();   // the row we are about to append
                connect(reset, &QToolButton::clicked, this, [this, idx]{
                    if (idx < 0 || idx >= rows_.size()) return;
                    Row& rr = rows_[idx];
                    if (rr.edit) {
                        QSignalBlocker block(rr.edit);
                        rr.edit->setKeySequence(input::keySequenceFromChord(rr.defaultChord));
                    } else if (rr.buttonEdit) {
                        rr.buttonEdit->setChord(rr.defaultChord);   // self-blocking
                    } else if (rr.wheelEdit) {
                        rr.wheelEdit->setChord(rr.defaultChord);    // self-blocking
                    }
                    recomputeConflicts();
                    Q_EMIT changed();
                });
            }

            v->addLayout(rowLayout);
            rows_.append(row);
        }
    }

    v->addStretch(1);
    scroll->setWidget(body);
}

void PrefInput::updateFromRegistry()
{
    const input::BindingRegistry& reg = input::registry();
    for (Row& r : rows_) {
        const input::Chord eff = reg.effectiveChord(r.commandId);
        if (r.edit) {
            QSignalBlocker block(r.edit);   // programmatic load must not light Apply
            r.edit->setKeySequence(input::keySequenceFromChord(eff));
        } else if (r.buttonEdit) {
            r.buttonEdit->setChord(eff);    // self-blocking: no chordChanged emitted
        } else if (r.wheelEdit) {
            r.wheelEdit->setChord(eff);     // self-blocking: no chordChanged emitted
        }
    }
    recomputeConflicts();

    // Refresh the keymap-layout picker (built-in presets + the user's saved layouts) each
    // time the dialog opens, so an externally-changed config is reflected.
    loadProfiles();
    rebuildProfileCombo();
}

void PrefInput::commitToRegistry()
{
    input::BindingRegistry& reg = input::registry();

    // Editors -> overrides: store a diff only where the chord differs from default;
    // an edit cleared back to the default (or emptied) drops the override.
    for (const Row& r : rows_) {
        if (!r.edit && !r.buttonEdit && !r.wheelEdit) continue;   // read-only rows: nothing to commit
        const input::Chord c = rowChord(r);
        if (!c.isValid() || c == r.defaultChord) reg.clearOverride(r.commandId);
        else                                     reg.setOverride(r.commandId, c);
    }

    // Persist the diff map (command id -> Chord::toString()).  KlustersApp pushes the
    // QAction shortcuts in applyPreferences() via applyInputOverridesToActions().
    QMap<QString, QString> ov;
    const QList<QPair<QString, input::Chord>> diffs = reg.overrides();
    for (const auto& kv : diffs) ov.insert(kv.first, kv.second.toString());
    store_.setInputBindingOverrides(ov);

    // Persist the user's saved keymap layouts alongside the overrides, so the whole page
    // commits atomically on Apply/OK (and Cancel discards layout edits too).
    commitProfiles();
}

void PrefInput::restoreDefaults()
{
    for (Row& r : rows_) {
        if (r.edit) {
            QSignalBlocker block(r.edit);
            r.edit->setKeySequence(input::keySequenceFromChord(r.defaultChord));
        } else if (r.buttonEdit) {
            r.buttonEdit->setChord(r.defaultChord);   // self-blocking
        } else if (r.wheelEdit) {
            r.wheelEdit->setChord(r.defaultChord);    // self-blocking
        }
    }
    recomputeConflicts();
    Q_EMIT changed();
}

void PrefInput::recomputeConflicts()
{
    // Group editable rows by scope + current chord; a chord shared by >1 command in
    // the SAME scope is a real conflict (one shadows the other).  Cross-scope repeats
    // are legal and not flagged (matches BindingRegistry::conflicts()).
    QHash<QString, QList<int>> byKey;   // "scope\x1Fchord" -> row indices
    for (int i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        if (!r.edit && !r.buttonEdit && !r.wheelEdit) continue;   // read-only rows never conflict
        const input::Chord c = rowChord(r);
        if (!c.isValid()) continue;
        byKey[r.scopeId + QLatin1Char('\x1f') + c.toString()].append(i);
    }

    QList<bool> conflicted(rows_.size(), false);
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it)
        if (it.value().size() > 1)
            for (int i : it.value()) conflicted[i] = true;

    for (int i = 0; i < rows_.size(); ++i) {
        Row& r = rows_[i];
        if (r.edit) {
            // A QKeySequenceEdit displays through an internal QLineEdit that paints over the
            // widget's own background, so a stylesheet on the outer widget alone leaves the
            // visible field uncoloured (the conflict tint never showed).  Tint the internal
            // line-edit too — that is the control the user actually sees.
            const QString sheet = conflicted[i]
                ? QStringLiteral("background:#e2524a;color:white;") : QString();
            r.edit->setStyleSheet(sheet);
            if (QLineEdit* le = r.edit->findChild<QLineEdit*>())
                le->setStyleSheet(sheet);
            r.edit->setToolTip(conflicted[i]
                ? tr("This combination is already used by another command in the same context.")
                : QString());
        } else if (r.buttonEdit) {
            r.buttonEdit->setConflict(conflicted[i]);
        } else if (r.wheelEdit) {
            r.wheelEdit->setConflict(conflicted[i]);
        }
    }
}

void PrefInput::loadProfiles()
{
    // Built-in presets are compiled into the binary (:/keymaps); user layouts come from
    // Configuration as serialized keymap text (name -> text), parsed back here.
    bundled_ = input::bundledKeymaps();
    userProfiles_.clear();
    const QMap<QString, QString> stored = store_.getInputProfiles();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        bool ok = false;
        input::KeymapProfile p = input::parseKeymap(it.value(), &ok);
        p.name = it.key();                       // the stored key is the authoritative name
        userProfiles_.insert(it.key(), p);
    }
}

void PrefInput::rebuildProfileCombo(const QString& select)
{
    if (!profileCombo_) return;
    QSignalBlocker block(profileCombo_);         // repopulating must not fire currentIndexChanged
    profileCombo_->clear();
    // Built-in presets first (marked, read-only), then the user's own layouts.  Each item
    // carries its bare name (UserRole) and an is-built-in flag (UserRole+1).
    for (const input::KeymapProfile& b : bundled_) {
        profileCombo_->addItem(b.name + tr(" (built-in)"));
        const int i = profileCombo_->count() - 1;
        profileCombo_->setItemData(i, b.name, Qt::UserRole);
        profileCombo_->setItemData(i, true,   Qt::UserRole + 1);
    }
    for (auto it = userProfiles_.constBegin(); it != userProfiles_.constEnd(); ++it) {
        profileCombo_->addItem(it.key());
        const int i = profileCombo_->count() - 1;
        profileCombo_->setItemData(i, it.key(), Qt::UserRole);
        profileCombo_->setItemData(i, false,    Qt::UserRole + 1);
    }
    int sel = 0;
    if (!select.isEmpty())
        for (int i = 0; i < profileCombo_->count(); ++i)
            if (profileCombo_->itemData(i, Qt::UserRole).toString() == select) { sel = i; break; }
    if (profileCombo_->count() > 0) profileCombo_->setCurrentIndex(sel);
    updateProfileButtons();
}

void PrefInput::updateProfileButtons()
{
    if (!profileCombo_) return;
    const int  i       = profileCombo_->currentIndex();
    const bool haveSel = (i >= 0);
    const bool isUser  = haveSel && !profileCombo_->itemData(i, Qt::UserRole + 1).toBool();
    if (applyProfileBtn_) applyProfileBtn_->setEnabled(haveSel);
    if (renameBtn_)       renameBtn_->setEnabled(isUser);
    if (deleteBtn_)       deleteBtn_->setEnabled(isUser);
}

void PrefInput::applyProfileToRows(const input::KeymapProfile& p)
{
    // Load the layout into the editors: a command the layout rebinds takes its chord, every
    // other command falls back to its shipped default (so applying a layout fully defines the
    // visible state).  Mirrors restoreDefaults, but to the layout's chords.  Signals blocked
    // on load; the single changed() at the end lights the dialog's Apply.
    for (Row& r : rows_) {
        const input::Chord c = p.bindings.value(r.commandId, r.defaultChord);
        if (r.edit) {
            QSignalBlocker block(r.edit);
            r.edit->setKeySequence(input::keySequenceFromChord(c));
        } else if (r.buttonEdit) {
            r.buttonEdit->setChord(c);   // self-blocking
        } else if (r.wheelEdit) {
            r.wheelEdit->setChord(c);    // self-blocking
        }
    }
    recomputeConflicts();
    Q_EMIT changed();
}

input::KeymapProfile PrefInput::captureRowsAsProfile(const QString& name) const
{
    // Capture the diff from the shipped defaults — exactly what commitToRegistry decides to
    // store: an editor left at (or cleared to) its default contributes no binding.
    input::KeymapProfile p;
    p.name = name;
    for (const Row& r : rows_) {
        if (!r.edit && !r.buttonEdit && !r.wheelEdit) continue;   // read-only rows
        const input::Chord c = rowChord(r);
        if (c.isValid() && !(c == r.defaultChord))
            p.bindings.insert(r.commandId, c);
    }
    return p;
}

void PrefInput::commitProfiles()
{
    QMap<QString, QString> stored;
    for (auto it = userProfiles_.constBegin(); it != userProfiles_.constEnd(); ++it)
        stored.insert(it.key(), input::serializeKeymap(it.value()));
    store_.setInputProfiles(stored);
}
