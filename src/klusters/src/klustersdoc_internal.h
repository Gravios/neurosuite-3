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
#include <neurosuite/core/custody.hpp>

namespace klustersdoc_internal {

// Extract the method token from a per-group path <base>.<type>.<method>.<group>;
// returns "standard" for an untagged legacy name (or anything unparseable).
inline QString featureMethod(const QString& path) {
    const std::string m = neurosuite::custody::methodOf(path.toStdString());
    return m.empty() ? QStringLiteral("standard") : QString::fromStdString(m);
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
