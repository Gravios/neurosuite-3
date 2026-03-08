/***************************************************************************
 * descriptionyamlwriter.cpp  –  program description YAML serialiser
 ***************************************************************************/
#include "descriptionyamlwriter.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

static QString esc(const QString& s)
{
    QString e = s;
    e.replace(QLatin1Char('"'), QLatin1String("\\\""));
    e.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return QLatin1Char('"') + e + QLatin1Char('"');
}

void DescriptionYamlWriter::setProgramInformation(const ProgramInformation& info)
{
    m_info = info;
}

bool DescriptionYamlWriter::writeTofile(const QString& url)
{
    QFile f(url);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream o(&f);
    o << "program:\n";
    o << "  name: " << esc(m_info.getProgramName()) << '\n';
    o << "  help: " << esc(m_info.getHelp()) << '\n';

    const auto params = m_info.getParameterInformation();
    if (params.isEmpty()) {
        o << "  parameters: []\n";
    } else {
        o << "  parameters:\n";
        // rows are keyed by integer order; QStringList is {name, value, status[, type, default, hidden]}
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const QStringList& row = it.value();
            o << "    - name:   " << esc(row.value(0)) << '\n';
            o << "      value:  " << esc(row.value(1)) << '\n';
            o << "      status: " << esc(row.value(2)) << '\n';
            if (row.size() > 3) o << "      type:    " << esc(row.value(3)) << '\n';
            if (row.size() > 4) o << "      default: " << esc(row.value(4)) << '\n';
            if (row.size() > 5) o << "      hidden:  " << esc(row.value(5)) << '\n';
        }
    }

    f.close();
    return true;
}
