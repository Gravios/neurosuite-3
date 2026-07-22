/***************************************************************************
 * pipelinedesignerpage.cpp
 ***************************************************************************/
#include "pipelinedesignerpage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

// ═══════════════════════════════════════════════════════════════════════════
// Script definitions
// ═══════════════════════════════════════════════════════════════════════════

const QVector<NdmScriptDef>& ndmScriptDefs()
{
    static const QVector<NdmScriptDef> s_defs = {
        // ── Orchestrator (root) ────────────────────────────────────────────
        // ndm_start is the pipeline entry point.  When the graph is exported
        // to a session YAML, ndm_start is always the first entry in the
        // programs: list; running `ndm_start session` then iterates the rest
        // of the list in order (graph topology), dispatching each plugin.
        // Its parameters are the legacy ndm_start orchestration flags —
        // historically the dispatch gate; under the graph model they survive
        // as a backwards-compat hint that ndm_start can fall through to its
        // pre-graph behaviour when no orchestrator node is present.
        { "ndm_start", "ndm_start ★", "Pipeline orchestrator (root node)", "orchestrator", "",
          {{ "suffixes",      "nlx",   "Mandatory" },
           { "log",           "false", "Optional"  },
           { "wideband",      "true",  "Optional"  },
           { "events",        "true",  "Optional"  },
           { "video",         "true",  "Optional"  },
           { "concatenation", "true",  "Optional"  },
           { "spikes",        "true",  "Optional"  },
           { "lfp",           "true",  "Optional"  },
           { "clean",         "false", "Optional"  }} },
        // ── Conversion ────────────────────────────────────────────────────
        { "ndm_ncs2dat", "NCS → DAT", "Neuralynx .ncs to .dat", "conversion", "wideband",
          {{ "reverse","false","Optional" }, { "suffixes","nlx","Mandatory" }} },
        { "ndm_smr2dat", "SMR → DAT", "CED .smr to .dat", "conversion", "wideband",
          {{ "reverse","true","Optional" }, { "suffixes","","Mandatory" }, { "channelsExcluded","","Mandatory" }} },
        { "ndm_aom2dat", "AOM → DAT", "AlphaOmega .mat to .dat", "conversion", "wideband",
          {{ "matFile","","Mandatory" }, { "chunkSamples","10000000","Optional" }} },
        // ── Preparation ───────────────────────────────────────────────────
        { "ndm_extractchannels", "Extract Channels", "Strip channels from .dat", "preparation", "wideband",
          {{ "nChannels","","Mandatory" }, { "channels","","Mandatory" }} },
        { "ndm_mergedat", "Merge DATs", "Merge multiple .dat files", "preparation", "wideband",
          {{ "suffixes","","Mandatory" }, { "nChannels","","Mandatory" }} },
        { "ndm_resample", "Resample", "Resample to common rate", "preparation", "wideband",
          {{ "suffixes","","Mandatory" }, { "samplingRates","","Mandatory" }, { "nChannels","","Mandatory" }} },
        { "ndm_concatenate", "Concatenate", "Concatenate sessions", "preparation", "concatenation",
          {{ "spotsSamplingRate","50","Mandatory" }} },
        { "ndm_hipass", "High-pass Filter", "Median filter → .fil", "preparation", "spikes",
          {{ "windowHalfLength","16","Mandatory" }, { "chunkSize","134217728","Mandatory" }} },
        { "ndm_denoiseuniform", "Denoise Uniform", "Remove uniform-noise events", "preparation", "spikes",
          {{ "uniformityThreshold","0.30","Optional" }, { "removeFlat","1","Optional" } , { "dryRun","0","Optional" }} },
        { "ndm_spikecleaner", "Spike Cleaner", "Remove dropout/flat waveforms", "preparation", "spikes",
          {{ "removeFlatChannels","1","Optional" }, { "dryRun","0","Optional" }} },
        // ── Grouping ──────────────────────────────────────────────────────
        { "ndm_setupgroups", "Setup Groups", "Build groups from probe file", "grouping", "spikes",
          {{ "nSamples","52","Optional" }, { "peakSampleIndex","26","Optional" },
           { "nFeatures","3","Optional" }, { "probeLibrary","","Optional" }} },
        { "ndm_spikegrouper", "Spike Grouper", "Auto-discover channel groups", "grouping", "spikes",
          {{ "thresholdFactor","3.0","Mandatory" }, { "refractoryMs","1.0","Mandatory" },
           { "windowSec","60","Mandatory" }, { "maxSubGroups","16","Mandatory" }} },
        // ── Detection ─────────────────────────────────────────────────────
        { "ndm_detectspikes", "Detect Spikes", ".fil → .res (threshold detection)", "detection", "spikes",
          {{ "thresholdFactor","1.5","Mandatory" }, { "refractoryPeriod","16","Mandatory" },
           { "peakSearchLength","32","Mandatory" }, { "start","0","Mandatory" }, { "duration","60","Mandatory" },
           { "inputExtension","fil","Optional" },
           { "method","standard","Optional" }, { "methodOrder","3","Optional" }} },
        // Extraction is a second pass: it reads the .res written by
        // ndm_detectspikes, so it carries no threshold/window parameters.
        { "ndm_extractspikes", "Extract Spikes", ".res + .fil → .spk (after Detect Spikes)", "detection", "spikes",
          {{ "inputExtension","fil","Optional" },
           { "method","standard","Optional" }, { "methodOrder","3","Optional" }} },
        { "ndm_extractspikes_sdiff", "Extract Spikes (SDiff)", ".fil → .spk (spatial diff detection)", "detection", "spikes",
          {{ "thresholdFactor","1.5","Mandatory" }, { "refractoryPeriod","16","Mandatory" },
           { "sdiffOrder","2","Optional" }, { "start","0","Mandatory" }, { "duration","60","Mandatory" }} },
        { "ndm_extractspikes_stderiv", "Extract Spikes (StDeriv)", ".fil → .spk.stderiv (spatial+temporal deriv)", "detection", "spikes",
          {{ "thresholdFactor","1.5","Mandatory" }, { "refractoryPeriod","16","Mandatory" },
           { "sdiffOrder","3","Optional" }, { "start","0","Mandatory" }, { "duration","60","Mandatory" }} },
        // ── Alignment (NEW) ───────────────────────────────────────────────
        { "ndm_alignspikes", "Align Spikes ★", "Peak-align snippets before PCA", "alignment", "spikes",
          {{ "maxShift","3","Optional" }, { "minScore","0.0","Optional" }, { "method","standard","Optional" }} },
        // ── Features ──────────────────────────────────────────────────────
        { "ndm_pca", "PCA", ".spk → .fet", "features", "spikes",
          {{ "before","12","Mandatory" }, { "after","12","Mandatory" }, { "extra","false","Mandatory" },
           { "method","standard","Optional" }, { "methodOrder","3","Optional" }} },
        { "ndm_refeaturize", "Re-featurize", ".spk + .pca → .fet (no PCA refit)", "features", "spikes",
          {{ "extra","false","Optional" },
           { "method","standard","Optional" }, { "methodOrder","3","Optional" }} },
        { "ndm_pca_stderiv", "PCA (StDeriv)", ".spk + stderiv transform → .fet", "features", "spikes",
          {{ "before","12","Mandatory" }, { "after","12","Mandatory" },
           { "extra","false","Optional" }, { "sdiffOrder","3","Optional" }} },
        // ── Sorting ───────────────────────────────────────────────────────
        { "ndm_klustakwik", "KiloKlustaKwik", ".fet → .clu (CEM clustering)", "sorting", "spikes",
          {{ "minClusters","2","Optional" }, { "maxClusters","200","Optional" },
           { "maxPossibleClusters","500","Optional" }, { "nRuns","5","Optional" },
           { "chunkMinutes","9","Optional" }, { "chunkOverlapMinutes","3","Optional" },
           { "chunkPreseedFraction","0.1","Optional" }, { "mergeThresh","42.0","Optional" },
           { "timeMergeIter","100","Optional" }, { "maxIter","500","Optional" },
           { "templateMatchScore","0.88","Optional" }, { "crossChunkTemplateScore","0.80","Optional" },
           { "penaltyMix","0","Optional" }, { "method","standard","Optional" }} },
        // ── Post-sort ─────────────────────────────────────────────────────
        { "ndm_reextractspikes", "Reextract Spikes", "2nd-pass detection + shadow clustering", "postprocess", "spikes",
          {{ "reextractThresholdFactor","0.75","Optional" }, { "reextractMinClusterSize","50","Optional" },
           { "reextractChi2","0.999","Optional" }, { "reextractAutoSubcluster","0","Optional" }} },
        { "ndm_subcluster_unmatched", "Subcluster Unmatched", "Re-cluster the unmatched bin", "postprocess", "spikes",
          {{ "minClusters","2","Optional" }, { "maxClusters","100","Optional" }, { "penaltyMix","0.25","Optional" }} },
        { "ndm_stripdat", "Strip DAT", "Remove sorted spikes from .dat", "postprocess", "spikes",
          {{ "redetectSpikes","no","Optional" }, { "subtractionMode","raw","Optional" }, { "autoStrip","true","Optional" }} },
        // ── Analysis ──────────────────────────────────────────────────────
        { "ndm_estimatedrift", "Estimate Drift", "Compute probe drift from sorted units", "analysis", "",
          {{ "windowSec","60","Optional" }, { "minUnits","3","Optional" }, { "minSpikes","20","Optional" }} },
        { "ndm_applydrift", "Apply Drift", "Generate drift-adaptive chunk files", "analysis", "",
          {{ "threshUm","5.0","Optional" }, { "runKlustaKwik","false","Optional" }, { "method","standard","Optional" }} },
        { "ndm_decomposecollisions", "Decompose Collisions", "Separate collision waveforms", "analysis", "",
          {{ "maxShiftSamp","10","Optional" }, { "corrThreshold","0.85","Optional" },
           { "residualThreshold","0.25","Optional" }, { "minSnrRms","4.0","Optional" }, { "method","standard","Optional" }} },
        // ── LFP ───────────────────────────────────────────────────────────
        { "ndm_lfp", "LFP", ".dat → .lfp (downsampled)", "lfp", "lfp",
          {{ "samplingRate","1250","Mandatory" }, { "subtractSpikes","auto","Optional" }} },
        // ── Output ────────────────────────────────────────────────────────
        { "ndm_clean", "Clean", "Remove intermediate files", "output", "",
          {{ "hipass","true","Optional" }, { "wideband","true","Optional" }} },
    };
    return s_defs;
}

