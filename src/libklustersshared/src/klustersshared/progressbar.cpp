/***************************************************************************
 *   Copyright (C) 2004-2011 by Michael Zugaro                             *
 *   michael.zugaro@college-de-france.fr                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "progressbar.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>              // open, O_RDONLY, O_NOCTTY, O_CLOEXEC
#include <iostream>
#include <unistd.h>             // isatty, fileno, STDOUT_FILENO, close
#include <sys/ioctl.h>          // TIOCGWINSZ — terminal width

namespace {

// Layout constants.
//
// The maximum line width we ever emit.  Hard ceiling, even on wider
// terminals: a long unbroken line is harder to read than an 80-col one,
// and the user explicitly asked for 80.
constexpr int MAX_LINE_WIDTH = 80;

// Chrome around the bar: "[" + "]" + " " = 3 chars (no closing
// bracket on the bar — the minimalist style runs the line to the edge
// without a delimiter, then the completion ✓ floats off the end).
constexpr int CHROME_WIDTH   = 3;

// Don't let the bar collapse below this; if the label/step combo is so
// long that the bar would be narrower, ellipsise the label instead.
constexpr int MIN_BAR_WIDTH  = 10;

// Pacing for non-TTY output: emit one line per N percent.
constexpr int NONTTY_PCT_STEP = 5;

// Unicode box-drawing characters for the minimalist bar style.
// Filled portion: U+2501 ━ (heavy horizontal) — thin but bold.
// Unfilled:       U+2500 ─ (light horizontal) — same metrics, lighter weight.
// Both are 3 bytes in UTF-8.
const char* const FULL_BLOCK   = "\xE2\x94\x81";   // U+2501 ━
const char* const LIGHT_SHADE  = "\xE2\x94\x80";   // U+2500 ─

// Completion marker.  ANSI bold-green (\x1b[1;32m) + space + ✓ (U+2713)
// + reset.  Drawn at the end of the bar by finish() / message() when
// Unicode is available; pushes the line width to ~82 cells on
// completion only.
const char* const COMPLETE_MARK_UTF8 = " \x1b[1;32m\xE2\x9C\x93\x1b[0m";

// Failure marker.  ANSI bold-red (\x1b[1;31m) + space + ✗ (U+2717) +
// reset.  Used by finish() when setFailed() has been called.  Same
// 2-cell width as the success marker.
const char* const FAIL_MARK_UTF8 = " \x1b[1;31m\xE2\x9C\x97\x1b[0m";

// Eighths blocks for fractional leading edge: index 0 unused (zero
// fraction means "no partial block"), 1..7 are the 1/8 .. 7/8 widths.
//
//   1/8  U+258F  ▏
//   2/8  U+258E  ▎
//   3/8  U+258D  ▍
//   4/8  U+258C  ▌
//   5/8  U+258B  ▋
//   6/8  U+258A  ▊
//   7/8  U+2589  ▉
const char* const EIGHTHS[8] = {
    "",
    "\xE2\x96\x8F",  // ▏
    "\xE2\x96\x8E",  // ▎
    "\xE2\x96\x8D",  // ▍
    "\xE2\x96\x8C",  // ▌
    "\xE2\x96\x8B",  // ▋
    "\xE2\x96\x8A",  // ▊
    "\xE2\x96\x89",  // ▉
};

// Returns the controlling terminal's column count via TIOCGWINSZ.
// Probes file descriptors in the same order the bar's output channel
// is chosen: /dev/tty (transient open), stderr, stdout.  Returns 0 if
// nothing is a TTY or all ioctls fail; caller substitutes a default.
int detectTerminalWidth()
{
    struct winsize ws{};

    // Try /dev/tty: bypasses shell redirections, matches what the bar
    // actually draws to in the most-redirected scenarios.
    int ttyFd = ::open("/dev/tty", O_RDONLY | O_NOCTTY | O_CLOEXEC);
    if (ttyFd >= 0) {
        if (ioctl(ttyFd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            ::close(ttyFd);
            return static_cast<int>(ws.ws_col);
        }
        ::close(ttyFd);
    }
    // stderr — usually the second-most-likely-to-be-TTY fd.
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    // stdout — last try.
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return static_cast<int>(ws.ws_col);
    }
    return 0;
}

// True if locale env vars indicate UTF-8.  We check LC_ALL first (it
// overrides everything else if set), then LC_CTYPE, then LANG.  Anything
// else (including unset vars) means "assume legacy 8-bit, fall back to
// ASCII".  We don't call setlocale() because that has process-wide side
// effects we don't want to introduce.
bool localeIsUtf8()
{
    auto looksUtf8 = [](const char* v) -> bool {
        if (!v || !*v) return false;
        // Match "UTF-8", "utf-8", "UTF8", "utf8" anywhere in the value.
        for (const char* p = v; *p; ++p) {
            if ((p[0] == 'U' || p[0] == 'u') &&
                (p[1] == 'T' || p[1] == 't') &&
                (p[2] == 'F' || p[2] == 'f')) {
                const char* q = p + 3;
                if (*q == '-' || *q == '_') ++q;
                if (q[0] == '8') return true;
            }
        }
        return false;
    };
    if (const char* v = std::getenv("LC_ALL"))   { if (*v) return looksUtf8(v); }
    if (const char* v = std::getenv("LC_CTYPE")) { if (*v) return looksUtf8(v); }
    if (const char* v = std::getenv("LANG"))     { if (*v) return looksUtf8(v); }
    return false;
}

// Truncate @p s to @p maxChars characters, appending an ellipsis "…"
// (or "..." in ASCII mode) if truncation occurred.  Operates on bytes
// since labels are expected to be ASCII filenames; if we ever need to
// support non-ASCII labels, this needs codepoint-aware truncation.
std::string ellipsise(const std::string& s, int maxChars, bool unicode)
{
    if (static_cast<int>(s.length()) <= maxChars) return s;
    if (maxChars <= 0) return std::string();
    if (unicode) {
        // "…" is U+2026, 3 bytes in UTF-8.  We render maxChars-1 chars
        // of the original then the ellipsis — total visible width
        // (cells) = maxChars.
        if (maxChars < 2) return std::string("\xE2\x80\xA6");
        return s.substr(0, static_cast<size_t>(maxChars - 1)) +
               "\xE2\x80\xA6";
    }
    if (maxChars < 4) return std::string(maxChars, '.');
    return s.substr(0, static_cast<size_t>(maxChars - 3)) + "...";
}

} // namespace

// ---------------------------------------------------------------------------
ProgressBar::ProgressBar(std::string label, std::string step, int total)
    : m_label(std::move(label))
    , m_step(std::move(step))
    , m_total(total)
    , m_done(0)
    , m_barWidth(0)
    , m_lastDrawnEighths(-1)
    , m_unicode(false)
    , m_isTty(false)
    , m_ttyFile(nullptr)
    , m_ttyFileOwned(false)
    , m_nonTtyMilestones(0)
    , m_started(false)
    , m_finished(false)
    , m_failed(false)
{
    detectCapabilities();
}

ProgressBar::~ProgressBar()
{
    finish();
    if (m_ttyFileOwned && m_ttyFile) {
        std::fclose(static_cast<std::FILE*>(m_ttyFile));
    }
    m_ttyFile      = nullptr;
    m_ttyFileOwned = false;
}

// ---------------------------------------------------------------------------
// Pick an output channel for the live bar:
//   1. /dev/tty if it opens — bypasses shell redirections, the right
//      thing for plugins invoked from wrapper scripts that capture
//      stdout/stderr to per-group logfiles (ndm_pca's parallel mode).
//   2. stderr if it's a TTY — conventional CLI fallback for tools where
//      stdout is the data channel.
//   3. neither: m_isTty stays false, the redraw loop emits milestone
//      lines on stdout instead.
//
// We also detect Unicode capability on the chosen channel.
// ---------------------------------------------------------------------------
void ProgressBar::detectCapabilities()
{
    // 1. Try /dev/tty.  Most reliable: it's the controlling terminal,
    //    unaffected by `> log` or `2>&1 | foo` in the parent shell.
    //    Will fail for non-interactive runs (cron, batch jobs, CI) —
    //    we fall through to stderr in that case.
    if (std::FILE* tty = std::fopen("/dev/tty", "w")) {
        // Disable buffering so \r-redraws are visible immediately.
        std::setvbuf(tty, nullptr, _IONBF, 0);
        m_ttyFile      = tty;
        m_ttyFileOwned = true;
        m_isTty        = true;
    }
    // 2. Fall back to stderr if it's a TTY.  Don't open it — just point
    //    at the existing FILE*; setvbuf on stderr is usually a no-op
    //    since stderr defaults to unbuffered, but some libc'es buffer
    //    it line-wise.  Safe to call.
    else if (isatty(STDERR_FILENO)) {
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        m_ttyFile      = stderr;
        m_ttyFileOwned = false;
        m_isTty        = true;
    }
    // 3. Last try: stdout if it's a TTY (rare — usually means /dev/tty
    //    failed for an unusual reason).  Keep it for completeness.
    else if (isatty(STDOUT_FILENO)) {
        m_ttyFile      = stdout;
        m_ttyFileOwned = false;
        m_isTty        = true;
    } else {
        m_isTty   = false;
        m_ttyFile = nullptr;
    }

    m_unicode = m_isTty && localeIsUtf8();
}

// ---------------------------------------------------------------------------
void ProgressBar::computeLayout()
{
    // Decide the line width we'll target.  We want the smaller of the
    // terminal's actual width and our ceiling — narrow terminals
    // (e.g. 70 cols on a small SSH session) shouldn't get truncated.
    int termWidth = detectTerminalWidth();
    if (termWidth <= 0) termWidth = MAX_LINE_WIDTH;
    int targetWidth = std::min(termWidth, MAX_LINE_WIDTH);

    // Bar width budget: total minus chrome and the label/step lengths.
    // Step gets to keep its full length (it's typically 1-4 chars by
    // convention); label gets ellipsised if needed.
    //
    // Chrome accounts for: '[' + step + ']' + ' '  = 3 chars baseline,
    // plus a leading ' ' between label and '[' when the label is
    // non-empty.
    int stepLen      = static_cast<int>(m_step.length());
    int labelLen     = static_cast<int>(m_label.length());
    int chromeActual = CHROME_WIDTH + (labelLen > 0 ? 1 : 0);
    int budget       = targetWidth - chromeActual - stepLen - labelLen;

    if (budget < MIN_BAR_WIDTH) {
        // Need to ellipsise the label.  Max label width is whatever's
        // left after we reserve MIN_BAR_WIDTH for the bar.  We're going
        // to keep the label non-empty (just shorter), so chrome stays
        // at chromeActual = CHROME_WIDTH + 1.
        int maxLabel = targetWidth - (CHROME_WIDTH + 1) - stepLen - MIN_BAR_WIDTH;
        if (maxLabel < 1) {
            // Pathological: the step alone is too long.  Shrink both —
            // truncate the step to a sensible cap and drop the label.
            m_label.clear();
            const int stepCap = 8;
            if (static_cast<int>(m_step.length()) > stepCap) {
                m_step = ellipsise(m_step, stepCap, m_unicode);
            }
            // Label is now empty so chrome = CHROME_WIDTH (no leading space).
            m_barWidth = targetWidth - CHROME_WIDTH
                       - static_cast<int>(m_step.length());
        } else {
            m_label    = ellipsise(m_label, maxLabel, m_unicode);
            m_barWidth = MIN_BAR_WIDTH;
        }
    } else {
        m_barWidth = budget;
    }

    // Final safety clamp.  Should never trigger after the logic above
    // but defends against future arithmetic mistakes.
    if (m_barWidth < 1) m_barWidth = 1;
}

// ---------------------------------------------------------------------------
void ProgressBar::start()
{
    if (m_started) return;
    m_started = true;

    computeLayout();
    m_done             = 0;
    m_lastDrawnEighths = -1;
    m_nonTtyMilestones = 0;
    redraw();
}

// ---------------------------------------------------------------------------
void ProgressBar::advance()
{
    if (!m_started) start();
    if (m_finished) return;

    if (m_done < m_total) ++m_done;
    redraw();
}

// ---------------------------------------------------------------------------
void ProgressBar::redraw()
{
    if (m_total <= 0) return;

    // Fraction of work done, in eighths-of-a-cell units.  This is the
    // smallest visible state change a Unicode bar can render; using it
    // as the change-detection key lets us skip redundant redraws.
    const long totalEighths = static_cast<long>(m_barWidth) * 8;
    long doneEighths        = (static_cast<long>(m_done) * totalEighths)
                            / static_cast<long>(m_total);
    if (doneEighths < 0) doneEighths = 0;
    if (doneEighths > totalEighths) doneEighths = totalEighths;

    // ── Non-TTY path: emit one line per ~5% milestone on stdout. ─────────
    // Stays on stdout so log-capturing wrappers see milestones in their
    // logfiles when no TTY is available at all (CI, batch, cron).
    if (!m_isTty) {
        const int pct = static_cast<int>((100L * m_done) / m_total);
        const int milestone = pct / NONTTY_PCT_STEP;
        if (milestone > m_nonTtyMilestones) {
            m_nonTtyMilestones = milestone;
            std::cout << m_label
                      << (m_label.empty() ? "" : " ")
                      << "[" << m_step << "] " << pct << "%\n";
            std::cout.flush();
        }
        return;
    }

    // ── TTY path: full-line redraw via \r on the chosen channel. ─────────
    if (doneEighths == m_lastDrawnEighths) return;   // nothing visibly changed
    m_lastDrawnEighths = doneEighths;

    // Round eighths to whole cells.  The minimalist horizontal-line
    // style doesn't accommodate vertical eighths-blocks as a leading
    // edge (they'd be tall vertical lines amid thin horizontal ones —
    // visual mismatch).  Eighths resolution is retained at the
    // tracking level to debounce redundant redraws when total >>
    // barWidth, but the rendered bar is whole-cell.
    int filled = static_cast<int>(doneEighths / 8);
    if ((doneEighths % 8) >= 4) ++filled;
    if (filled > m_barWidth) filled = m_barWidth;

    // Build the line as a single string then write atomically.  Prefix
    // with ESC[2K (erase entire line) + \r so any concurrent text
    // intrusion from another process's stdout/stderr writes (e.g.
    // ndm_pca's logfile-cat-back from finished groups while in-flight
    // groups are still drawing bars) gets wiped before we redraw.
    // Multiple parallel bars also benefit: each redraw clears the line
    // first, eliminating bar-on-bar tearing.
    std::string line;
    line.reserve(static_cast<size_t>(MAX_LINE_WIDTH) * 4);  // UTF-8 worst case
    line += "\r\x1b[2K";
    if (!m_label.empty()) { line += m_label; line.push_back(' '); }
    line.push_back('[');
    line += m_step;
    line += "] ";

    if (m_unicode) {
        for (int i = 0; i < filled;             ++i) line += FULL_BLOCK;
        for (int i = 0; i < m_barWidth - filled; ++i) line += LIGHT_SHADE;
    } else {
        for (int i = 0; i < filled;             ++i) line.push_back('=');
        for (int i = 0; i < m_barWidth - filled; ++i) line.push_back('-');
    }

    std::FILE* out = static_cast<std::FILE*>(m_ttyFile);
    std::fwrite(line.data(), 1, line.size(), out);
    std::fflush(out);
}

// ---------------------------------------------------------------------------
void ProgressBar::message(std::string msg)
{
    if (!m_started) start();

    // Force the visible bar to 100% before we drop the message.
    m_done = m_total;
    m_lastDrawnEighths = -1;       // bypass change-skip in redraw()
    redraw();

    // The message line goes on the same channel the bar drew on so it
    // appears in the same place visually.  Newline-prefix in TTY mode
    // (cursor sits at end of \r-redrawn line); plain prefix in non-TTY
    // mode (the redraw above ended in \n already on stdout).
    if (m_isTty) {
        std::FILE* out = static_cast<std::FILE*>(m_ttyFile);
        // Same completion marker as finish() — the bar reached 100%
        // for this phase, so the check belongs here too.  ASCII fallback
        // skips the marker.
        if (m_unicode) std::fputs(COMPLETE_MARK_UTF8, out);
        std::fputc('\n', out);
        std::fputs("    \xE2\x86\x92 ", out);   // "    → " in UTF-8
        std::fputs(msg.c_str(), out);
        std::fputc('\n', out);
        std::fflush(out);
    } else {
        std::cout << "    -> " << msg << '\n';
        std::cout.flush();
    }

    // Reset for reuse: the next phase starts a fresh bar at 0%.
    m_started          = false;
    m_finished         = false;
    m_done             = 0;
    m_lastDrawnEighths = -1;
    start();
}

// ---------------------------------------------------------------------------
void ProgressBar::setFailed()
{
    m_failed = true;
}

// ---------------------------------------------------------------------------
void ProgressBar::finish()
{
    if (m_finished) return;
    m_finished = true;
    if (!m_started) return;

    // Snap to 100% only on success.  On failure we leave the bar at
    // whatever fill state it had reached so the failure point is
    // visible — useful for debugging "where did this die".
    if (!m_failed && m_done < m_total) {
        m_done = m_total;
        m_lastDrawnEighths = -1;
        redraw();
    } else if (m_failed) {
        // Force a redraw of the current state to make sure the line is
        // clean (any concurrent stderr/stdout intrusions get wiped by
        // the redraw's leading ESC[2K) before the marker is appended.
        m_lastDrawnEighths = -1;
        redraw();
    }
    if (m_isTty) {
        std::FILE* out = static_cast<std::FILE*>(m_ttyFile);
        // Bold-coloured terminal marker — green ✓ on success, red ✗ on
        // failure.  Skipped in ASCII mode to avoid mojibake on legacy
        // 8-bit terminals where the ANSI escape would still work but
        // the Unicode glyph would render as garbage.
        if (m_unicode) {
            std::fputs(m_failed ? FAIL_MARK_UTF8 : COMPLETE_MARK_UTF8, out);
        }
        std::fputc('\n', out);
        std::fflush(out);
    } else {
        std::cout.flush();
    }
}
