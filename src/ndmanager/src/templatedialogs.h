/* templatedialogs.h -- File > Export Session as Template and File > Apply
 * Template to Session (sessiontemplate.h does the YAML work).
 *
 * Templates are kept in the user's template folder (sessionTemplatesDir()),
 * which the apply dialog lists by name; either dialog also takes any other
 * path.
 */
#ifndef NDMANAGER_TEMPLATEDIALOGS_H
#define NDMANAGER_TEMPLATEDIALOGS_H

#include <QDialog>
#include <QStringList>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

/** The user's template folder: <AppDataLocation>/templates. */
QString sessionTemplatesDir();

/** A file name for a template called @p name: unsafe characters become '_',
 *  and ".template.yaml" is appended. */
QString templateFileName(const QString& name);

/** Asks for the template's name, description, sections and file. */
class ExportTemplateDialog : public QDialog
{
    Q_OBJECT
public:
    /** @p sections: the session's sections (sessiontemplate::sectionsOf). */
    ExportTemplateDialog(const QStringList& sections, const QString& defaultName, QWidget* parent = nullptr);

    QString name() const;
    QString description() const;
    QStringList sections() const;   ///< the checked ones
    QString path() const;

private:
    void nameEdited(const QString& text);
    void browse();
    void refresh();

    QLineEdit* nameEdit;
    QLineEdit* descriptionEdit;
    QListWidget* sectionList;
    QLineEdit* pathEdit;
    QDialogButtonBox* buttons;
    bool pathFollowsName = true;
};

/** Asks for the template and the sections of it to apply. */
class ApplyTemplateDialog : public QDialog
{
    Q_OBJECT
public:
    /** @p sessionChannels: the open session's channel count (-1: unknown),
     *  to warn about groups made for another count. */
    ApplyTemplateDialog(int sessionChannels, QWidget* parent = nullptr);

    QString templatePath() const { return chosenPath; }
    QString templateName() const { return chosenName; }
    QStringList sections() const;   ///< the checked ones

private:
    void addTemplate(const QString& path, bool select);
    void templateChosen();
    void browse();
    void refresh();

    int sessionChannels;
    QListWidget* templateList;
    QListWidget* sectionList;
    QLabel* note;
    QDialogButtonBox* buttons;
    QString chosenPath;
    QString chosenName;
    int templateChannels = -1;
};

#endif // NDMANAGER_TEMPLATEDIALOGS_H
