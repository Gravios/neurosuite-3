#pragma once
// klustersdoc_internal.h — chain-of-custody path helpers shared across the
// klustersdoc.cpp decomposition (klustersdoc.cpp and the klustersdoc_*.cpp TUs).
//
// ── chain-of-custody method helpers ─────────────────────────────────────────
//
// Every per-group artifact is <base>.<type>.<method>.<group> for method in
// {standard,sdiff,stderiv,...}.  The method is read off the .clu anchor at open
// time and pins resolution of all sibling files; there is no preference-guessing
// and no fetIsStderiv/spkIsTransformed inference — method == "stderiv" is the
// single signal for transformed waveforms / stderiv-space features.
//
// These were an anonymous-namespace block in klustersdoc.cpp.  They are lifted
// here as inline functions so the TUs that resolve feature-file paths (the io
// open/save path and the realign block) share ONE definition instead of each
// carrying a private anonymous-namespace copy.

#include <string>
#include <QString>
#include <QMap>
#include <QList>
#include <vector>
#include <neurosuite/core/custody.hpp>
#include <neurosuite/core/stderiv_transform.hpp>
#include "parameteryamlreader.h"
#include "sdiff_pairs.h"

namespace klustersdoc_internal {

// Extract the method token from a per-group path <base>.<type>.<method>.<group>;
// returns "standard" for an untagged legacy name (or anything unparseable).
inline QString featureMethod(const QString& path) {
    const std::string m = neurosuite::custody::methodOf(path.toStdString());
    return m.empty() ? QStringLiteral("standard") : QString::fromStdString(m);
}

// True for the stderiv family, INCLUDING suffixed tokens (stderiv_S3, stderiv_C4).
// A literal == "stderiv" test silently reports a suffixed session as standard, so
// every method check goes through here.
inline bool methodIsStderiv(const QString& m) {
    return neurosuite::custody::isStderivMethod(m.toStdString());
}

// Resolve a per-group artifact through the shared custody policy: method-specific
// types (.clu/.fet/.pca) resolve strictly to <base>.<type>.<method>.<group>;
// shared types (.res, raw .spk) fall back method -> standard -> untagged; the
// single source of truth lives in custody.hpp.
inline QString resolveFeature(const QString& fullBase, const QString& type,
                              const QString& group, const QString& method) {
    const neurosuite::custody::Resolved r =
        neurosuite::custody::resolve(fullBase.toStdString(), type.toStdString(),
                                     group.toInt(), method.toStdString());
    return QString::fromStdString(r.path);
}

// Custom spatial pattern for a group, read from the session's sdiffPairs.
//
// The three re-extraction paths (realign, nudge, re-extract-on-save) each had their
// own copy of this block, and all three called parseSdiffPairs ONLY -- the order-4
// single-partner parser.  A reference-set pattern (order 5, written with '+') does
// not parse as pairs, so `ok` came back false, the override was skipped, and the
// waveforms were rebuilt with all-pairs: silently the wrong spatial operator for a
// stderiv_C5 session.  One place now decides it for all three.
//
// setOff/setMem are the flattened per-channel reference sets (setOff has nChan+1
// entries) in the form neurosuite::core::spatialDeriv takes.
struct SdiffPattern {
    neurosuite::core::SdiffOrder order = neurosuite::core::SdiffOrder::AllPairs;
    std::vector<int> partner;                 // order 4
    std::vector<int> setOff, setMem;          // order 5
    bool applied = false;                     // a pattern was found AND fitted nChan
    QString spec;                             // the raw sdiffPairs text, for logging
    QString problem;                          // non-empty if a spec was present but unusable
};

inline SdiffPattern readSdiffPattern(const QString& parameterFile, const QString& group,
                                     int nChan, int totalNbChannels) {
    SdiffPattern out;
    if (parameterFile.isEmpty()) return out;
    ParameterYamlReader rdr;
    if (!rdr.parseFile(parameterFile)) return out;
    QMap<int, QList<int> >             sgChans;
    QMap<int, QMap<QString, QString> > sgInfo;
    rdr.getSpikeDescription(totalNbChannels, sgChans, sgInfo);
    const QString spec = sgInfo.value(group.toInt()).value(QStringLiteral("sdiffPairs"));
    if (spec.isEmpty()) return out;
    out.spec = spec;

    const QByteArray raw = spec.toUtf8();
    bool ok = false;
    if (sdiffSpecUsesSets(raw.constData())) {          // '+' present => order 5
        const std::vector<std::vector<int> > sets = parseSdiffSets(raw.constData(), &ok);
        if (ok && static_cast<int>(sets.size()) == nChan) {
            out.setOff.reserve(nChan + 1);
            out.setOff.push_back(0);
            for (int c = 0; c < nChan; ++c) {
                for (size_t k = 0; k < sets[c].size(); ++k) out.setMem.push_back(sets[c][k]);
                out.setOff.push_back(static_cast<int>(out.setMem.size()));
            }
            out.order = neurosuite::core::SdiffOrder::CustomCar;
            out.applied = true;
        } else {
            out.problem = QStringLiteral("reference-set pattern did not parse to %1 channels").arg(nChan);
        }
    } else {
        std::vector<int> pm = parseSdiffPairs(raw.constData(), &ok);
        if (ok && static_cast<int>(pm.size()) == nChan) {
            out.partner = std::move(pm);
            out.order = neurosuite::core::SdiffOrder::Custom;
            out.applied = true;
        } else {
            out.problem = QStringLiteral("partner pattern did not parse to %1 channels").arg(nChan);
        }
    }
    return out;
}

// Resolve a SHARED artifact across EVERY method.  Spike times are method-independent:
// there is one .res per group whatever token wrote it, and detection may have run
// under a different method than the .clu being opened (detect at stderiv, sort at
// stderiv_C5).  resolveFeature above walks only method -> standard -> untagged and
// returns a non-existent path in that case.  Use this for .res; .spk and .fet stay
// strict, since falling back across methods there would mix waveform domains.
inline QString resolveFeatureAny(const QString& fullBase, const QString& type,
                                 const QString& group, const QString& method) {
    const neurosuite::custody::Resolved r =
        neurosuite::custody::resolveAny(fullBase.toStdString(), type.toStdString(),
                                        group.toInt(), method.toStdString());
    return QString::fromStdString(r.path);
}

// <base>.<type>.<method>.<group>  ->  <base>  (also handles legacy untagged).
inline QString stripFeatureSuffix(const QString& path, const QString& type) {
    QString b = path.left(path.lastIndexOf(QLatin1Char('.')));   // strip .<group>
    const int lastDot = b.lastIndexOf(QLatin1Char('.'));
    if (lastDot >= 0) {
        const QString seg = b.mid(lastDot + 1);
        if (seg != type && seg != (type + "D"))                  // a method token
            b = b.left(lastDot);
    }
    return b.left(b.lastIndexOf(QLatin1Char('.')));              // strip .<type>
}

}  // namespace klustersdoc_internal