const NdmScriptDef* ndmScriptDef(const QString& type)
{
    for (const NdmScriptDef& d : ndmScriptDefs())
        if (d.type == type) return &d;
    return nullptr;
}

// Sticky-root identifier.  ndm_start is the pipeline entry point: always
// present in the graph, always position 0 of the exported program list,
// undeletable, and rejects incoming edges.
static const QString kRootType = QStringLiteral("ndm_start");

/** Returns the index of the ndm_start node in @p nodes, or -1 if absent. */
static int rootIndexIn(const QList<PipelineNode>& nodes)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes[i].type == kRootType) return i;
    }
    return -1;
}

const QMap<QString, CategoryStyle>& categoryStyles()
{
    static const QMap<QString, CategoryStyle> s_styles {
        // Orchestrator — gold accent, distinct from any other category, so the
        // ndm_start root node reads as the entry point at a glance.
        { "orchestrator",{ "Orchestrator",QColor(0x2a,0x1d,0x05), QColor(0xfa,0xc1,0x5c) } },
        { "conversion",  { "Conversion",  QColor(0x1e,0x1a,0x3d), QColor(0x81,0x8c,0xf8) } },
        { "preparation", { "Preparation", QColor(0x0f,0x23,0x3a), QColor(0x60,0xa5,0xfa) } },
        { "grouping",    { "Grouping",    QColor(0x0a,0x28,0x26), QColor(0x2d,0xd4,0xbf) } },
        { "detection",   { "Detection",   QColor(0x0b,0x2e,0x18), QColor(0x4a,0xde,0x80) } },
        { "alignment",   { "Alignment",   QColor(0x1a,0x2a,0x04), QColor(0xa3,0xe6,0x35) } },
        { "features",    { "Features",    QColor(0x2d,0x1f,0x04), QColor(0xfb,0xbf,0x24) } },
        { "sorting",     { "Sorting",     QColor(0x2d,0x12,0x03), QColor(0xfb,0x92,0x3c) } },
        { "postprocess", { "Post-sort",   QColor(0x2d,0x07,0x14), QColor(0xfb,0x71,0x85) } },
        { "analysis",    { "Analysis",    QColor(0x23,0x07,0x28), QColor(0xe8,0x79,0xf9) } },
        { "lfp",         { "LFP",         QColor(0x03,0x1a,0x22), QColor(0x22,0xd3,0xee) } },
        { "output",      { "Output",      QColor(0x18,0x18,0x18), QColor(0x94,0xa3,0xb8) } },
    };
    return s_styles;
}

static CategoryStyle catStyle(const QString& cat)
{
    const auto& m = categoryStyles();
    if (m.contains(cat)) return m[cat];
    return { cat, QColor(0x20,0x25,0x35), QColor(0x64,0x74,0x8b) };
}

// ═══════════════════════════════════════════════════════════════════════════
// PipelineCanvas
// ═══════════════════════════════════════════════════════════════════════════

PipelineCanvas::PipelineCanvas(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true);
    setMinimumSize(400, 300);
    setMouseTracking(true);
    // Dark canvas background
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0d, 0x13, 0x1f));
    setPalette(pal);
    setAutoFillBackground(true);
}

void PipelineCanvas::bind(QList<PipelineNode>* nodes, QList<PipelineEdge>* edges)
{
    this->nodes = nodes;
    this->edges = edges;
    update();
}

void PipelineCanvas::clearAll()
{
    if (!nodes) return;
    nodes->clear();
    edges->clear();
    selNodeId.clear();
    selEdgeKey.clear();
    connecting = false;
    update();
    emit graphModified();
}

void PipelineCanvas::loadPreset(const QVector<QString>& types)
{
    if (!nodes) return;
    nodes->clear();
    edges->clear();
    selNodeId.clear();
    selEdgeKey.clear();
    connecting = false;

    const float startX = 60.f;
    const float startY = 80.f;
    const float stepX  = NW + 60.f;

    // Build the type list with ndm_start prepended (or hoisted if the
    // preset already includes it — defensive against future presets).
    QVector<QString> withRoot;
    withRoot.reserve(types.size() + 1);
    withRoot.append(kRootType);
    for (const QString& t : types) {
        if (t != kRootType) withRoot.append(t);
    }

    for (int i = 0; i < withRoot.size(); ++i) {
        const NdmScriptDef* def = ndmScriptDef(withRoot[i]);
        PipelineNode n;
        n.id      = newId();
        n.type    = withRoot[i];
        n.pos     = QPointF(startX + i * stepX, startY);
        n.enabled = true;
        if (def)
            for (const NdmParamDef& p : def->params)
                n.params.append({ p.name, p.defaultValue });
        nodes->append(n);
    }

    for (int i = 0; i + 1 < nodes->size(); ++i)
        edges->append({ (*nodes)[i].id, (*nodes)[i + 1].id });

    update();
    emit graphModified();
}

void PipelineCanvas::deleteSelected()
{
    if (!nodes) return;
    if (!selNodeId.isEmpty()) {
        // ndm_start is the sticky root.  Silently refuse to delete it —
        // the user can still remove edges or rearrange children, but the
        // entry point is permanent.
        for (const PipelineNode& n : *nodes) {
            if (n.id == selNodeId && n.type == kRootType) {
                return;
            }
        }
        // remove edges touching this node
        edges->removeIf([&](const PipelineEdge& e) {
            return e.from == selNodeId || e.to == selNodeId;
        });
        nodes->removeIf([&](const PipelineNode& n) { return n.id == selNodeId; });
        selNodeId.clear();
        emit selectionCleared();
        emit graphModified();
        update();
        return;
    }
    if (!selEdgeKey.isEmpty()) {
        const QStringList parts = selEdgeKey.split(QChar(0x2192)); // →
        if (parts.size() == 2) {
            edges->removeIf([&](const PipelineEdge& e) {
                return e.from == parts[0] && e.to == parts[1];
            });
        }
        selEdgeKey.clear();
        emit graphModified();
        update();
    }
}

// ── Geometry helpers ─────────────────────────────────────────────────────────

QRectF PipelineCanvas::nodeRect(const PipelineNode& n) const
{
    return QRectF(n.pos.x(), n.pos.y(), NW, NH);
}

QPointF PipelineCanvas::outPort(const PipelineNode& n) const
{
    return QPointF(n.pos.x() + NW, n.pos.y() + NH * 0.5f);
}

QPointF PipelineCanvas::inPort(const PipelineNode& n) const
{
    return QPointF(n.pos.x(), n.pos.y() + NH * 0.5f);
}

