/***************************************************************************
 *   Copyright (C) 2004-2011 by Michael Zugaro                             *
 *   michael.zugaro@college-de-france.fr                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef PROGRESS_BAR_H
#define PROGRESS_BAR_H

#include <string>

/**
 * Terminal progress bar with auto-fitting layout for an 80-column line.
 *
 * Layout (≤ 80 cols during run, ≤ 82 at completion with ✓ marker):
 *
 *   <label> [<step>] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━────────────────────────────
 *   <label> [<step>] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ ✓
 *
 * The bar width is computed at start() from the terminal width capped at
 * 80, minus the space taken by the label, the step tag, and the brackets.
 * Long labels are ellipsised so the bar never collapses below
 * MIN_BAR_WIDTH characters.  The bar's fill state is the progress
 * indicator — there is no separate numerical percentage.  On completion
 * the bar appends a bold-green ✓ when Unicode is available.
 *
 * Output channel.  The bar is drawn to /dev/tty directly when that's
 * available, falling back to stderr when stderr is a TTY, falling back
 * to non-TTY mode (milestone lines on stdout) when neither is.  This
 * lets the bar render live even when the calling shell script
 * redirects stdout and stderr — e.g. ndm_pca's per-group parallel
 * logfile capture (`once $i > log 2>&1 &`) — because /dev/tty bypasses
 * shell redirections.  Diagnostic prints from the plugin (`transformed
 * N spikes ...`) still go through stdout/stderr normally and are
 * captured to the logfile as before.
 *
 * Each redraw is prefixed with the ANSI ESC[2K (erase entire line)
 * escape, so concurrent text intrusions from other processes (e.g. a
 * wrapper script's logfile cat-back from finished groups while
 * in-flight groups are still drawing) get wiped on the next redraw
 * cycle and don't corrupt the bar.
 *
 * Visual style.  Minimalist horizontal-line: filled cells use U+2501 ━
 * (box-drawing heavy horizontal), unfilled use U+2500 ─ (light).  ASCII
 * fallback uses '='/'-' when the output channel doesn't support UTF-8.
 * No closing delimiter — the bar runs to the edge, completion ✓ floats
 * off the end.
 *
 * Visual style.  When stdout is a UTF-8 TTY the bar uses Unicode block
 * characters: U+2588 (full block) for filled cells and U+2591 (light
 * shade) for unfilled cells.  The leading edge of the filled region uses
 * the eighths-block characters (U+258F .. U+2589) to render fractional
 * progress smoothly.  When stdout is not a TTY (piped to a file, captured
 * by a parent process, or LANG/LC_ALL doesn't say UTF-8), the bar falls
 * back to ASCII '#' / '.' and emits one line per ~5% milestone instead of
 * carriage-return redraws — log files stay clean.
 *
 * Thread safety.  None.  Each thread should hold its own ProgressBar.
 *
 * Usage:
 *
 *   ProgressBar progress(basename, "PCA", nIterations);
 *   progress.start();
 *   for (int i = 0; i < nIterations; ++i) {
 *       doWork(i);
 *       progress.advance();
 *   }
 *   progress.finish();        // optional — destructor also calls it
 *
 * @author Michael Zugaro (original); modernised 2026
 */
class ProgressBar
{
public:
    /**
     * @param label  per-instance prefix, e.g. session base name
     * @param step   short tag for the current phase, e.g. "NEV" or "PCA"
     * @param total  total number of advance() calls expected to reach 100%
     */
    ProgressBar(std::string label, std::string step, int total);

    virtual ~ProgressBar();

    /** Draw the empty bar.  Idempotent on repeated calls. */
    virtual void start();

    /** Increment progress by one step and redraw if the visible state
     *  changed (avoids hammering the terminal for total >> bar-width). */
    virtual void advance();

    /** Finish the current bar at 100%, emit @p msg on a fresh line, and
     *  start a new bar at 0% so the same instance can be reused for the
     *  next phase.  If @p msg is empty, just emits a clean "done" line
     *  and stops. */
    virtual void message(std::string msg);

    /** Force the bar to 100% and emit a trailing newline.  Called
     *  automatically by the destructor; safe to call explicitly.
     *
     *  By default the bar terminates with a green ✓ marker (Unicode
     *  only).  If setFailed() has been called any time before finish(),
     *  the marker is a red ✗ instead, and the bar is left at whatever
     *  fill state it had reached (not snapped to 100%) so the failure
     *  point is visible. */
    virtual void finish();

