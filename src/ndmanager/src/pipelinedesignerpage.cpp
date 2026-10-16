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
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QTimer>
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
    // One entry per placeable plugin, in palette order.  Parameters (names,
    // default values, status) come from templates/template.yaml; labels,
    // tooltips and categories from src/ndmanager/tools/pipeline_script_defs.py,
    // which generates this list — edit those and regenerate, not the .inc.
    static const QVector<NdmScriptDef> s_defs = {
#include "pipelinescriptdefs.inc"
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
    // Tab reaches the canvas too (it was click-only), so Delete can remove the
    // selected node or edge from the keyboard.
    setFocusPolicy(Qt::StrongFocus);
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

bool PipelineCanvas::hasNodeId(const QString& id) const
{
    if (!nodes) return false;
    for (const PipelineNode& n : *nodes)
        if (n.id == id) return true;
    return false;
}

QString PipelineCanvas::allocateId()
{
    // The counter alone is not enough: a loaded pipeline file keeps its own
    // ids, which may be ahead of idSeq.  reserveIds() moves the counter past
    // them; the membership check guards any id the counter cannot see
    // (non-numeric ids, ids added behind the canvas's back).
    QString id;
    do {
        id = QStringLiteral("n%1").arg(++idSeq);
    } while (hasNodeId(id));
    return id;
}

void PipelineCanvas::reserveIds()
{
    if (!nodes) return;
    static const QRegularExpression numericId(QStringLiteral("^n(\\d{1,9})$"));
    for (const PipelineNode& n : *nodes) {
        const QRegularExpressionMatch m = numericId.match(n.id);
        if (m.hasMatch())
            idSeq = qMax(idSeq, m.captured(1).toInt());
    }
}

bool PipelineCanvas::wouldCreateCycle(const QString& from, const QString& to) const
{
    if (from == to) return true;
    if (!edges) return false;
    // Does `to` already reach `from`?  Then from→to closes a cycle.
    QSet<QString> seen{ to };
    QStringList stack{ to };
    while (!stack.isEmpty()) {
        const QString cur = stack.takeLast();
        for (const PipelineEdge& e : *edges) {
            if (e.from != cur || seen.contains(e.to)) continue;
            if (e.to == from) return true;
            seen.insert(e.to);
            stack.append(e.to);
        }
    }
    return false;
}

void PipelineCanvas::resetInteraction()
{
    selNodeId.clear();
    selEdgeKey.clear();
    dragId.clear();
    connecting = false;
    connectFrom.clear();
    if (panActive) {
        panActive = false;
        setCursor(Qt::ArrowCursor);
    }
    update();
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
            dragStartPos = hit->pos;
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
        // A click that selects a node is not an edit; only a real move is.
        bool moved = false;
        if (nodes) {
            for (const PipelineNode& n : *nodes)
                if (n.id == dragId) { moved = (n.pos != dragStartPos); break; }
        }
        dragId.clear();
        if (moved) emit graphModified();
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
                    emit edgeRejected(tr("ndm_start is the pipeline root; it cannot "
                                         "have an incoming edge."));
                    break;
                }
                // Prevent duplicate edges
                bool dup = false;
                for (const PipelineEdge& e : *edges)
                    if (e.from == connectFrom && e.to == n.id) { dup = true; break; }
                if (dup) break;
                // A cycle has no run order: the GUI topo sort would append
                // its nodes at the end while ndm_start's would skip them
                // (and everything after them) altogether.
                if (wouldCreateCycle(connectFrom, n.id)) {
                    emit edgeRejected(tr("Edge refused: %1 already leads to the node you "
                                         "dragged from, so this edge would close a cycle.")
                                      .arg(n.type));
                    break;
                }
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
    else
        QWidget::keyPressEvent(ev);         // Tab, shortcuts: not swallowed
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
    // Seed the queue in node (= file) order, not QMap key order: ndm_start's
    // read_pipeline_file seeds Kahn's queue in YAML insertion order, and
    // the two must agree (with QMap order "n10" sorted before "n2").
    QList<QString> queue;
    for (const PipelineNode& n : nodes)
        if (indeg.value(n.id) == 0 && !queue.contains(n.id)) queue.append(n.id);

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
    // alongside the session.  Save targets `<session>.ndm.default.pipeline`;
    // Save As prompts for a name (e.g. "best", "experimental") and
    // writes `<session>.ndm.<name>.pipeline`.  The actual filesystem path
    // resolution and prompting happen in ParameterView, which knows
    // doc->url(); we just emit signals.
    auto* saveBtn = new QPushButton(tr("💾 Save"));
    saveBtn->setToolTip(tr("Save pipeline as <session>.ndm.default.pipeline (Ctrl+Alt+P)"));
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

    // Notice bar: load failures and warnings, refused edges.  Hidden until
    // showNotice() has something to say.
    noticeLabel = new QLabel;
    noticeLabel->setWordWrap(true);
    noticeLabel->setTextFormat(Qt::PlainText);
    noticeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    noticeLabel->setVisible(false);
    vbox->addWidget(noticeLabel);

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
    connect(canvas, &PipelineCanvas::edgeRejected,   this, &PipelineDesignerPage::onEdgeRejected);
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
    if (canvas) canvas->resetInteraction();
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
    applyPending = false;
    unsaved = false;
    refreshApplyState();
}