bool PipelineCanvas::hitOut(const PipelineNode& n, const QPointF& w) const
{
    return (outPort(n) - w).manhattanLength() <= PR * 2.5f;
}

bool PipelineCanvas::hitIn(const PipelineNode& n, const QPointF& w) const
{
    return (inPort(n) - w).manhattanLength() <= PR * 2.5f;
}

PipelineNode* PipelineCanvas::nodeAt(const QPointF& world)
{
    if (!nodes) return nullptr;
    // iterate reversed so front-most node wins
    for (int i = nodes->size() - 1; i >= 0; --i) {
        if (nodeRect((*nodes)[i]).contains(world))
            return &(*nodes)[i];
    }
    return nullptr;
}

QPainterPath PipelineCanvas::edgePath(const QPointF& p1, const QPointF& p2) const
{
    const float dx = qAbs(float(p2.x() - p1.x())) * 0.55f + 60.f;
    QPainterPath path;
    path.moveTo(p1);
    path.cubicTo(p1 + QPointF(dx, 0), p2 - QPointF(dx, 0), p2);
    return path;
}

QString PipelineCanvas::edgeAt(const QPointF& screen) const
{
    if (!nodes || !edges) return {};
    QPainterPathStroker stroker;
    stroker.setWidth(10.0);
    for (const PipelineEdge& e : *edges) {
        const PipelineNode* fn = nullptr;
        const PipelineNode* tn = nullptr;
        for (const PipelineNode& n : *nodes) {
            if (n.id == e.from) fn = &n;
            if (n.id == e.to)   tn = &n;
        }
        if (!fn || !tn) continue;
        const QPointF p1 = w2s(outPort(*fn));
        const QPointF p2 = w2s(inPort(*tn));
        QPainterPath stroke = stroker.createStroke(edgePath(p1, p2));
        if (stroke.contains(screen))
            return fn->id + QChar(0x2192) + tn->id;
    }
    return {};
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void PipelineCanvas::drawGrid(QPainter& p)
{
    const float step = 40.f;
    const float ox   = float(std::fmod(pan.x(), step));
    const float oy   = float(std::fmod(pan.y(), step));
    const QColor lineColor(0x1a, 0x22, 0x32);
    p.setPen(QPen(lineColor, 0.5));
    for (float x = ox; x < width();  x += step) p.drawLine(QPointF(x,0), QPointF(x,height()));
    for (float y = oy; y < height(); y += step) p.drawLine(QPointF(0,y), QPointF(width(),y));
}

void PipelineCanvas::drawEdge(QPainter& p, const PipelineEdge& e, bool selected)
{
    if (!nodes) return;
    const PipelineNode* fn = nullptr;
    const PipelineNode* tn = nullptr;
    for (const PipelineNode& n : *nodes) {
        if (n.id == e.from) fn = &n;
        if (n.id == e.to)   tn = &n;
    }
    if (!fn || !tn) return;

    const QPointF p1 = w2s(outPort(*fn));
    const QPointF p2 = w2s(inPort(*tn));
    const QPainterPath path = edgePath(p1, p2);

    // Glow on hover/selected
    if (selected) {
        p.setPen(QPen(QColor(0x4a, 0x9e, 0xff, 80), 6, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(path);
    }
    // Main line
    QColor lineColor = selected ? QColor(0x7b, 0xc8, 0xff) : QColor(0x3a, 0x7e, 0xd8, 180);
    p.setPen(QPen(lineColor, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(path);

    // Arrow at endpoint
    const QPointF dir  = path.pointAtPercent(1.0) - path.pointAtPercent(0.98);
    const double  ang  = std::atan2(dir.y(), dir.x());
    const float   alen = 9.f, awid = 5.f;
    QPolygonF arr;
    arr << p2
        << QPointF(p2.x() - alen * std::cos(ang - awid * M_PI / 180.0),
                   p2.y() - alen * std::sin(ang - awid * M_PI / 180.0))
        << QPointF(p2.x() - alen * std::cos(ang + awid * M_PI / 180.0),
                   p2.y() - alen * std::sin(ang + awid * M_PI / 180.0));
    p.setBrush(lineColor);
    p.setPen(Qt::NoPen);
    p.drawPolygon(arr);
    p.setBrush(Qt::NoBrush);
}

void PipelineCanvas::drawNode(QPainter& p, const PipelineNode& n)
{
    const CategoryStyle cs  = catStyle(ndmScriptDef(n.type) ?
                                       ndmScriptDef(n.type)->category : "output");
    const bool    isSel     = (n.id == selNodeId);
    const QRectF  r         = QRectF(w2s(n.pos), QSizeF(NW, NH));

    // Shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawRoundedRect(r.adjusted(3, 4, 3, 4), CR, CR);

    // Body
    QColor bodyColor = n.enabled ? cs.bg : QColor(0x15, 0x1a, 0x28);
    p.setBrush(bodyColor);
    p.setPen(QPen(isSel ? QColor(0x4a, 0x9e, 0xff) :
                  n.enabled ? cs.accent.darker(160) : QColor(0x2a, 0x35, 0x45),
                  isSel ? 2.0 : 1.0));
    p.drawRoundedRect(r, CR, CR);

    // Header stripe
    if (n.enabled) {
        p.setPen(Qt::NoPen);
        p.setBrush(cs.accent);
        QPainterPath stripe;
        stripe.addRoundedRect(QRectF(r.x(), r.y(), NW, HBAR + CR), CR, CR);
        stripe.addRect(QRectF(r.x(), r.y() + CR, NW, HBAR));
        p.drawPath(stripe);
    }

    // Text
    const NdmScriptDef* def = ndmScriptDef(n.type);
    const QString label = def ? def->label : n.type;

    p.setPen(n.enabled ? cs.accent.lighter(130) : QColor(0x55, 0x66, 0x78));
    QFont labFont = p.font();
    labFont.setPointSizeF(8.5);
    labFont.setWeight(QFont::Bold);
    p.setFont(labFont);
    p.drawText(QRectF(r.x() + 12, r.y() + HBAR + 5, NW - 24, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(label, Qt::ElideRight, int(NW - 24)));

    p.setPen(QColor(0x50, 0x65, 0x80));
    QFont subFont = p.font();
    subFont.setPointSizeF(7.0);
    subFont.setWeight(QFont::Normal);
    p.setFont(subFont);
    p.drawText(QRectF(r.x() + 12, r.y() + HBAR + 22, NW - 24, 14),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(n.type, Qt::ElideRight, int(NW - 24)));

    // Ports
    const QColor portFill   = isSel ? cs.accent : cs.accent.darker(200);
    const QColor portBorder = cs.accent;

    const bool isRoot = (n.type == kRootType);

    // Input (left) — suppressed for the root node, which can't have
    // incoming edges by construction.
    if (!isRoot) {
        p.setPen(QPen(portBorder, 1.5));
        p.setBrush(connecting && !connectFrom.isEmpty() ? portFill.lighter(150) : portFill);
        p.drawEllipse(w2s(inPort(n)), PR, PR);
    }

    // Output (right)
    p.setPen(QPen(portBorder, 1.5));
    p.setBrush(connecting && connectFrom == n.id ? cs.accent : portFill);
    p.drawEllipse(w2s(outPort(n)), PR, PR);

    // Root badge — small "ROOT" pill in the top-right corner of the
    // header stripe, drawn in the orchestrator accent on a dark backing.
    if (isRoot) {
        QFont badgeFont = p.font();
        badgeFont.setPointSizeF(6.5);
        badgeFont.setWeight(QFont::Bold);
        QFontMetricsF bfm(badgeFont);
        const QString badgeText = QStringLiteral("★ ROOT");
        const qreal bw = bfm.horizontalAdvance(badgeText) + 10.0;
        const qreal bh = bfm.height() + 2.0;
        const QRectF bRect(r.right() - bw - 6.0, r.top() + 4.0, bw, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRoundedRect(bRect, 4.0, 4.0);
        p.setPen(cs.accent.lighter(120));
        p.setFont(badgeFont);
        p.drawText(bRect, Qt::AlignCenter, badgeText);
    }

    // Disabled overlay — never applied to the root node, which is always
    // active (it's the orchestrator itself).
    if (!n.enabled && !isRoot) {
        p.setBrush(QColor(0, 0, 0, 100));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, CR, CR);
    }
}

void PipelineCanvas::drawPending(QPainter& p)
{
    if (!connecting || connectFrom.isEmpty() || !nodes) return;
    for (const PipelineNode& n : *nodes) {
        if (n.id != connectFrom) continue;
        const QPointF p1 = w2s(outPort(n));
        p.setPen(QPen(QColor(0x4a, 0x9e, 0xff, 180), 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPath(edgePath(p1, connectCursor));
        break;
    }
}

void PipelineCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    drawGrid(p);

    if (!nodes || !edges) return;

    // Edges (behind nodes)
    for (const PipelineEdge& e : *edges) {
        const QString key = e.from + QChar(0x2192) + e.to;
        drawEdge(p, e, key == selEdgeKey);
    }

    drawPending(p);

    // Nodes
    for (const PipelineNode& n : *nodes)
        drawNode(p, n);

    // Hint when empty
    if (nodes->isEmpty()) {
        p.setPen(QColor(0x2a, 0x38, 0x50));
        QFont f = p.font();
        f.setPointSizeF(10);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Drag scripts from the palette\nor load a preset pipeline."));
    }
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void PipelineCanvas::mousePressEvent(QMouseEvent* ev)
{
    if (!nodes) return;
    setFocus();
    const QPointF world = s2w(ev->position());

    if (ev->button() == Qt::MiddleButton ||
        (ev->button() == Qt::RightButton)) {
        panActive = true;
        panStart  = ev->position() - pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (ev->button() == Qt::LeftButton) {
        // Check output ports first
        for (PipelineNode& n : *nodes) {
            if (hitOut(n, world)) {
                connecting   = true;
                connectFrom  = n.id;
                connectCursor = ev->position();
                selNodeId.clear();
                selEdgeKey.clear();
                emit selectionCleared();
                update();
                return;
            }
        }
        // Check input ports (ignore — only drag from output)
        // Check node bodies
        PipelineNode* hit = nodeAt(world);
        if (hit) {
            selNodeId  = hit->id;
            selEdgeKey.clear();
            dragId     = hit->id;
            dragOffset = world - hit->pos;
            emit nodeSelected(hit->id);
            update();
            return;
        }
        // Check edges
        const QString ekey = edgeAt(ev->position());
        if (!ekey.isEmpty()) {
            selEdgeKey = ekey;
            selNodeId.clear();
            emit selectionCleared();
            update();
            return;
        }
        // Deselect
        selNodeId.clear();
        selEdgeKey.clear();
        panActive = true;
        panStart  = ev->position() - pan;
        setCursor(Qt::ClosedHandCursor);
        emit selectionCleared();
        update();
    }
}

void PipelineCanvas::mouseMoveEvent(QMouseEvent* ev)
{
    if (panActive) {
        pan = ev->position() - panStart;
        update();
        return;
    }
    if (!dragId.isEmpty() && nodes) {
        const QPointF world = s2w(ev->position());
        for (PipelineNode& n : *nodes) {
            if (n.id == dragId) {
                n.pos = world - dragOffset;
                n.pos.setX(qMax(0.0, n.pos.x()));
                n.pos.setY(qMax(0.0, n.pos.y()));
                update();
                return;
            }
        }
    }
    if (connecting) {
        connectCursor = ev->position();
        update();
    }
}

void PipelineCanvas::mouseReleaseEvent(QMouseEvent* ev)
{
    setCursor(Qt::ArrowCursor);
    panActive = false;

    if (!dragId.isEmpty()) {
        dragId.clear();
        emit graphModified();
        return;
    }

    if (connecting && ev->button() == Qt::LeftButton && nodes && edges) {
        const QPointF world = s2w(ev->position());
        for (PipelineNode& n : *nodes) {
            if (n.id != connectFrom && hitIn(n, world)) {
                // ndm_start is the entry node — it must have indegree 0
                // so the topological sort always emits it first.  Reject
                // any attempt to connect *into* it.
                if (n.type == kRootType) {
                    break;
                }
                // Prevent duplicate edges
                bool dup = false;
                for (const PipelineEdge& e : *edges)
                    if (e.from == connectFrom && e.to == n.id) { dup = true; break; }
                if (!dup)
                    edges->append({ connectFrom, n.id });
                emit graphModified();
                break;
            }
        }
        connecting  = false;
        connectFrom.clear();
        update();
    }
}

void PipelineCanvas::keyPressEvent(QKeyEvent* ev)
{
    if (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace)
        deleteSelected();
}

void PipelineCanvas::wheelEvent(QWheelEvent* ev)
{
    // Pan vertically/horizontally with scroll wheel
    const float step = (ev->modifiers() & Qt::ShiftModifier) ? 60.f : 40.f;
    pan += (ev->angleDelta().y() > 0) ?
             QPointF(0, step) : QPointF(0, -step);
    update();
}

// ── Drag-and-drop from palette ────────────────────────────────────────────────

void PipelineCanvas::dragEnterEvent(QDragEnterEvent* ev)
{
    if (ev->mimeData()->hasText())
        ev->acceptProposedAction();
}

void PipelineCanvas::dropEvent(QDropEvent* ev)
{
    if (!nodes || !ev->mimeData()->hasText()) return;
    const QString type = ev->mimeData()->text();
    if (!ndmScriptDef(type)) return;

    // Refuse to add a second ndm_start.  The root is auto-inserted by
    // setPrograms / loadPreset; the palette entry for it should be
    // hidden, but if the drop happens anyway (legacy palette, manual
    // mime payload, etc.) we silently ignore it.
    if (type == kRootType && rootIndexIn(*nodes) >= 0) {
        ev->ignore();
        return;
    }

    const NdmScriptDef* def = ndmScriptDef(type);
    const QPointF world = s2w(ev->position());

    PipelineNode n;
    n.id      = newId();
    n.type    = type;
    n.pos     = QPointF(qMax(0.0, world.x() - NW / 2.0),
                         qMax(0.0, world.y() - NH / 2.0));
    n.enabled = true;
    if (def)
        for (const NdmParamDef& p : def->params)
            n.params.append({ p.name, p.defaultValue });

    nodes->append(n);
    selNodeId  = n.id;
    selEdgeKey.clear();
    emit nodeSelected(n.id);
    emit graphModified();
    ev->acceptProposedAction();
    update();
}

// ═══════════════════════════════════════════════════════════════════════════
// Topological sort helper
// ═══════════════════════════════════════════════════════════════════════════

static QList<PipelineNode> topoSort(const QList<PipelineNode>& nodes,
                                    const QList<PipelineEdge>& edges)
{
    QMap<QString,QStringList> adj;
    QMap<QString,int>         indeg;
    for (const PipelineNode& n : nodes) { adj[n.id]; indeg[n.id] = 0; }
    for (const PipelineEdge& e : edges) {
        if (adj.contains(e.from) && indeg.contains(e.to)) {
            adj[e.from].append(e.to);
            indeg[e.to]++;
        }
    }
    QList<QString> queue;
    for (auto it = indeg.begin(); it != indeg.end(); ++it)
        if (it.value() == 0) queue.append(it.key());

    QList<PipelineNode> result;
    QSet<QString> visited;
    while (!queue.isEmpty()) {
        const QString id = queue.takeFirst();
        visited.insert(id);
        for (const PipelineNode& n : nodes)
            if (n.id == id) { result.append(n); break; }
        for (const QString& next : adj[id])
            if (--indeg[next] == 0) queue.append(next);
    }
    // Append disconnected nodes in original order
    for (const PipelineNode& n : nodes)
        if (!visited.contains(n.id)) result.append(n);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// PipelineDesignerPage
// ═══════════════════════════════════════════════════════════════════════════

PipelineDesignerPage::PipelineDesignerPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("PipelineDesignerPage"));

    // ── Outer layout: toolbar + main splitter ─────────────────────────────
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────────────────
    auto* toolbar = new QWidget;
    toolbar->setFixedHeight(40);
    toolbar->setStyleSheet("background:#0d1117;border-bottom:1px solid #1e2736;");
    auto* tbox = new QHBoxLayout(toolbar);
    tbox->setContentsMargins(8, 0, 8, 0);
    tbox->setSpacing(8);

    auto* presetLabel = new QLabel(tr("Preset:"));
    presetLabel->setStyleSheet("color:#6b7280;font-size:11px;");

    presetCombo = new QComboBox;
    presetCombo->setFixedWidth(220);
    presetCombo->setStyleSheet(
        "QComboBox{background:#131e2d;border:1px solid #2a3650;"
        "color:#8899aa;padding:2px 6px;font-size:11px;border-radius:3px;}"
        "QComboBox::drop-down{border:none;}"
        "QComboBox QAbstractItemView{background:#131e2d;color:#8899aa;border:1px solid #2a3650;}");
    presetCombo->addItem(tr("— select preset —"), QVariant());
    presetCombo->addItem(tr("Pipeline A  (raw → align → PCA → KK)"),
        QVariant(QStringList{"ndm_hipass","ndm_detectspikes","ndm_extractspikes","ndm_alignspikes","ndm_pca","ndm_klustakwik"}));
    presetCombo->addItem(tr("Pipeline C  (raw → align → StDeriv PCA → KK)"),
        QVariant(QStringList{"ndm_hipass","ndm_detectspikes","ndm_extractspikes","ndm_alignspikes","ndm_pca_stderiv","ndm_klustakwik"}));
    presetCombo->addItem(tr("Pipeline D  (StDeriv detect → align → PCA → KK)"),
        QVariant(QStringList{"ndm_hipass","ndm_extractspikes_stderiv","ndm_alignspikes","ndm_pca","ndm_klustakwik"}));
    presetCombo->addItem(tr("Full pipeline (D + reextract + drift + LFP)"),
        QVariant(QStringList{"ndm_hipass","ndm_denoiseuniform","ndm_extractspikes_stderiv",
                              "ndm_alignspikes","ndm_pca","ndm_klustakwik",
                              "ndm_reextractspikes","ndm_subcluster_unmatched",
                              "ndm_estimatedrift","ndm_decomposecollisions","ndm_lfp"}));
    connect(presetCombo, QOverload<int>::of(&QComboBox::activated),
            this, &PipelineDesignerPage::onPresetChanged);

    auto* clearBtn = new QPushButton(tr("Clear"));
    clearBtn->setFixedWidth(60);
    clearBtn->setStyleSheet(
        "QPushButton{background:#1e0a0a;border:1px solid #5c1a1a;color:#ef4444;"
        "font-size:11px;padding:3px 8px;border-radius:3px;}"
        "QPushButton:hover{background:#2d1010;}");
    connect(clearBtn, &QPushButton::clicked, this, &PipelineDesignerPage::onClear);

    tbox->addWidget(presetLabel);
    tbox->addWidget(presetCombo);
    tbox->addWidget(clearBtn);
    tbox->addStretch();

    auto* helpLabel = new QLabel(
        tr("Drag nodes from palette · Connect output→input ports · "
           "Click edge to select · Delete removes selection"));
    helpLabel->setStyleSheet("color:#2a3a50;font-size:10px;");
    tbox->addWidget(helpLabel);
    tbox->addSpacing(12);

    // Save / Save As — write the current graph to a `.pipeline` YAML
    // alongside the session.  Save targets `<session>.default.pipeline`;
    // Save As prompts for a name (e.g. "best", "experimental") and
    // writes `<session>.<name>.pipeline`.  The actual filesystem path
    // resolution and prompting happen in ParameterView, which knows
    // doc->url(); we just emit signals.
    auto* saveBtn = new QPushButton(tr("💾 Save"));
    saveBtn->setToolTip(tr("Save pipeline as <session>.default.pipeline (Ctrl+Alt+P)"));
    saveBtn->setStyleSheet(
        "QPushButton{background:#102a18;border:1px solid #2dd4bf;color:#2dd4bf;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#163d20;}");
    connect(saveBtn, &QPushButton::clicked,
            this, &PipelineDesignerPage::savePipelineRequested);
    tbox->addWidget(saveBtn);

    auto* saveAsBtn = new QPushButton(tr("Save As…"));
    saveAsBtn->setToolTip(tr("Save pipeline under a custom name (Ctrl+Alt+Shift+P)"));
    saveAsBtn->setStyleSheet(
        "QPushButton{background:#102a18;border:1px solid #2dd4bf;color:#2dd4bf;"
        "font-size:11px;padding:3px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#163d20;}");
    connect(saveAsBtn, &QPushButton::clicked,
            this, &PipelineDesignerPage::saveAsPipelineRequested);
    tbox->addWidget(saveAsBtn);

    tbox->addSpacing(8);

    applyBtn = new QPushButton(tr("▶  Apply Pipeline"));
    applyBtn->setFixedWidth(150);
    applyBtn->setEnabled(false);
    applyBtn->setStyleSheet(
        "QPushButton{background:#0d2240;border:1px solid #4a9eff;color:#4a9eff;"
        "font-size:11px;font-weight:bold;padding:4px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#143060;}"
        "QPushButton:disabled{background:#0a0f18;border-color:#2a3650;color:#2a3650;}");
    connect(applyBtn, &QPushButton::clicked, this, &PipelineDesignerPage::onApply);
    tbox->addWidget(applyBtn);

    vbox->addWidget(toolbar);

    // ── Main splitter ─────────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(2);
    splitter->setStyleSheet("QSplitter::handle{background:#1e2736;}");

    // ── Palette ───────────────────────────────────────────────────────────
    buildPalette();
    auto* palWrapper = new QWidget;
    palWrapper->setFixedWidth(210);
    palWrapper->setStyleSheet("background:#0d1117;border-right:1px solid #1e2736;");
    auto* palVbox = new QVBoxLayout(palWrapper);
    palVbox->setContentsMargins(0, 0, 0, 0);
    palVbox->setSpacing(0);
    auto* palHeader = new QLabel(tr("  Script Palette"));
    palHeader->setStyleSheet(
        "background:#0d1117;color:#4a5568;font-size:10px;font-weight:bold;"
        "letter-spacing:2px;padding:8px 0 6px;border-bottom:1px solid #1e2736;");
    palVbox->addWidget(palHeader);
    palVbox->addWidget(palette);
    splitter->addWidget(palWrapper);

    // ── Canvas ────────────────────────────────────────────────────────────
    canvas = new PipelineCanvas;
    canvas->bind(&nodes, &edges);
    connect(canvas, &PipelineCanvas::nodeSelected,   this, &PipelineDesignerPage::onNodeSelected);
    connect(canvas, &PipelineCanvas::selectionCleared, this, &PipelineDesignerPage::onSelectionCleared);
    connect(canvas, &PipelineCanvas::graphModified,  this, &PipelineDesignerPage::onGraphModified);
    splitter->addWidget(canvas);

    // ── Inspector ─────────────────────────────────────────────────────────
    buildInspector();
    splitter->addWidget(inspScroll);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({ 210, 600, 260 });

    vbox->addWidget(splitter, 1);
}

void PipelineDesignerPage::buildPalette()
{
    palette = new QListWidget;
    palette->setDragEnabled(true);
    palette->setDragDropMode(QAbstractItemView::DragOnly);
    palette->setStyleSheet(
        "QListWidget{background:#0d1117;border:none;outline:none;}"
        "QListWidget::item{padding:5px 10px 5px 14px;border-left:3px solid transparent;}"
        "QListWidget::item:selected{background:#141c2a;color:#e2e8f0;}"
        "QListWidget::item:hover{background:#101624;}");
    palette->setIconSize(QSize(8, 8));

    // Group by category in the order they appear in the palette
    const QStringList catOrder {
        "conversion","preparation","grouping","detection","alignment",
        "features","sorting","postprocess","analysis","lfp","output"
    };

    // build category → scripts map preserving order
    QMap<QString, QVector<const NdmScriptDef*>> bycat;
    for (const NdmScriptDef& d : ndmScriptDefs())
        bycat[d.category].append(&d);

    for (const QString& cat : catOrder) {
        if (!bycat.contains(cat)) continue;
        const CategoryStyle cs = catStyle(cat);

        // Category separator item
        auto* sepItem = new QListWidgetItem(cs.label.toUpper());
        sepItem->setFlags(Qt::NoItemFlags);
        QFont sepFont = sepItem->font();
        sepFont.setPointSizeF(8.0);
        sepFont.setWeight(QFont::Bold);
        sepItem->setFont(sepFont);
        sepItem->setForeground(cs.accent);
        sepItem->setBackground(QColor(0x0a, 0x0f, 0x1a));
        palette->addItem(sepItem);

        for (const NdmScriptDef* def : bycat[cat]) {
            auto* item = new QListWidgetItem(def->label);
            item->setData(Qt::UserRole, def->type);
            item->setToolTip(def->brief);
            item->setForeground(QColor(0xaa, 0xbb, 0xcc));
            QFont f = item->font();
            f.setPointSizeF(8.5);
            item->setFont(f);
            // Coloured dot icon
            QPixmap dot(8, 8);
            dot.fill(Qt::transparent);
            QPainter dotP(&dot);
            dotP.setRenderHint(QPainter::Antialiasing);
            dotP.setBrush(cs.accent);
            dotP.setPen(Qt::NoPen);
            dotP.drawEllipse(QRectF(0.5, 0.5, 7, 7));
            item->setIcon(QIcon(dot));
            palette->addItem(item);
        }
    }

    // Double-click adds to canvas at a default position
    connect(palette, &QListWidget::itemDoubleClicked,
            this,      &PipelineDesignerPage::onPaletteDoubleClicked);

    // Custom drag: write program type as text mime data
    connect(palette, &QListWidget::itemPressed, this, [this](QListWidgetItem* item) {
        const QString type = item->data(Qt::UserRole).toString();
        if (type.isEmpty()) return;
        auto* drag = new QDrag(palette);
        auto* mime = new QMimeData;
        mime->setText(type);
        drag->setMimeData(mime);
        // Small drag pixmap
        QPixmap px(120, 28);
        px.fill(QColor(0x13, 0x1e, 0x2d));
        QPainter pp(&px);
        pp.setRenderHint(QPainter::Antialiasing);
        pp.setPen(QColor(0x4a, 0x9e, 0xff));
        pp.setFont(palette->font());
        pp.drawText(QRect(6, 4, 108, 20), Qt::AlignLeft | Qt::AlignVCenter, item->text());
        drag->setPixmap(px);
        drag->setHotSpot(QPoint(60, 14));
        drag->exec(Qt::CopyAction);
    });
}

void PipelineDesignerPage::buildInspector()
{
    inspScroll = new QScrollArea;
    inspScroll->setFixedWidth(265);
    inspScroll->setFrameShape(QFrame::NoFrame);
    inspScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspScroll->setWidgetResizable(true);
    inspScroll->setStyleSheet(
        "QScrollArea{background:#0d1117;border-left:1px solid #1e2736;}"
        "QScrollBar:vertical{background:#0d1117;width:6px;border:none;}"
        "QScrollBar::handle:vertical{background:#2a3650;border-radius:3px;}"
        "QScrollBar::add-line,QScrollBar::sub-line{height:0;}");

    inspWidget = new QWidget;
    inspWidget->setStyleSheet("background:#0d1117;");
    inspScroll->setWidget(inspWidget);

    auto* ivbox = new QVBoxLayout(inspWidget);
    ivbox->setContentsMargins(12, 12, 12, 20);
    ivbox->setSpacing(8);

    // Header
    auto* hdrLabel = new QLabel(tr("INSPECTOR"));
    hdrLabel->setStyleSheet(
        "color:#4a5568;font-size:10px;font-weight:bold;letter-spacing:3px;"
        "padding-bottom:4px;border-bottom:1px solid #1e2736;");
    ivbox->addWidget(hdrLabel);

    inspTitle = new QLabel(tr("Select a node to inspect"));
    inspTitle->setWordWrap(true);
    inspTitle->setStyleSheet("color:#6b7280;font-size:11px;padding:8px 0;");
    ivbox->addWidget(inspTitle);

    inspEnabled = new QCheckBox(tr("Enabled"));
    inspEnabled->setVisible(false);
    inspEnabled->setStyleSheet(
        "QCheckBox{color:#a0b0c0;font-size:11px;spacing:6px;}"
        "QCheckBox::indicator{width:14px;height:14px;"
        "border:1px solid #2a3650;border-radius:3px;background:#0a0f18;}"
        "QCheckBox::indicator:checked{background:#4a9eff;border-color:#4a9eff;}");
    connect(inspEnabled, &QCheckBox::toggled, this, &PipelineDesignerPage::onEnabledToggled);
    ivbox->addWidget(inspEnabled);

    // Parameter form area
    inspParamArea = new QWidget;
    inspParamArea->setVisible(false);
    auto* paramVbox = new QVBoxLayout(inspParamArea);
    paramVbox->setContentsMargins(0, 6, 0, 0);
    paramVbox->setSpacing(2);
    auto* paramHdr = new QLabel(tr("PARAMETERS"));
    paramHdr->setStyleSheet(
        "color:#4a5568;font-size:9px;font-weight:bold;letter-spacing:2px;"
        "padding:6px 0 4px;border-top:1px solid #1e2736;");
    paramVbox->addWidget(paramHdr);
    auto* formWidget = new QWidget;
    inspForm = new QFormLayout(formWidget);
    inspForm->setContentsMargins(0, 0, 0, 0);
    inspForm->setSpacing(6);
    inspForm->setLabelAlignment(Qt::AlignLeft);
    paramVbox->addWidget(formWidget);
    ivbox->addWidget(inspParamArea);
    ivbox->addStretch();
}

// ── Public API ────────────────────────────────────────────────────────────────

void PipelineDesignerPage::setPrograms(const QList<ProgramInformation>& programs)
{
    nodes.clear();
    edges.clear();
    inspectedId.clear();
    clearInspector();

    const float startX = 60.f, startY = 80.f, stepX = PipelineCanvas::NW + 60.f;

    // ── When does the YAML carry a pipeline? ─────────────────────────────
    // The session YAML's programs: list is primarily a parameter store —
    // historically every plugin in the toolchain has had an entry there,
    // independent of which subset is actually wired into the pipeline.
    // To distinguish "this is a saved graph" from "this is just a parameter
    // pool", we use a single signal: programs[0].name == "ndm_start".
    //
    //  * Yes → treat the list as a graph in legacy in-YAML form.  Hoist
    //          ndm_start to position 0 (it should already be there) and
    //          load every other entry as a node, chained linearly.
    //  * No  → start with an empty graph containing only the synthesised
    //          ndm_start root.  The user's pipeline lives in a separate
    //          .pipeline file (loaded via loadPipelineFile) or is yet to
    //          be built; the YAML's other programs: entries hold per-plugin
    //          parameter overrides, not graph membership.
    bool yamlCarriesPipeline = false;
    if (!programs.isEmpty() && programs.first().getProgramName() == kRootType) {
        yamlCarriesPipeline = true;
    }

    if (!yamlCarriesPipeline) {
        // Empty graph + synthesised root.  Same code path as
        // clearGraphToRoot() — share the implementation by delegating.
        clearGraphToRoot();
        return;
    }

    // ── Legacy in-YAML pipeline: load every entry, root first ────────────
    QList<const ProgramInformation*> ordered;
    const ProgramInformation* rootProg = nullptr;
    for (const ProgramInformation& p : programs) {
        if (p.getProgramName() == kRootType) {
            rootProg = &p;
            break;
        }
    }
    // (yamlCarriesPipeline guarantees rootProg != nullptr, but keep the
    // synthesise path for safety against a future schema change.)
    ProgramInformation synthesisedRoot;
    if (!rootProg) {
        synthesisedRoot.setProgramName(kRootType);
        QMap<int, QStringList> defParams;
        if (const NdmScriptDef* def = ndmScriptDef(kRootType)) {
            int row = 0;
            for (const NdmParamDef& pd : def->params) {
                defParams[row++] = QStringList{ pd.name, pd.defaultValue, pd.status };
            }
        }
        synthesisedRoot.setParameterInformation(defParams);
        rootProg = &synthesisedRoot;
    }
    ordered.append(rootProg);
    for (const ProgramInformation& p : programs) {
        if (&p == rootProg) continue;
        if (p.getProgramName() == kRootType) continue;
        ordered.append(&p);
    }

    int i = 0;
    for (const ProgramInformation* progPtr : ordered) {
        const ProgramInformation& prog = *progPtr;
        PipelineNode n;
        n.id      = canvas->allocateId();
        n.type    = prog.getProgramName();
        n.pos     = QPointF(startX + i * stepX, startY);
        n.enabled = true;

        // Load params from ProgramInformation (QMap<int,QStringList>{name,value,status})
        const QMap<int,QStringList>& pmap = prog.getParameterInformation();
        for (auto it = pmap.begin(); it != pmap.end(); ++it) {
            const QStringList& row = it.value();
            if (row.size() >= 2)
                n.params.append({ row[0], row[1] });
        }

        // Fall back to defaults for any missing params
        if (const NdmScriptDef* def = ndmScriptDef(n.type)) {
            QSet<QString> have;
            for (const auto& pv : n.params) have.insert(pv.first);
            for (const NdmParamDef& pd : def->params)
                if (!have.contains(pd.name))
                    n.params.append({ pd.name, pd.defaultValue });
        }

        nodes.append(n);
        ++i;
    }

    // Chain everything
    for (int j = 0; j + 1 < nodes.size(); ++j)
        edges.append({ nodes[j].id, nodes[j+1].id });

    if (canvas) canvas->update();
    modified = false;
    refreshApplyState();
}

void PipelineDesignerPage::clearGraphToRoot()
{
    nodes.clear();
    edges.clear();
    inspectedId.clear();
    clearInspector();

    // Synthesise a single root node with default flag values.
    PipelineNode root;
    root.id      = canvas->allocateId();
    root.type    = kRootType;
    root.pos     = QPointF(60.0, 80.0);
    root.enabled = true;
    if (const NdmScriptDef* def = ndmScriptDef(kRootType)) {
        for (const NdmParamDef& pd : def->params) {
            root.params.append({ pd.name, pd.defaultValue });
        }
    }
    nodes.append(root);

    if (canvas) canvas->update();
    modified = false;
    refreshApplyState();
}

QList<ProgramInformation> PipelineDesignerPage::getPrograms() const
{
    const QList<PipelineNode> ordered = topoSort(nodes, edges);
    QList<ProgramInformation> result;
    int rootResultIdx = -1;
    int idx = 0;
    for (const PipelineNode& n : ordered) {
        // ndm_start must always be exported, regardless of its `enabled`
        // flag — it's the orchestrator entry point.
        if (!n.enabled && n.type != kRootType) continue;
        ProgramInformation pi;
        pi.setProgramName(n.type);
        // Build QMap<int,QStringList>
        QMap<int,QStringList> pmap;
        int row = 0;
        // Use definition to know status; fall back to "Optional"
        const NdmScriptDef* def = ndmScriptDef(n.type);
        for (const auto& pv : n.params) {
            QString status = QStringLiteral("Optional");
            if (def) {
                for (const NdmParamDef& pd : def->params)
                    if (pd.name == pv.first) { status = pd.status; break; }
            }
            pmap[row++] = QStringList{ pv.first, pv.second, status };
        }
        pi.setParameterInformation(pmap);
        if (n.type == kRootType) rootResultIdx = idx;
        result.append(pi);
        ++idx;
    }
    // Topo-sort emits ndm_start first when it has indegree 0 (which the
    // canvas enforces by rejecting incoming edges).  Belt-and-braces:
    // hoist it to position 0 of the output anyway, so a hand-edited graph
    // or any future loosening of the constraint can't move it later.
    if (rootResultIdx > 0) {
        ProgramInformation root = result.takeAt(rootResultIdx);
        result.prepend(root);
    }
    return result;
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PipelineDesignerPage::onNodeSelected(const QString& id)
{
    populateInspector(id);
}

void PipelineDesignerPage::onSelectionCleared()
{
    inspectedId.clear();
    clearInspector();
}

void PipelineDesignerPage::onGraphModified()
{
    modified = true;
    refreshApplyState();
    emit graphModified();
}

void PipelineDesignerPage::onParamEdited()
{
    if (inspectedId.isEmpty()) return;
    for (PipelineNode& n : nodes) {
        if (n.id != inspectedId) continue;
        for (int i = 0; i < paramEdits.size() && i < n.params.size(); ++i)
            n.params[i].second = paramEdits[i]->text();
        break;
    }
    onGraphModified();
}

void PipelineDesignerPage::onEnabledToggled(bool checked)
{
    if (inspectedId.isEmpty()) return;
    for (PipelineNode& n : nodes) {
        if (n.id == inspectedId) { n.enabled = checked; break; }
    }
    if (canvas) canvas->update();
    onGraphModified();
}

void PipelineDesignerPage::onApply()
{
    const QList<ProgramInformation> programs = getPrograms();
    modified = false;
    refreshApplyState();
    emit applyRequested(programs);
}

void PipelineDesignerPage::onClear()
{
    if (canvas) canvas->clearAll();
    inspectedId.clear();
    clearInspector();
}

void PipelineDesignerPage::onPresetChanged(int index)
{
    const QVariant data = presetCombo->itemData(index);
    if (!data.isValid()) return;
    const QStringList types = data.toStringList();
    if (types.isEmpty()) return;
    QVector<QString> tv(types.begin(), types.end());
    if (canvas) canvas->loadPreset(tv);
    presetCombo->setCurrentIndex(0); // reset combo after loading
}

void PipelineDesignerPage::onPaletteDoubleClicked(QListWidgetItem* item)
{
    const QString type = item->data(Qt::UserRole).toString();
    if (type.isEmpty() || !canvas) return;

    // Place at a sensible default position (right of last node, or center)
    float x = 80.f, y = 100.f;
    if (!nodes.isEmpty()) {
        float maxX = 0;
        for (const PipelineNode& n : nodes)
            if (float(n.pos.x()) > maxX) maxX = float(n.pos.x());
        x = maxX + PipelineCanvas::NW + 60.f;
    }

    // Manually add node (palette double-click path)
    const NdmScriptDef* def = ndmScriptDef(type);
    PipelineNode n;
    n.id      = canvas->allocateId();
    n.type    = type;
    n.pos     = QPointF(x, y);
    n.enabled = true;
    if (def)
        for (const NdmParamDef& p : def->params)
            n.params.append({ p.name, p.defaultValue });
    nodes.append(n);
    if (canvas->selectedNodeId().isEmpty() && nodes.size() > 1) {
        // auto-connect to the previous last node
        const QString prevId = nodes[nodes.size()-2].id;
        edges.append({ prevId, n.id });
    }
    if (canvas) {
        canvas->selectNode(n.id);
        emit canvas->graphModified();
    }
}

// ── Inspector helpers ─────────────────────────────────────────────────────────

void PipelineDesignerPage::populateInspector(const QString& id)
{
    inspectedId = id;
    PipelineNode* node = nullptr;
    for (PipelineNode& n : nodes)
        if (n.id == id) { node = &n; break; }
    if (!node) { clearInspector(); return; }

    const NdmScriptDef* def = ndmScriptDef(node->type);
    const CategoryStyle cs  = catStyle(def ? def->category : "output");

    inspTitle->setText(
        QStringLiteral("<span style='color:%1;font-weight:bold;font-size:12px;'>%2</span>"
                       "<br/><span style='color:#4a6080;font-size:9px;'>%3</span>"
                       "%4")
        .arg(cs.accent.name())
        .arg(def ? def->label : node->type)
        .arg(node->type)
        .arg(def && !def->brief.isEmpty() ?
             QStringLiteral("<br/><span style='color:#334455;font-size:9px;'>%1</span>").arg(def->brief) :
             QString()));

    inspEnabled->blockSignals(true);
    inspEnabled->setChecked(node->enabled);
    inspEnabled->blockSignals(false);
    // The orchestrator root is never disable-able — toggling it has no
    // semantic meaning (the abstraction layer is always part of the run).
    inspEnabled->setVisible(node->type != kRootType);

    // Rebuild form
    while (inspForm->rowCount() > 0) inspForm->removeRow(0);
    paramEdits.clear();

    const QString editStyle =
        "QLineEdit{background:#0a0f18;border:1px solid #1e2736;color:#8fa8c8;"
        "padding:3px 6px;font-size:11px;font-family:monospace;border-radius:3px;}"
        "QLineEdit:focus{border-color:#4a9eff;}";
    const QString lblStyle = "color:#4a6080;font-size:9px;letter-spacing:1px;";

    for (const auto& pv : node->params) {
        auto* lbl = new QLabel(pv.first.toUpper());
        lbl->setStyleSheet(lblStyle);
        auto* ed = new QLineEdit(pv.second);
        ed->setStyleSheet(editStyle);
        connect(ed, &QLineEdit::editingFinished, this, &PipelineDesignerPage::onParamEdited);
        inspForm->addRow(lbl, ed);
        paramEdits.append(ed);
    }

    if (node->params.isEmpty()) {
        auto* noParam = new QLabel(tr("No configurable parameters"));
        noParam->setStyleSheet("color:#2a3a4a;font-size:10px;font-style:italic;padding:4px 0;");
        inspForm->addRow(noParam);
    }

    inspParamArea->setVisible(true);
}

void PipelineDesignerPage::clearInspector()
{
    inspTitle->setText(tr("Select a node"));
    inspTitle->setStyleSheet("color:#6b7280;font-size:11px;padding:8px 0;");
    inspEnabled->setVisible(false);
    inspParamArea->setVisible(false);
    while (inspForm->rowCount() > 0) inspForm->removeRow(0);
    paramEdits.clear();
}

void PipelineDesignerPage::refreshApplyState()
{
    applyBtn->setEnabled(!nodes.isEmpty());
    applyBtn->setStyleSheet(modified && !nodes.isEmpty() ?
        "QPushButton{background:#143060;border:1px solid #4a9eff;color:#7bc8ff;"
        "font-size:11px;font-weight:bold;padding:4px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#1a3a70;}" :
        "QPushButton{background:#0d2240;border:1px solid #4a9eff;color:#4a9eff;"
        "font-size:11px;font-weight:bold;padding:4px 10px;border-radius:3px;}"
        "QPushButton:hover{background:#143060;}"
        "QPushButton:disabled{background:#0a0f18;border-color:#2a3650;color:#2a3650;}");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline file I/O
// ─────────────────────────────────────────────────────────────────────────────
//
// Pipeline files live alongside the session YAML as `<session>.<name>.pipeline`
// (default name is "default", e.g. session.default.pipeline).  They are
// minimal YAML carrying only the graph — node positions, edges, parameter
// values — independent of the session-wide schema in session.yaml.  This
// lets a user maintain multiple named pipelines per session
// (default / new / best / experimental etc.) and switch between them
// without touching the session YAML's parameter pool.
//
// Schema:
//   nodes:
//     - id: n1
//       type: ndm_start
//       pos: [60, 80]
//       enabled: true
//       params:
//         wideband: 'true'
//         events:   'true'
//         ...
//     - id: n2
//       type: ndm_hipass
//       ...
//   edges:
//     - {from: n1, to: n2}
//
// Implementation note: we hand-roll the writer/reader rather than pulling
// in yaml-cpp because the schema is simple, the bash side already uses
// PyYAML for its own reads, and the format is human-editable enough that
// adding a third parser dependency just for the GUI would be overkill.

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Quote a string for safe YAML scalar output.  Always single-quoted for
// determinism — the values are short flag-like strings (true, false, 0.75)
// so we don't need block-scalar handling.  Escapes embedded single quotes
// per YAML 1.2 §7.3.2.
QString yamlQuote(const QString& s) {
    QString out = s;
    out.replace(QLatin1String("'"), QLatin1String("''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

// Strip a leading/trailing pair of matching quote characters and undo
// the YAML doubled-single-quote escape used by yamlQuote().
QString yamlUnquote(QString s) {
    s = s.trimmed();
    if (s.size() >= 2) {
        const QChar q = s.front();
        if ((q == QLatin1Char('\'') || q == QLatin1Char('"')) && s.back() == q) {
            s = s.mid(1, s.size() - 2);
            if (q == QLatin1Char('\'')) {
                s.replace(QLatin1String("''"), QLatin1String("'"));
            }
        }
    }
    return s;
}

}  // namespace

bool PipelineDesignerPage::savePipelineFile(const QString& path, QString* error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Could not open '%1' for writing: %2")
                        .arg(path, f.errorString());
        }
        return false;
    }
    QTextStream out(&f);
    out << "# neurosuite-3 pipeline file — generated by ndmanager\n";
    out << "# Loadable via File → Load Pipeline.\n";
    out << "#\n";
    out << "# Schema: nodes[id, type, pos, enabled, params{...}] + edges[from,to]\n";
    out << "\n";

    // Nodes ──────────────────────────────────────────────────────────────
    out << "nodes:\n";
    for (const PipelineNode& n : nodes) {
        out << "  - id: "      << n.id   << "\n";
        out << "    type: "    << n.type << "\n";
        out << "    pos: ["    << QString::number(n.pos.x(), 'f', 1)
            << ", "            << QString::number(n.pos.y(), 'f', 1) << "]\n";
        out << "    enabled: " << (n.enabled ? "true" : "false") << "\n";
        if (n.params.isEmpty()) {
            out << "    params: {}\n";
        } else {
            out << "    params:\n";
            for (const auto& pv : n.params) {
                out << "      " << pv.first << ": " << yamlQuote(pv.second) << "\n";
            }
        }
    }

    // Edges ──────────────────────────────────────────────────────────────
    if (edges.isEmpty()) {
        out << "edges: []\n";
    } else {
        out << "edges:\n";
        for (const PipelineEdge& e : edges) {
            out << "  - {from: " << e.from << ", to: " << e.to << "}\n";
        }
    }

    out.flush();
    if (f.error() != QFile::NoError) {
        if (error) *error = QStringLiteral("Write failed: %1").arg(f.errorString());
        return false;
    }
    return true;
}

bool PipelineDesignerPage::loadPipelineFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Could not open '%1' for reading: %2")
                        .arg(path, f.errorString());
        }
        return false;
    }

    // Use Python (already a hard dep of the toolchain) to parse the YAML
    // robustly, including comments and multi-line params blocks.  The
    // helper on stdout emits a flat key=value listing that's trivial to
    // walk in C++.  Falls back to a plain failure if Python is missing.
    QProcess py;
    py.start(QStringLiteral("python3"), QStringList{
        QStringLiteral("-c"),
        QStringLiteral(
            "import sys, yaml\n"
            "doc = yaml.safe_load(open(sys.argv[1])) or {}\n"
            "for n in doc.get('nodes', []) or []:\n"
            "    nid = n.get('id', '')\n"
            "    typ = n.get('type', '')\n"
            "    pos = n.get('pos', [0, 0]) or [0, 0]\n"
            "    en  = bool(n.get('enabled', True))\n"
            "    print(f'NODE\\t{nid}\\t{typ}\\t{pos[0]}\\t{pos[1]}\\t{int(en)}')\n"
            "    for k, v in (n.get('params') or {}).items():\n"
            "        print(f'PARAM\\t{nid}\\t{k}\\t{v}')\n"
            "for e in doc.get('edges', []) or []:\n"
            "    print(f'EDGE\\t{e.get(\"from\",\"\")}\\t{e.get(\"to\",\"\")}')\n"),
        path
    });
    if (!py.waitForFinished(5000)) {
        if (error) *error = QStringLiteral("Pipeline file parser timed out");
        return false;
    }
    if (py.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Failed to parse '%1':\n%2")
                        .arg(path, QString::fromUtf8(py.readAllStandardError()));
        }
        return false;
    }

    // Replace the graph atomically — only mutate nodes/edges if the
    // parse succeeds.
    QList<PipelineNode> newNodes;
    QList<PipelineEdge> newEdges;
    QHash<QString, int> nodeIdx;  // id → index in newNodes
    QStringList unknownTypes;

    const QString output = QString::fromUtf8(py.readAllStandardOutput());
    for (const QString& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.isEmpty()) continue;
        if (parts[0] == QLatin1String("NODE") && parts.size() >= 6) {
            // Drop unknown types with a warning rather than failing the load —
            // this can happen when a pipeline file references a plugin that
            // was renamed or removed since it was saved.
            if (!ndmScriptDef(parts[2])) {
                unknownTypes.append(parts[2]);
                continue;
            }
            PipelineNode n;
            n.id      = parts[1];
            n.type    = parts[2];
            n.pos     = QPointF(parts[3].toDouble(), parts[4].toDouble());
            n.enabled = (parts[5] == QLatin1String("1"));
            nodeIdx.insert(n.id, newNodes.size());
            newNodes.append(n);
        } else if (parts[0] == QLatin1String("PARAM") && parts.size() >= 4) {
            if (!nodeIdx.contains(parts[1])) continue;  // unknown-type drop
            const QString val = yamlUnquote(parts[3]);
            newNodes[nodeIdx.value(parts[1])].params.append({ parts[2], val });
        } else if (parts[0] == QLatin1String("EDGE") && parts.size() >= 3) {
            // Skip edges that reference a dropped (unknown-type) node.
            if (!nodeIdx.contains(parts[1])) continue;
            if (!nodeIdx.contains(parts[2])) continue;
            newEdges.append({ parts[1], parts[2] });
        }
    }

    // Ensure ndm_start is present and at position 0.  A hand-edited pipeline
    // file might have lost the root; we silently re-insert it rather than
    // failing the load.
    int rootIdx = -1;
    for (int i = 0; i < newNodes.size(); ++i) {
        if (newNodes[i].type == kRootType) { rootIdx = i; break; }
    }
    if (rootIdx < 0) {
        PipelineNode root;
        root.id      = QStringLiteral("nroot");
        root.type    = kRootType;
        root.pos     = QPointF(60.0, 80.0);
        root.enabled = true;
        if (const NdmScriptDef* def = ndmScriptDef(kRootType)) {
            for (const NdmParamDef& pd : def->params) {
                root.params.append({ pd.name, pd.defaultValue });
            }
        }
        newNodes.prepend(root);
    } else if (rootIdx > 0) {
        newNodes.move(rootIdx, 0);
    }

    // Apply.
    nodes = newNodes;
    edges = newEdges;
    inspectedId.clear();
    clearInspector();

    if (canvas) canvas->update();
    modified = false;
    refreshApplyState();

    if (error && !unknownTypes.isEmpty()) {
        *error = QStringLiteral("Loaded with %1 unknown plugin type(s) dropped: %2")
                    .arg(unknownTypes.size())
                    .arg(unknownTypes.join(QStringLiteral(", ")));
    }
    return true;
}
