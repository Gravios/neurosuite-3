/* templatedialogs.cpp -- see templatedialogs.h. */
#include "templatedialogs.h"
#include "sessiontemplate.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <yaml-cpp/yaml.h>

#include <stdexcept>

QString sessionTemplatesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/templates");
}

QString templateFileName(const QString& name)
{
    QString base = name.trimmed();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("session");
    return base + QStringLiteral(".template.yaml");
}

namespace {

/** A checkable row per section (Space or a click toggles it), checked when
 *  the section's export or apply default says so. */
void fillSections(QListWidget* list, const QStringList& sections, bool forExport)
{
    list->clear();
    for (const QString& key : sections) {
        const sessiontemplate::SectionInfo info = sessiontemplate::describe(key.toStdString());
        auto* item = new QListWidgetItem(QString::fromStdString(info.label), list);
        item->setData(Qt::UserRole, key);
        QString tip = QString::fromStdString(info.hint);
        tip = tip.isEmpty() ? key : tip + QStringLiteral(" (") + key + QStringLiteral(")");
        item->setToolTip(tip);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        const bool on = forExport ? info.exportDefault : info.applyDefault;
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
}

QStringList checkedSections(const QListWidget* list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            out << list->item(i)->data(Qt::UserRole).toString();
    return out;
}

} // namespace

// ── Export ───────────────────────────────────────────────────────────────────

ExportTemplateDialog::ExportTemplateDialog(const QStringList& sections, const QString& defaultName,
                                           QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export Session as Template"));

    nameEdit = new QLineEdit(defaultName);
    descriptionEdit = new QLineEdit;
    descriptionEdit->setPlaceholderText(tr("e.g. probe, rig, sorting method"));
    sectionList = new QListWidget;
    fillSections(sectionList, sections, true);
    pathEdit = new QLineEdit;
    auto* browseButton = new QPushButton(tr("Browse…"));

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), nameEdit);
    form->addRow(tr("Description:"), descriptionEdit);
    form->addRow(tr("Sections:"), sectionList);
    form->addRow(tr("File:"), pathRow);

    auto* hint = new QLabel(tr("The template holds only the checked sections. Session-specific "
                               "ones (general information, source recording, units) start unchecked."));
    hint->setWordWrap(true);

    buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(nameEdit, &QLineEdit::textEdited, this, &ExportTemplateDialog::nameEdited);
    connect(pathEdit, &QLineEdit::textEdited, this, [this]() { pathFollowsName = false; refresh(); });
    connect(browseButton, &QPushButton::clicked, this, &ExportTemplateDialog::browse);
    connect(sectionList, &QListWidget::itemChanged, this, &ExportTemplateDialog::refresh);

    nameEdited(nameEdit->text());
    resize(520, 480);
}

void ExportTemplateDialog::nameEdited(const QString& text)
{
    // The file follows the name until the user picks a file of their own.
    if (pathFollowsName)
        pathEdit->setText(sessionTemplatesDir() + QLatin1Char('/') + templateFileName(text));
    refresh();
}

void ExportTemplateDialog::browse()
{
    const QString start = pathEdit->text().isEmpty() ? sessionTemplatesDir() : pathEdit->text();
    QDir().mkpath(sessionTemplatesDir());
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Template file"), start,
                                                        tr("Templates (*.yaml *.yml)"));
    if (chosen.isEmpty()) return;
    pathFollowsName = false;
    pathEdit->setText(chosen);
    refresh();
}

void ExportTemplateDialog::refresh()
{
    buttons->button(QDialogButtonBox::Save)->setEnabled(
        !nameEdit->text().trimmed().isEmpty() && !pathEdit->text().trimmed().isEmpty()
        && !checkedSections(sectionList).isEmpty());
}

QString ExportTemplateDialog::name() const { return nameEdit->text().trimmed(); }
QString ExportTemplateDialog::description() const { return descriptionEdit->text().trimmed(); }
QStringList ExportTemplateDialog::sections() const { return checkedSections(sectionList); }
QString ExportTemplateDialog::path() const { return pathEdit->text().trimmed(); }

// ── Apply ────────────────────────────────────────────────────────────────────