void PipelineDesignerPage::clearGraphToRoot()
{
    nodes.clear();
    edges.clear();
    if (canvas) canvas->resetInteraction();
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
    applyPending = false;
    unsaved = false;
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
    applyPending = true;
    unsaved = true;
    refreshApplyState();
    emit graphModified();
}

void PipelineDesignerPage::onParamEdited()
{
    if (inspectedId.isEmpty()) return;
    // editingFinished also fires on a plain focus change: only a value
    // that actually changed is an edit.
    bool changed = false;
    for (PipelineNode& n : nodes) {
        if (n.id != inspectedId) continue;
        for (int i = 0; i < paramEdits.size() && i < n.params.size(); ++i) {
            const QString v = paramEdits[i]->text();
            if (n.params[i].second == v) continue;
            n.params[i].second = v;
            n.editedParams.insert(n.params[i].first);
            changed = true;
        }
        break;
    }
    if (changed) onGraphModified();
}

bool PipelineDesignerPage::setProgramParameter(const QString& type, const QString& name,
                                               const QString& value)
{
    bool changed = false, inspected = false;
    for (PipelineNode& n : nodes) {
        if (n.type != type) continue;
        for (auto& pv : n.params) {
            if (pv.first != name || pv.second == value) continue;
            pv.second = value;
            n.editedParams.remove(name);
            changed = true;
            if (n.id == inspectedId) inspected = true;
        }
    }
    if (!changed) return false;
    unsaved = true;
    if (inspected) populateInspector(inspectedId);
    if (canvas) canvas->update();
    emit graphModified();
    return true;
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
    const PipelineParamEdits edited = editedParameterValues();
    applyPending = false;   // still unsaved: Apply does not write the pipeline file
    refreshApplyState();
    emit applyRequested(programs, edited);
    // Those edits are now in the Plugins tab.  Edits on disabled nodes were
    // not exported, so they stay pending for a later Apply.
    for (PipelineNode& n : nodes)
        if (n.enabled || n.type == kRootType) n.editedParams.clear();
}

PipelineParamEdits PipelineDesignerPage::editedParameterValues() const
{
    PipelineParamEdits result;
    for (const PipelineNode& n : topoSort(nodes, edges)) {
        if (!n.enabled && n.type != kRootType) continue;   // as getPrograms()
        if (n.editedParams.isEmpty()) continue;
        QVector<QPair<QString,QString>>& out = result[n.type];
        for (const auto& pv : n.params) {
            if (!n.editedParams.contains(pv.first)) continue;
            bool have = false;
            for (const auto& o : out) if (o.first == pv.first) { have = true; break; }
            if (!have) out.append(pv);
        }
    }
    return result;
}

