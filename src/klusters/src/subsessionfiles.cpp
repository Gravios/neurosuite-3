#include "subsessionfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>

#include <cstring>
#include <vector>

namespace subsession {

QStringList groupFiles(const QString& dir, const QString& baseName, const QString& kind, const QString& group)
{
    QStringList out;
    const QString prefix = baseName + QLatin1Char('.') + kind + QLatin1Char('.');
    const QStringList names = QDir(dir).entryList(QStringList{prefix + QLatin1Char('*')},
                                                  QDir::Files | QDir::System, QDir::Name);
    for (const QString& n : names) {
        const QStringList toks = n.mid(prefix.size()).split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (toks.isEmpty() || toks.last() != group) continue;        // another group, or a stage tag after it
        out << QDir(dir).absoluteFilePath(n);
    }
    return out;
}

QString variantOf(const QString& path, const QString& baseName, const QString& kind, const QString& group)
{
    const QString n = QFileInfo(path).fileName();
    const QString prefix = baseName + QLatin1Char('.') + kind + QLatin1Char('.');
    QStringList toks = n.mid(prefix.size()).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!toks.isEmpty() && toks.last() == group) toks.removeLast();
    return toks.join(QLatin1Char('.'));
}

QString withVariant(const QString& tmpBase, const QString& kind, const QString& variant, const QString& group,
                    const QString& tag)
{
    QString p = tmpBase + QLatin1Char('.') + kind;
    if (!variant.isEmpty()) p += QLatin1Char('.') + variant;
    p += QLatin1Char('.') + group;
    if (!tag.isEmpty()) p += QLatin1Char('.') + tag;
    return p;
}

bool copyRecords(const QString& src, const QString& dst, qint64 header, qint64 recordBytes, qint64 nRecords,
                 const QVector<int64_t>& rows1, QString* err)
{
    QFile in(src), out(dst);
    if (recordBytes <= 0) { if (err) *err = QObject::tr("%1: record size %2").arg(src).arg(recordBytes); return false; }
    if (!in.open(QIODevice::ReadOnly)) { if (err) *err = QObject::tr("cannot read %1").arg(src); return false; }
    if (in.size() != header + recordBytes * nRecords) {
        if (err) *err = QObject::tr("%1: %2 bytes, not %3 records of %4 bytes").arg(QFileInfo(src).fileName())
                            .arg(in.size()).arg(nRecords).arg(recordBytes);
        return false;
    }
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = QObject::tr("cannot write %1").arg(dst); return false; }
    if (header > 0 && out.write(in.read(header)) != header) { if (err) *err = QObject::tr("cannot write %1").arg(dst); return false; }
    std::vector<char> buf(static_cast<size_t>(recordBytes));
    for (int64_t r1 : rows1) {
        const qint64 r = static_cast<qint64>(r1) - 1;
        if (r < 0 || r >= nRecords) {
            if (err) *err = QObject::tr("%1: record %2 is beyond the %3 records").arg(QFileInfo(src).fileName()).arg(r1).arg(nRecords);
            return false;
        }
        if (!in.seek(header + r * recordBytes) || in.read(buf.data(), recordBytes) != recordBytes) {
            if (err) *err = QObject::tr("%1: cannot read record %2").arg(QFileInfo(src).fileName()).arg(r1);
            return false;
        }
        if (out.write(buf.data(), recordBytes) != recordBytes) { if (err) *err = QObject::tr("cannot write %1").arg(dst); return false; }
    }
    out.close();
    return out.error() == QFile::NoError;
}

bool writeClu(const QString& path, const QVector<int32_t>& ids, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = QObject::tr("cannot write %1").arg(path); return false; }
    QSet<int32_t> distinct;
    for (int32_t id : ids) distinct.insert(id);
    const int32_t n = static_cast<int32_t>(distinct.size());
    f.write(reinterpret_cast<const char*>(&n), 4);
    f.write(reinterpret_cast<const char*>(ids.constData()), 4LL * ids.size());
    f.close();
    return f.error() == QFile::NoError;
}

bool readClu(const QString& path, QVector<int32_t>* ids, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = QObject::tr("cannot read %1").arg(path); return false; }
    const qint64 size = f.size();
    if (size < 4 || (size - 4) % 4) { if (err) *err = QObject::tr("%1: %2 bytes is no binary .clu").arg(QFileInfo(path).fileName()).arg(size); return false; }
    f.seek(4);
    const QByteArray body = f.readAll();
    ids->resize(static_cast<int>(body.size() / 4));
    memcpy(ids->data(), body.constData(), static_cast<size_t>(body.size()));
    return true;
}

int removeAll(const QString& tmpBase)
{
    const QFileInfo fi(tmpBase);
    const QString name = fi.fileName();
    if (name.isEmpty()) return 0;
    QDir dir(fi.absolutePath());
    int n = 0;
    const QStringList pat{name + QLatin1String(".*")};
    for (const QFileInfo& e : dir.entryInfoList(pat, QDir::Files | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot))
        if (QFile::remove(e.absoluteFilePath())) ++n;
    for (const QFileInfo& e : dir.entryInfoList(pat, QDir::Dirs | QDir::NoDotAndDotDot))
        if (!e.isSymLink() && QDir(e.absoluteFilePath()).removeRecursively()) ++n;
    return n;
}

} // namespace subsession
