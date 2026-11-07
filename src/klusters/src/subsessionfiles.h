// subsessionfiles.h -- the file operations of the recluster SUB-SESSION (QtCore only, testable):
// which files of a group a session has, fixed-size record subsets of them, the sub-session's
// names, and its removal.  KlustersDoc::createReclusterSubsession composes them; the layout is the
// one fiber-kit's fiber_subsession.export() writes (docs/pipeline.md "Reclustering a selection").
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace subsession {

/** <base>.<kind>.<g> and <base>.<kind>.<variant>.<g> in `dir` -- the group's files of that kind,
 *  without a stage tag after the group (a tagged `.clu.v.g.stage` is not one).  Absolute paths. */
QStringList groupFiles(const QString& dir, const QString& baseName, const QString& kind, const QString& group);

/** The variant token of such a file ("" when it has none). */
QString variantOf(const QString& path, const QString& baseName, const QString& kind, const QString& group);

/** <tmpBase>.<kind>[.<variant>].<group>[.<tag>] */
QString withVariant(const QString& tmpBase, const QString& kind, const QString& variant, const QString& group,
                    const QString& tag = QString());

/** Copy the fixed-size records `rows1` (1-BASED record numbers, in the order given) of `src` to `dst`:
 *  `header` bytes are copied first, then each record of `recordBytes`.  The source must hold exactly
 *  `nRecords` records (its size is checked).  False, with `err`, on any failure. */
bool copyRecords(const QString& src, const QString& dst, qint64 header, qint64 recordBytes, qint64 nRecords,
                 const QVector<int64_t>& rows1, QString* err);

/** Write a binary .clu (int32 distinct-id count, then int32 ids). */
bool writeClu(const QString& path, const QVector<int32_t>& ids, QString* err);
/** Read one back (the same layout: .clu, .clc). */
bool readClu(const QString& path, QVector<int32_t>* ids, QString* err);

/** Remove every file, link or directory `<tmpBase>.*`; returns how many. */
int removeAll(const QString& tmpBase);

} // namespace subsession