void PipelineDesignerPage::onClear()
{
    // Back to just the root — never an empty graph: ndm_start must stay
    // present (getPrograms would otherwise export a list without it, and
    // a saved file would lose the orchestrator entry).  Clearing is an
    // edit, so flag it after clearGraphToRoot resets the state.
    clearGraphToRoot();
    onGraphModified();
}

void PipelineDesignerPage::onEdgeRejected(const QString& reason)
{
    showNotice(reason, /*isError=*/false, /*timeoutMs=*/6000);
}

void PipelineDesignerPage::showNotice(const QString& text, bool isError, int timeoutMs)
{
    if (!noticeLabel) return;
    noticeLabel->setText(text);
    noticeLabel->setStyleSheet(isError
        ? "background:#2d1010;color:#fca5a5;border-bottom:1px solid #5c1a1a;"
          "font-size:11px;padding:4px 10px;"
        : "background:#2a1d05;color:#fac15c;border-bottom:1px solid #5c4a1a;"
          "font-size:11px;padding:4px 10px;");
    noticeLabel->setVisible(!text.isEmpty());
    if (timeoutMs > 0 && !text.isEmpty()) {
        QTimer::singleShot(timeoutMs, noticeLabel, [label = noticeLabel, text]() {
            if (label->text() == text) label->setVisible(false);
        });
    }
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
    applyBtn->setStyleSheet(applyPending && !nodes.isEmpty() ?
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
// Pipeline files live alongside the session YAML as `<session>.ndm.<name>.pipeline`
// (default name is "default", e.g. session.ndm.default.pipeline).  They are
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// True for a UTF-16 unit that PyYAML will not read back unchanged inside a
// single-quoted scalar: C0 controls (tab, newline and CR included — a
// newline inside a flow scalar is folded to a space), DEL and the C1 block
// (U+0085 NEL is a YAML 1.1 line break), the U+2028/U+2029 line/paragraph
// separators, BOM and U+FFFE/U+FFFF.  Surrogates are handled by the caller.
bool needsEscape(char16_t c)
{
    return c < 0x20 || (c >= 0x7f && c <= 0x9f) ||
           c == 0x2028 || c == 0x2029 || c == 0xfeff || c >= 0xfffe;
}

// Quote a string as a YAML scalar that PyYAML reads back as exactly the
// same string.  Single-quoted (only ' needs doubling) when every character
// is printable on one line; otherwise double-quoted with escapes, so tabs,
// newlines and control characters survive the round trip.
QString yamlQuote(const QString& s)
{
    bool plainOk = true;
    for (int i = 0; i < s.size() && plainOk; ++i) {
        const QChar c = s.at(i);
        if (c.isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            ++i;                      // a valid pair is a printable astral char
        } else if (c.isSurrogate() || needsEscape(c.unicode())) {
            plainOk = false;
        }
    }
    if (plainOk) {
        QString out = s;
        out.replace(QLatin1String("'"), QLatin1String("''"));
        return QLatin1Char('\'') + out + QLatin1Char('\'');
    }
    QString out;
    out.reserve(s.size() + 8);
    out += QLatin1Char('"');
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        const char16_t u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            out += c;
            out += s.at(++i);
        } else if (u == '\\') { out += QLatin1String("\\\\"); }
        else if (u == '"')    { out += QLatin1String("\\\""); }
        else if (u == '\n')   { out += QLatin1String("\\n"); }
        else if (u == '\t')   { out += QLatin1String("\\t"); }
        else if (u == '\r')   { out += QLatin1String("\\r"); }
        else if (c.isSurrogate() || needsEscape(u)) {
            out += (u < 0x100)
                ? QStringLiteral("\\x%1").arg(uint(u), 2, 16, QLatin1Char('0'))
                : QStringLiteral("\\u%1").arg(uint(u), 4, 16, QLatin1Char('0'));
        } else {
            out += c;
        }
    }
    out += QLatin1Char('"');
    return out;
}

