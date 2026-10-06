// prefinput.cpp — see prefinput.h.

#include "prefinput.h"

#include "configuration.h"
#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"   // input::registry()

#include <QKeySequenceEdit>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QSignalBlocker>
#include <QMap>
#include <QHash>

#include <algorithm>

PrefInput::PrefInput(QWidget* parent)
    : QWidget(parent)
{
    build();
    updateFromRegistry();
}

input::Chord PrefInput::rowChord(const Row& r) const
{
    if (r.keyEditable && r.edit)
        return input::chordFromKeySequence(r.edit->keySequence());
    return r.defaultChord;   // non-editable rows keep their default
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

    auto* intro = new QLabel(
        tr("Editable keyboard shortcuts.  Click a field and press the new key "
           "combination; Reset restores the shipped default.  Fields that clash "
           "within the same context are highlighted."), body);
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
            // A key chord (or an as-yet-unbound command) gets an editable field; a
            // button/wheel chord is shown read-only until the button editor lands.
            row.keyEditable  = (c->defaultChord.device == Device::Key
                                || c->defaultChord.device == Device::None);

            if (row.keyEditable) {
                auto* edit = new QKeySequenceEdit(body);
                edit->setMaximumWidth(220);
                rowLayout->addWidget(edit);
                row.edit = edit;
                connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this](const QKeySequence&){
                    recomputeConflicts();
                    Q_EMIT changed();
                });

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
                    }
                    recomputeConflicts();
                    Q_EMIT changed();
                });
            } else {
                auto* fixed = new QLabel(c->defaultChord.displayString()
                                         + tr("  (mouse — not editable yet)"), body);
                fixed->setEnabled(false);
                rowLayout->addWidget(fixed);
                row.fixed = fixed;
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
        if (!r.edit) continue;
        QSignalBlocker block(r.edit);   // programmatic load must not light Apply
        r.edit->setKeySequence(input::keySequenceFromChord(reg.effectiveChord(r.commandId)));
    }
    recomputeConflicts();
}

void PrefInput::commitToRegistry()
{
    input::BindingRegistry& reg = input::registry();

    // Editors -> overrides: store a diff only where the chord differs from default;
    // an edit cleared back to the default (or emptied) drops the override.
    for (const Row& r : rows_) {
        if (!r.keyEditable) continue;
        const input::Chord c = rowChord(r);
        if (!c.isValid() || c == r.defaultChord) reg.clearOverride(r.commandId);
        else                                     reg.setOverride(r.commandId, c);
    }

    // Persist the diff map (command id -> Chord::toString()).  KlustersApp pushes the
    // QAction shortcuts in applyPreferences() via applyInputOverridesToActions().
    QMap<QString, QString> ov;
    const QList<QPair<QString, input::Chord>> diffs = reg.overrides();
    for (const auto& kv : diffs) ov.insert(kv.first, kv.second.toString());
    configuration().setInputBindingOverrides(ov);
}

void PrefInput::restoreDefaults()
{
    for (Row& r : rows_) {
        if (!r.edit) continue;
        QSignalBlocker block(r.edit);
        r.edit->setKeySequence(input::keySequenceFromChord(r.defaultChord));
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
        if (!r.edit) continue;
        const input::Chord c = rowChord(r);
        if (!c.isValid()) continue;
        byKey[r.scopeId + QLatin1Char('\x1f') + c.toString()].append(i);
    }

    QList<bool> conflicted(rows_.size(), false);
    for (auto it = byKey.constBegin(); it != byKey.constEnd(); ++it)
        if (it.value().size() > 1)
            for (int i : it.value()) conflicted[i] = true;

    for (int i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].edit) continue;
        rows_[i].edit->setStyleSheet(
            conflicted[i] ? QStringLiteral("QKeySequenceEdit{background:#e2524a;color:white;}")
                          : QString());
        if (conflicted[i])
            rows_[i].edit->setToolTip(tr("This combination is already used by another "
                                         "command in the same context."));
        else
            rows_[i].edit->setToolTip(QString());
    }
}
