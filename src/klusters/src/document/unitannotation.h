/* unitannotation.h -- what the curator records about one unit.
 *
 * Nine free-text fields, in the column order of the session YAML's units table
 * and of the clustering's .cli file (unitsidecar.h):
 *
 *   structure          finest anatomical label            CA1
 *   type               putative cell type                 pyramidal
 *   isolationDistance                                     14.2
 *   quality                                               good
 *   notes
 *   region             the named region                   hippocampal formation
 *   division           broad division                     archicortex
 *   atlas              where the terms come from          WHS SD rat v4
 *   regionId           stable ID of the finest term       an atlas or UBERON id
 *
 * The last four were added after the first five; every reader takes a missing
 * one as empty, so older files and tables read unchanged.
 */
#ifndef KLUSTERS_UNITANNOTATION_H
#define KLUSTERS_UNITANNOTATION_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

struct UnitAnnotation {
    QString structure, type, isolationDistance, quality, notes;
    QString region, division, atlas, regionId;

    static constexpr int kFields = 9;

    /** The fields in column order. */
    QStringList fields() const {
        return {structure, type, isolationDistance, quality, notes,
                region, division, atlas, regionId};
    }

    /** From fields in column order; missing trailing ones are empty. */
    static UnitAnnotation fromFields(const QStringList& f) {
        auto at = [&f](int i) { return i < f.size() ? f.at(i) : QString(); };
        UnitAnnotation a;
        a.structure = at(0); a.type = at(1); a.isolationDistance = at(2);
        a.quality = at(3); a.notes = at(4); a.region = at(5);
        a.division = at(6); a.atlas = at(7); a.regionId = at(8);
        return a;
    }

    /** Field names, in column order, for labels ("Structure", ...). */
    static QStringList labels() {
        return {QCoreApplication::translate("UnitAnnotation", "Structure"),
                QCoreApplication::translate("UnitAnnotation", "Type"),
                QCoreApplication::translate("UnitAnnotation", "Isolation distance"),
                QCoreApplication::translate("UnitAnnotation", "Quality"),
                QCoreApplication::translate("UnitAnnotation", "Notes"),
                QCoreApplication::translate("UnitAnnotation", "Region"),
                QCoreApplication::translate("UnitAnnotation", "Division"),
                QCoreApplication::translate("UnitAnnotation", "Atlas"),
                QCoreApplication::translate("UnitAnnotation", "Region ID")};
    }

    bool isEmpty() const {
        for (const QString& s : fields()) if (!s.isEmpty()) return false;
        return true;
    }

    /** The non-empty fields joined with ", " (the palette's text). */
    QString summary() const {
        QStringList parts;
        for (const QString& s : fields()) if (!s.isEmpty()) parts << s;
        return parts.join(QStringLiteral(", "));
    }

    /** "Label: value" for the non-empty fields (the status bar's text). */
    QString labelled() const {
        const QStringList f = fields(), l = labels();
        QStringList parts;
        for (int i = 0; i < kFields; ++i)
            if (!f.at(i).isEmpty()) parts << l.at(i) + QStringLiteral(": ") + f.at(i);
        return parts.join(QStringLiteral(", "));
    }

    /** Each field of @p a joined to this one's with "--" (a merge of units). */
    UnitAnnotation mergedWith(const UnitAnnotation& a) const {
        const QStringList x = fields(), y = a.fields();
        QStringList out;
        for (int i = 0; i < kFields; ++i) out << x.at(i) + QStringLiteral("--") + y.at(i);
        return fromFields(out);
    }

    bool operator==(const UnitAnnotation& o) const { return fields() == o.fields(); }
};

#endif // KLUSTERS_UNITANNOTATION_H