// A node id, plugin type or parameter name: written bare when it is an
// identifier PyYAML reads back as that same string (not a bool / null
// word), otherwise quoted.  Generated ids (n12) and plugin / parameter
// names are always bare, so files look as before.
QString yamlName(const QString& s)
{
    static const QRegularExpression ident(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.\\-]*$"));
    static const QStringList reserved{
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("yes"),
        QStringLiteral("no"),   QStringLiteral("on"),    QStringLiteral("off"),
        QStringLiteral("null"), QStringLiteral("y"),     QStringLiteral("n") };
    if (ident.match(s).hasMatch() && !reserved.contains(s.toLower()))
        return s;
    return yamlQuote(s);
}

// Parser run by loadPipelineFile (python3 -c <this> <file>).  PyYAML is
// already a dependency of the ndm_* scripts.  It walks the composed node
// tree rather than safe_load's Python objects so a parameter value comes
// back as its YAML text — 0.50 stays "0.50", yes stays "yes", ~ becomes ""
// — instead of a Python repr; a list or mapping value becomes flow-style
// YAML text.  id / type / enabled / pos / edges are read the way the bash
// ndm_start reads them (safe_load semantics, e.g. bool(enabled)), so the
// designer shows what ndm_start will run.  The result is one JSON object on
// stdout: {"nodes": [{"id","type","pos":[x,y],"enabled","params":[[k,v]..]}],
//          "edges": [[from,to]..]} — JSON is lossless for any string, unlike
// the old tab-separated lines.
const char* const kPipelineParser = R"PY(
import sys, json, math, yaml

def main(path):
    with open(path, encoding='utf-8') as f:
        loader = yaml.SafeLoader(f.read())
    try:
        root = loader.get_single_node()

        def get(m, key):
            if isinstance(m, yaml.MappingNode):
                for k, v in m.value:
                    if isinstance(k, yaml.ScalarNode) and k.value == key:
                        return v
            return None

        def obj(node):
            return loader.construct_object(node, deep=True)

        def name(node):
            if node is None:
                return ''
            v = obj(node)
            return '' if v is None else str(v)

        def text(node):
            if isinstance(node, yaml.ScalarNode):
                if node.tag == 'tag:yaml.org,2002:null':
                    return ''
                return node.value
            return yaml.safe_dump(obj(node), default_flow_style=True,
                                  allow_unicode=True, width=1 << 30).strip()

        out = {'nodes': [], 'edges': []}
        seq = get(root, 'nodes')
        for n in (seq.value if isinstance(seq, yaml.SequenceNode) else []):
            if not isinstance(n, yaml.MappingNode):
                continue
            pos = [0.0, 0.0]
            pn = get(n, 'pos')
            if isinstance(pn, yaml.SequenceNode):
                for i, c in enumerate(pn.value[:2]):
                    try:
                        x = float(obj(c))
                        if math.isfinite(x):    # JSON has no NaN / inf
                            pos[i] = x
                    except Exception:
                        pass
            en = get(n, 'enabled')
            params = []
            pm = get(n, 'params')
            if isinstance(pm, yaml.MappingNode):
                for k, v in pm.value:
                    params.append([text(k), text(v)])
            out['nodes'].append({
                'id': name(get(n, 'id')),
                'type': name(get(n, 'type')),
                'pos': pos,
                'enabled': True if en is None else bool(obj(en)),
                'params': params,
            })
        seq = get(root, 'edges')
        for e in (seq.value if isinstance(seq, yaml.SequenceNode) else []):
            if isinstance(e, yaml.MappingNode):
                out['edges'].append([name(get(e, 'from')), name(get(e, 'to'))])
    finally:
        loader.dispose()
    json.dump(out, sys.stdout, ensure_ascii=True)