    /** Mark this bar as failed.  Subsequent finish() (or destructor)
     *  will emit a red ✗ marker instead of the default green ✓, and
     *  will leave the bar at its current fill state rather than
     *  snapping to 100%.  Idempotent.  Call any time before finish(). */
    virtual void setFailed();

private:
    std::string label;
    std::string step;
    int         total;
    int         done;
    int         barWidth;        ///< computed at start(); 0 = uninitialised
    int         lastDrawnEighths;///< 1/8-cell granularity, prevents redundant redraws
    bool        unicode;         ///< true when output channel is a UTF-8 TTY
    bool        isTty;           ///< true when bar can be drawn live
    void*       ttyFile;         ///< FILE* for live drawing; NULL → non-TTY path.
                                   ///< Points at /dev/tty if open succeeds, else
                                   ///< stderr if stderr is a TTY, else NULL.
                                   ///< Held as void* so the header doesn't have
                                   ///< to drag in <cstdio>.
    bool        ttyFileOwned;    ///< true iff we fopen'd it (must fclose in dtor)
    int         nonTtyMilestones;///< for piped output: count of 5% milestones emitted
    bool        started;         ///< start() has run
    bool        finished;        ///< finish() has run
    bool        failed;          ///< setFailed() has been called

    /** Render the bar at its current state. */
    void redraw();

    /** Compute barWidth, taking terminal width and label/step lengths
     *  into account.  Caller is expected to handle ellipsis on label
     *  if the result would be too narrow. */
    void computeLayout();

    /** Decide whether to use Unicode block characters or ASCII fallback,
     *  and whether stdout is a TTY at all.  Sets unicode and isTty. */
    void detectCapabilities();
};


/**
 * Defrag-style block-map progress for a linear resource scanned in stages
 * (e.g. a .fil file walked once per extraction pass).
 *
 * The resource is drawn as a single row of small colored block cells.
 * Within a stage, cells already swept are filled with the stage's color,
 * the leading edge cell is bright, and not-yet-reached cells are dim — so
 * the row shows, defrag-style, how far the current stage has processed the
 * file.  Each stage (DETECT, EXTRACT, …) re-sweeps the row in its own
 * color and is left on its own finished line, so the sequence of stages
 * stacks vertically as a record of the run.
 *
 *   <label> [DETECT ] ████████████████░░░░░░░░░░░░░░░░░░░░  46%
 *   <label> [EXTRACT] ██████████████████████████████████░░  92%
 *
 * Output model is identical to ProgressBar: live redraws go to /dev/tty
 * (bypassing shell redirections), falling back to stderr, then to non-TTY
 * milestone lines on stdout (so captured logs stay clean).  A UTF-8 TTY
 * gets U+2588/U+2591 blocks with ANSI color; otherwise it falls back to
 * '#'/'.' with no color.
 *
 * Usage:
 *   BlockProgress p(basename);
 *   p.beginStage("DETECT", totalSamples);
 *   ... p.setPosition(samplesScanned) in the scan loop ...
 *   p.endStage();
 *   p.beginStage("EXTRACT", fileBytes);
 *   ... p.setPosition(bytePosition) in the read loop ...
 *   p.endStage();
 *   p.finish();                     // trailing ✓ (or ✗ after setFailed())
 */
class BlockProgress
{
public:
    /** @param label per-instance prefix, e.g. the session base name. */
    explicit BlockProgress(std::string label);
    ~BlockProgress();

    /** Begin a stage spanning [0, totalUnits]; resets the row to empty and
     *  advances the stage color.  totalUnits <= 0 is treated as 1. */
    void beginStage(const std::string& stageTag, long long totalUnits);

    /** Set the absolute position within the current stage.  Clamped to
     *  [0, total].  Cheap to call often: only redraws when the visible
     *  filled-cell count changes. */
    void setPosition(long long position);

    /** Fill the current stage's row to 100% and leave it on its own line. */
    void endStage();

    /** Terminal completion: appends a green ✓ (or red ✗ if setFailed() was
     *  called) and a newline.  Called by the destructor; safe to call
     *  explicitly.  Idempotent. */
    void finish();

    /** Mark the run as failed so finish() emits ✗ at the current fill
     *  rather than ✓.  Idempotent. */
    void setFailed();

private:
    std::string label;
    std::string stageTag;
    long long   total;            ///< units in the current stage (>= 1)
    long long   pos;              ///< current position within the stage
    int         stageIndex;       ///< 0-based; selects the stage color
    int         cells;            ///< block-row width, computed per stage
    int         lastDrawnCells;   ///< filled-cell count last drawn (dedupe)
    bool        unicode;          ///< UTF-8 TTY → blocks + color
    bool        isTty;            ///< can draw live
    void*       ttyFile;          ///< FILE* for live drawing; NULL → non-TTY
    bool        ttyFileOwned;     ///< true iff we fopen'd it
    int         nonTtyMilestones; ///< 5% milestones emitted this stage
    bool        stageOpen;        ///< a stage row is currently live
    bool        failed;
    bool        finished;

    void detectCapabilities();
    void redraw();
    void emitNonTtyMilestones();
};

#endif // PROGRESS_BAR_H