ApplyTemplateDialog::ApplyTemplateDialog(int sessionChannels, QWidget* parent)
    : QDialog(parent), sessionChannels(sessionChannels)
{
    setWindowTitle(tr("Apply Template to Session"));

    templateList = new QListWidget;
    auto* browseButton = new QPushButton(tr("Other File…"));
    sectionList = new QListWidget;
    note = new QLabel;
    note->setWordWrap(true);

    auto* folder = new QLabel(tr("Templates in %1:").arg(QDir::toNativeSeparators(sessionTemplatesDir())));
    folder->setWordWrap(true);
    auto* templateRow = new QHBoxLayout;
    templateRow->addWidget(templateList, 1);
    auto* side = new QVBoxLayout;
    side->addWidget(browseButton);
    side->addStretch(1);
    templateRow->addLayout(side);

    auto* hint = new QLabel(tr("Each checked section replaces the session's; the others are kept. "
                               "The session's own recording setup (acquisition, files, video, probes) "
                               "and its units start unchecked. Nothing is written until you save."));
    hint->setWordWrap(true);

    buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(folder);
    layout->addLayout(templateRow, 1);
    layout->addWidget(new QLabel(tr("Sections to apply:")));
    layout->addWidget(sectionList, 1);
    layout->addWidget(note);
    layout->addWidget(hint);
    layout->addWidget(buttons);

    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, &ApplyTemplateDialog::browse);
    connect(templateList, &QListWidget::currentItemChanged, this, &ApplyTemplateDialog::templateChosen);
    connect(sectionList, &QListWidget::itemChanged, this, &ApplyTemplateDialog::refresh);

    const QDir dir(sessionTemplatesDir());
    const QStringList files = dir.entryList({QStringLiteral("*.yaml"), QStringLiteral("*.yml")},
                                            QDir::Files, QDir::Name);
    for (const QString& f : files) addTemplate(dir.filePath(f), false);
    if (templateList->count() > 0) templateList->setCurrentRow(0);
    else note->setText(tr("No templates yet: export one with File ▸ Export Session as Template, "
                          "or choose a file with Other File…"));
    refresh();
    resize(560, 560);
}

void ApplyTemplateDialog::addTemplate(const QString& path, bool select)
{
    // Listed by the name in its template: block; a plain session file (no
    // block) can be used as a template too, under its file name.
    QString name = QFileInfo(path).completeBaseName();
    QString description;
    try {
        const YAML::Node doc = YAML::LoadFile(path.toStdString());
        const sessiontemplate::Meta meta = sessiontemplate::metaOf(doc);
        if (!meta.name.empty()) name = QString::fromStdString(meta.name);
        description = QString::fromStdString(meta.description);
    } catch (const std::exception&) {
        description = tr("(cannot be read)");
    }
    auto* item = new QListWidgetItem(description.isEmpty() ? name : name + QStringLiteral(" — ") + description,
                                     templateList);
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, name);
    item->setToolTip(QDir::toNativeSeparators(path));
    if (select) templateList->setCurrentItem(item);
}

void ApplyTemplateDialog::templateChosen()
{
    sectionList->clear();
    chosenPath.clear();
    chosenName.clear();
    templateChannels = -1;
    QListWidgetItem* item = templateList->currentItem();
    if (!item) { refresh(); return; }
    const QString path = item->data(Qt::UserRole).toString();
    try {
        const YAML::Node doc = YAML::LoadFile(path.toStdString());
        if (!doc.IsMap()) throw std::runtime_error("not a YAML mapping");
        QStringList sections;
        for (const std::string& s : sessiontemplate::sectionsOf(doc)) sections << QString::fromStdString(s);
        const QSignalBlocker blocker(sectionList);
        fillSections(sectionList, sections, false);
        chosenPath = path;
        chosenName = item->data(Qt::UserRole + 1).toString();
        templateChannels = sessiontemplate::channelCount(doc);
        note->clear();
    } catch (const std::exception& e) {
        note->setText(tr("%1 cannot be read: %2").arg(QDir::toNativeSeparators(path),
                                                      QString::fromLocal8Bit(e.what())));
    }
    refresh();
}

void ApplyTemplateDialog::browse()
{
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Template file"), sessionTemplatesDir(),
                                                        tr("Templates (*.yaml *.yml);;All files (*)"));
    if (!chosen.isEmpty()) addTemplate(chosen, true);
}

void ApplyTemplateDialog::refresh()
{
    const QStringList chosen = sections();
    buttons->button(QDialogButtonBox::Apply)->setEnabled(!chosenPath.isEmpty() && !chosen.isEmpty());
    if (chosenPath.isEmpty()) return;
    // Groups, display settings and plugin parameters name channels: made for
    // another channel count they do not fit this session.
    const bool channelSensitive = chosen.contains(QStringLiteral("anatomicalDescription"))
                               || chosen.contains(QStringLiteral("spikeDetection"))
                               || chosen.contains(QStringLiteral("neuroscope"))
                               || chosen.contains(QStringLiteral("probes"));
    const bool takesAcquisition = chosen.contains(QStringLiteral("acquisitionSystem"));
    if (channelSensitive && !takesAcquisition && templateChannels >= 0 && sessionChannels >= 0
            && templateChannels != sessionChannels)
        note->setText(tr("The template was made for %1 channels; this session has %2. "
                         "Its groups may name channels this session does not have.")
                      .arg(templateChannels).arg(sessionChannels));
    else
        note->clear();
}

QStringList ApplyTemplateDialog::sections() const { return checkedSections(sectionList); }