try:
    main(sys.argv[1])
except yaml.YAMLError as exc:
    sys.stderr.write('%s\n' % exc)
    sys.exit(2)
)PY";

}  // namespace

bool PipelineDesignerPage::savePipelineFile(const QString& path, QString* error,
                                            bool markSaved)
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
        out << "  - id: "      << yamlName(n.id)   << "\n";
        out << "    type: "    << yamlName(n.type) << "\n";
        out << "    pos: ["    << QString::number(n.pos.x(), 'f', 1)
            << ", "            << QString::number(n.pos.y(), 'f', 1) << "]\n";
        out << "    enabled: " << (n.enabled ? "true" : "false") << "\n";
        if (n.params.isEmpty()) {
            out << "    params: {}\n";
        } else {
            out << "    params:\n";
            for (const auto& pv : n.params) {
                out << "      " << yamlName(pv.first) << ": " << yamlQuote(pv.second) << "\n";
            }
        }
    }

    // Edges ──────────────────────────────────────────────────────────────
    if (edges.isEmpty()) {
        out << "edges: []\n";
    } else {
        out << "edges:\n";
        for (const PipelineEdge& e : edges) {
            out << "  - {from: " << yamlName(e.from) << ", to: " << yamlName(e.to) << "}\n";
        }
    }

    out.flush();
    if (f.error() != QFile::NoError) {
        if (error) *error = QStringLiteral("Write failed: %1").arg(f.errorString());
        return false;
    }
    // The file that failed to load now holds this graph: nothing left to
    // protect, and the failure notice no longer applies.
    if (!lastFailedLoad.isEmpty() && QFileInfo(path).absoluteFilePath() == lastFailedLoad) {
        lastFailedLoad.clear();
        showNotice(QString());
    }
    if (markSaved) unsaved = false;
    return true;
}

bool PipelineDesignerPage::loadPipelineFile(const QString& path, QString* error)
{
    QString message;
    const bool ok = readPipelineFile(path, &message);
    if (ok) lastFailedLoad.clear();
    else    lastFailedLoad = QFileInfo(path).absoluteFilePath();
    if (error) *error = message;
    return ok;
}

