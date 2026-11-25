#include "subsessionfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>

#include <algorithm>
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

bool scatterFetRows(const QString& src, const QString& dst, qint64 nTotal, const QVector<int64_t>& rows1,
                    const QVector<int64_t>* times, QString* err)
{
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) { if (err) *err = QObject::tr("cannot read %1").arg(src); return false; }
    int32_t nDims = 0;
    if (in.read(reinterpret_cast<char*>(&nDims), 4) != 4 || nDims < 1) {
        if (err) *err = QObject::tr("%1: no .fet header").arg(QFileInfo(src).fileName());
        return false;
    }
    const qint64 rowBytes = 8LL * nDims;
    const qint64 nSub = (in.size() - 4) / rowBytes;
    if ((in.size() - 4) % rowBytes || nSub != rows1.size()) {
        if (err) *err = QObject::tr("%1: %2 rows for %3 sub-session spikes").arg(QFileInfo(src).fileName())
                            .arg(nSub).arg(rows1.size());
        return false;
    }
    if (times && times->size() != nTotal) { if (err) *err = QObject::tr("time column: %1 values for %2 rows").arg(times->size()).arg(nTotal); return false; }
    std::vector<int64_t> table(static_cast<size_t>(nTotal * nDims), 0);
    bool kept = false;
    {
        QFile old(dst);
        if (old.exists() && old.open(QIODevice::ReadOnly)) {
            int32_t d0 = 0;
            if (old.read(reinterpret_cast<char*>(&d0), 4) == 4 && d0 == nDims && old.size() == 4 + rowBytes * nTotal) {
                if (old.read(reinterpret_cast<char*>(table.data()), rowBytes * nTotal) == rowBytes * nTotal) kept = true;
                else std::fill(table.begin(), table.end(), 0);
            }
        }
    }
    if (!kept && times) {
        for (qint64 r = 0; r < nTotal; ++r) table[static_cast<size_t>(r * nDims + nDims - 1)] = (*times)[static_cast<int>(r)];
    }
    std::vector<int64_t> row(static_cast<size_t>(nDims));
    for (int i = 0; i < rows1.size(); ++i) {
        const qint64 r = static_cast<qint64>(rows1[i]) - 1;
        if (r < 0 || r >= nTotal) { if (err) *err = QObject::tr("row %1 is beyond the %2 rows").arg(rows1[i]).arg(nTotal); return false; }
        if (in.read(reinterpret_cast<char*>(row.data()), rowBytes) != rowBytes) {
            if (err) *err = QObject::tr("%1: cannot read row %2").arg(QFileInfo(src).fileName()).arg(i + 1);
            return false;
        }
        std::memcpy(&table[static_cast<size_t>(r * nDims)], row.data(), static_cast<size_t>(rowBytes));
    }
    const QString tmp = dst + QLatin1String(".saving");
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (err) *err = QObject::tr("cannot write %1").arg(dst); return false; }
    out.write(reinterpret_cast<const char*>(&nDims), 4);
    out.write(reinterpret_cast<const char*>(table.data()), rowBytes * nTotal);
    out.close();
    if (out.error() != QFile::NoError) { if (err) *err = QObject::tr("cannot write %1").arg(dst); return false; }
    QFile::remove(dst);
    if (!QFile::rename(tmp, dst)) { if (err) *err = QObject::tr("cannot replace %1").arg(dst); return false; }
    return true;
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
