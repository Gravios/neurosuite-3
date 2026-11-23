// processlinemaker_test.cpp — a child's output cut into lines, the two streams kept apart.
//
// A line a process writes in two pieces must come out as ONE line even when the other stream
// speaks between the pieces.  It used to be cut there: the recluster pane showed
// "  mode: chu" / "nked  12 chunks", and test_recluster_klustakwik.py, which looks for
// "mode: chunked" in the pane, failed about one run in three.
//
//   - stderr written as "mode: chu" + (stdout line) + "nked\n": one stderr line "mode: chunked";
//   - the same the other way round for a stdout line;
//   - a last line without a newline is still delivered once the process has exited, on each stream;
//   - the lines of each stream arrive in the order written.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "processlinemaker.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what, const QStringList& got = {})
{
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what);
    if (!ok) {
        ++fails;
        for (const QString& l : got) std::printf("        [%s]\n", l.toLocal8Bit().constData());
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QProcess proc;
    ProcessLineMaker maker(&proc);
    QStringList out, err;
    bool over = false;
    QObject::connect(&maker, &ProcessLineMaker::receivedStdoutLine, [&](const QString& l) { out << l; });
    QObject::connect(&maker, &ProcessLineMaker::receivedStderrLine, [&](const QString& l) { err << l; });
    QObject::connect(&maker, &ProcessLineMaker::outputTreatmentOver, [&]() { over = true; });
    QObject::connect(&proc, &QProcess::finished, &maker, &ProcessLineMaker::slotProcessExited);

    // Each piece is its own write, with a pause, so each arrives as its own read.
    const char* script =
        "printf 'first err\\n' >&2; sleep 0.2;"
        "printf 'mode: chu' >&2; sleep 0.2;"
        "printf 'between, on stdout\\n'; sleep 0.2;"
        "printf 'nked\\n' >&2; sleep 0.2;"
        "printf 'out in two' ; sleep 0.2;"
        "printf 'between, on stderr\\n' >&2; sleep 0.2;"
        "printf ' pieces\\n'; sleep 0.2;"
        "printf 'err tail' >&2; printf 'out tail'";
    maker.reset();
    proc.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QString::fromLatin1(script)});
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    QObject::connect(&maker, &ProcessLineMaker::outputTreatmentOver, &loop, &QEventLoop::quit);
    loop.exec();
    QCoreApplication::processEvents();

    check(over && proc.state() == QProcess::NotRunning, "the process ran and its output was reported over");
    check(err.contains(QStringLiteral("mode: chunked")), "a stderr line written in two pieces around a stdout line is ONE line", err);
    check(out.contains(QStringLiteral("out in two pieces")), "a stdout line written in two pieces around a stderr line is ONE line", out);
    check(err == QStringList({QStringLiteral("first err"), QStringLiteral("mode: chunked"),
                              QStringLiteral("between, on stderr"), QStringLiteral("err tail")}),
          "stderr: every line, in order, the last one without its newline too", err);
    check(out == QStringList({QStringLiteral("between, on stdout"), QStringLiteral("out in two pieces"),
                              QStringLiteral("out tail")}),
          "stdout: every line, in order, the last one without its newline too", out);

    std::printf(fails ? "%d check(s) failed\n" : "processlinemaker: all checks passed\n", fails);
    return fails ? 1 : 0;
}