bool PipelineDesignerPage::readPipelineFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Could not open '%1' for reading: %2")
                        .arg(path, f.errorString());
        }
        return false;
    }
    f.close();

    // Parse with PyYAML (see kPipelineParser) and read its JSON.
    QProcess py;
    py.start(QStringLiteral("python3"), QStringList{
        QStringLiteral("-c"), QString::fromUtf8(kPipelineParser), path });
    // Tell the failure modes apart: python3 missing / not executable, the
    // parser hanging, crashing, or rejecting the file.
    if (!py.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not start python3 to read '%1' (%2).  "
                                    "Pipeline files are read with python3 and PyYAML, "
                                    "which must be on PATH.")
                        .arg(path, py.errorString());
        }
        return false;
    }
    if (!py.waitForFinished(15000)) {
        py.kill();
        py.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("Reading '%1' timed out: the python3 parser did not "
                                    "finish within 15 s.").arg(path);
        }
        return false;
    }
    if (py.exitStatus() != QProcess::NormalExit) {
        if (error) {
            *error = QStringLiteral("The python3 parser crashed while reading '%1'.")
                        .arg(path);
        }
        return false;
    }
    if (py.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Failed to parse '%1':\n%2")
                        .arg(path, QString::fromUtf8(py.readAllStandardError()).trimmed());
        }
        return false;
    }
    QJsonParseError jerr;
    const QJsonDocument jdoc = QJsonDocument::fromJson(py.readAllStandardOutput(), &jerr);
    if (jerr.error != QJsonParseError::NoError || !jdoc.isObject()) {
        if (error) {
            *error = QStringLiteral("Failed to parse '%1': unexpected parser output (%2)")
                        .arg(path, jerr.errorString());
        }
        return false;
    }
    const QJsonArray jnodes = jdoc.object().value(QLatin1String("nodes")).toArray();
    const QJsonArray jedges = jdoc.object().value(QLatin1String("edges")).toArray();

    // Replace the graph atomically — only mutate nodes/edges if the
    // parse succeeds.
    QList<PipelineNode> newNodes;
    QList<PipelineEdge> newEdges;
    QHash<QString, int> nodeIdx;  // id → index in newNodes
    QStringList unknownTypes;
    QStringList renamedIds;
    QStringList extraRoots;
    QStringList rootInEdges;
    bool haveRoot = false;
    // Fresh ids for renamed duplicates: "n<k>" above every numeric id in
    // the file, so they can't collide with a later node either.
    int freshSeq = 0;
    {
        static const QRegularExpression numericId(QStringLiteral("^n(\\d{1,9})$"));
        for (const QJsonValue& v : jnodes) {
            const QRegularExpressionMatch m =
                numericId.match(v.toObject().value(QLatin1String("id")).toString());
            if (m.hasMatch()) freshSeq = qMax(freshSeq, m.captured(1).toInt());
        }
    }
    auto uniqueFreshId = [&]() {
        QString id;
        do { id = QStringLiteral("n%1").arg(++freshSeq); } while (nodeIdx.contains(id));
        return id;
    };

    for (const QJsonValue& v : jnodes) {
        const QJsonObject o = v.toObject();
        const QString type = o.value(QLatin1String("type")).toString();
        // Drop unknown types with a warning rather than failing the load —
        // this can happen when a pipeline file references a plugin that
        // was renamed or removed since it was saved.
        if (!ndmScriptDef(type)) {
            unknownTypes.append(type.isEmpty() ? QStringLiteral("<no type>") : type);
            continue;
        }
        // One root only (the canvas never makes a second): keep the first
        // ndm_start, drop the rest together with their edges.
        if (type == kRootType && haveRoot) {
            extraRoots.append(o.value(QLatin1String("id")).toString());
            continue;
        }
        haveRoot = haveRoot || type == kRootType;
        PipelineNode n;
        n.id      = o.value(QLatin1String("id")).toString();
        n.type    = type;
        const QJsonArray pos = o.value(QLatin1String("pos")).toArray();
        n.pos     = QPointF(pos.at(0).toDouble(), pos.at(1).toDouble());
        n.enabled = o.value(QLatin1String("enabled")).toBool(true);
        if (n.id.isEmpty() || nodeIdx.contains(n.id)) {
            // A second node with the same id (or none): edges naming the id
            // belong to the first node; this one keeps its type, position
            // and parameters under a fresh id.
            renamedIds.append(QStringLiteral("%1 (%2)")
                .arg(n.id.isEmpty() ? QStringLiteral("<empty>") : n.id, n.type));
            n.id = uniqueFreshId();
        }
        for (const QJsonValue& pv : o.value(QLatin1String("params")).toArray()) {
            const QJsonArray kv = pv.toArray();
            if (kv.size() == 2)
                n.params.append({ kv.at(0).toString(), kv.at(1).toString() });
        }
        nodeIdx.insert(n.id, newNodes.size());
        newNodes.append(n);
    }
    for (const QJsonValue& v : jedges) {
        const QJsonArray e = v.toArray();
        if (e.size() != 2) continue;
        const QString from = e.at(0).toString(), to = e.at(1).toString();
        // Skip edges that reference a dropped (unknown-type) node.
        if (!nodeIdx.contains(from) || !nodeIdx.contains(to)) continue;
        // The root has indegree 0 (the canvas refuses such edges).
        if (newNodes.at(nodeIdx.value(to)).type == kRootType) {
            rootInEdges.append(from + QStringLiteral("→") + to);
            continue;
        }
        newEdges.append({ from, to });
    }

    // Drop edges that close a cycle (hand-edited files; the canvas refuses
    // them), in file order: keep an edge unless its target already reaches
    // its source through the edges kept so far.  ndm_start would skip every
    // node on or after the cycle, the canvas would append them — this way
    // both agree once the file is saved again.
    QStringList cycleEdges;
    {
        QList<PipelineEdge> kept;
        auto reaches = [&kept](const QString& a, const QString& b) {
            if (a == b) return true;
            QSet<QString> seen{ a };
            QStringList stack{ a };
            while (!stack.isEmpty()) {
                const QString cur = stack.takeLast();
                for (const PipelineEdge& e : kept) {
                    if (e.from != cur || seen.contains(e.to)) continue;
                    if (e.to == b) return true;
                    seen.insert(e.to);
                    stack.append(e.to);
                }
            }
            return false;
        };
        for (const PipelineEdge& e : newEdges) {
            if (reaches(e.to, e.from)) {
                cycleEdges.append(e.from + QStringLiteral("→") + e.to);
                continue;
            }
            kept.append(e);
        }
        newEdges = kept;
    }

    // Ensure ndm_start is present.  A hand-edited pipeline file might have
    // lost the root; re-insert it (first) rather than failing the load.  An
    // existing root stays where the file has it: node order is the order
    // Kahn's queue is seeded in, here and in ndm_start, and the root is
    // hoisted to programs[0] by getPrograms() regardless.
    if (!haveRoot) {
        PipelineNode root;
        root.id      = uniqueFreshId();
        root.type    = kRootType;
        root.pos     = QPointF(60.0, 80.0);
        root.enabled = true;
        if (const NdmScriptDef* def = ndmScriptDef(kRootType)) {
            for (const NdmParamDef& pd : def->params) {
                root.params.append({ pd.name, pd.defaultValue });
            }
        }
        newNodes.prepend(root);
    }

    // Apply.
    nodes = newNodes;
    edges = newEdges;
    if (canvas) {
        canvas->resetInteraction();
        canvas->reserveIds();   // new nodes must not reuse the file's ids
    }
    inspectedId.clear();
    clearInspector();

    if (canvas) canvas->update();
    applyPending = false;
    unsaved = false;
    refreshApplyState();

    QStringList warnings;
    if (!unknownTypes.isEmpty()) {
        warnings << QStringLiteral("Loaded with %1 unknown plugin type(s) dropped: %2")
                    .arg(unknownTypes.size())
                    .arg(unknownTypes.join(QStringLiteral(", ")));
    }
    if (!renamedIds.isEmpty()) {
        warnings << QStringLiteral("Duplicate or empty node id(s) given fresh ids "
                                   "(edges naming a duplicated id attach to its first node): %1")
                    .arg(renamedIds.join(QStringLiteral(", ")));
    }
    if (!haveRoot) {
        warnings << QStringLiteral("The file has no ndm_start node; a root was added.");
    }
    if (!extraRoots.isEmpty()) {
        warnings << QStringLiteral("Extra ndm_start node(s) dropped (only one root is "
                                   "allowed): %1").arg(extraRoots.join(QStringLiteral(", ")));
    }
    if (!rootInEdges.isEmpty()) {
        warnings << QStringLiteral("Edge(s) into the ndm_start root dropped: %1")
                    .arg(rootInEdges.join(QStringLiteral(", ")));
    }
    if (!cycleEdges.isEmpty()) {
        warnings << QStringLiteral("Edge(s) closing a cycle dropped: %1.  Until the pipeline "
                                   "is saved again, ndm_start skips the nodes on and after "
                                   "that cycle.")
                    .arg(cycleEdges.join(QStringLiteral(", ")));
    }
    if (error && !warnings.isEmpty()) *error = warnings.join(QLatin1Char('\n'));
    return true;
}
