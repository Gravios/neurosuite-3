// subsessionfiles_test.cpp -- the recluster sub-session's file operations.
//
// On a synthetic group (5 spikes, two .res variants, a .spk, a .fet with 3 dims, a tagged .clu that must
// be left out, another group's files that must be left out):
//   * groupFiles finds exactly the group's untagged files; variantOf / withVariant name them;
//   * copyRecords writes the chosen 1-based rows in the given order, header kept, for each record size
//     (.res 8, .spk nsamp*nchan*2, .fet 4 + 8*ndim), and refuses a row beyond the file or a wrong size;
//   * writeClu writes the int32 distinct count then the ids, readClu reads them back;
//   * removeAll removes every `<tmp>.*` file and symlink and nothing else.
#include "subsessionfiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    std::printf("%s %s\n", ok ? "  ok:" : "FAIL:", what.c_str());
    g_fail += !ok;
}

template <typename T> static void writeRaw(const QString& p, const std::vector<T>& v, const QByteArray& header = {})
{
    QFile f(p); f.open(QIODevice::WriteOnly);
    if (!header.isEmpty()) f.write(header);
    f.write(reinterpret_cast<const char*>(v.data()), static_cast<qint64>(v.size() * sizeof(T)));
}
template <typename T> static std::vector<T> readRaw(const QString& p, qint64 skip = 0)
{
    QFile f(p); f.open(QIODevice::ReadOnly); f.seek(skip);
    const QByteArray b = f.readAll();
    std::vector<T> v(static_cast<size_t>(b.size()) / sizeof(T));
    memcpy(v.data(), b.constData(), v.size() * sizeof(T));
    return v;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace subsession;
    QTemporaryDir td;
    const QString dir = td.path(), base = QStringLiteral("rat"), g = QStringLiteral("6");
    const QString B = dir + QLatin1Char('/') + base;
    // 5 spikes
    writeRaw<int64_t>(B + ".res.6", {100, 200, 300, 400, 500});
    writeRaw<int64_t>(B + ".res.stderiv_S3.6", {101, 201, 301, 401, 501});
    writeRaw<int64_t>(B + ".res.7", {1, 2, 3});                                   // another group
    std::vector<int16_t> spk; for (int s = 0; s < 5; ++s) for (int k = 0; k < 6; ++k) spk.push_back(static_cast<int16_t>(s * 10 + k));
    writeRaw<int16_t>(B + ".spk.stderiv.6", spk);                                 // 3 samples x 2 ch
    std::vector<int64_t> fet; for (int s = 0; s < 5; ++s) for (int k = 0; k < 3; ++k) fet.push_back(s * 100 + k);
    const int32_t nd = 3;
    writeRaw<int64_t>(B + ".fet.stderiv.6", fet, QByteArray(reinterpret_cast<const char*>(&nd), 4));
    writeRaw<int32_t>(B + ".clu.stderiv.6.curated", {3, 2, 2, 3, 2, 2});          // tagged: not a group file
    writeRaw<int64_t>(B + ".res.6.realigned", {1});                                // tagged: not a group file

    QStringList res = groupFiles(dir, base, "res", g);
    check(res.size() == 2 && res[0].endsWith(".res.6") && res[1].endsWith(".res.stderiv_S3.6"),
          "groupFiles: the group's two .res (not group 7, not the tagged one)");
    check(groupFiles(dir, base, "clu", g).isEmpty(), "groupFiles: a tagged .clu is not a group file");
    check(variantOf(res[1], base, "res", g) == "stderiv_S3" && variantOf(res[0], base, "res", g).isEmpty(), "variantOf");
    check(withVariant(dir + "/sub", "clu", "stderiv", g, "cur") == dir + "/sub.clu.stderiv.6.cur"
          && withVariant(dir + "/sub", "res", "", g) == dir + "/sub.res.6", "withVariant");

    const QVector<int64_t> rows{2, 4, 5};          // 1-based
    QString err;
    const QString S = dir + "/sub";
    check(copyRecords(res[0], S + ".res.6", 0, 8, 5, rows, &err), "copyRecords .res " + err.toStdString());
    check(readRaw<int64_t>(S + ".res.6") == std::vector<int64_t>{200, 400, 500}, "the sub .res holds rows 2,4,5 in order");
    check(copyRecords(B + ".spk.stderiv.6", S + ".spk.stderiv.6", 0, 12, 5, rows, &err), "copyRecords .spk " + err.toStdString());
    {
        const auto v = readRaw<int16_t>(S + ".spk.stderiv.6");
        check(v.size() == 18 && v[0] == 10 && v[5] == 15 && v[6] == 30 && v[12] == 40 && v[17] == 45, "the sub .spk rows are spikes 2,4,5 whole");
    }
    check(copyRecords(B + ".fet.stderiv.6", S + ".fet.stderiv.6", 4, 24, 5, rows, &err), "copyRecords .fet " + err.toStdString());
    {
        const auto hdr = readRaw<int32_t>(S + ".fet.stderiv.6"); const auto v = readRaw<int64_t>(S + ".fet.stderiv.6", 4);
        check(hdr[0] == 3 && v == std::vector<int64_t>{100, 101, 102, 300, 301, 302, 400, 401, 402}, "the sub .fet keeps the header and the rows");
    }
    check(!copyRecords(res[0], S + ".x", 0, 8, 5, QVector<int64_t>{6}, &err) && err.contains("beyond"), "a row beyond the file is refused: " + err.toStdString());
    check(!copyRecords(res[0], S + ".x", 0, 8, 4, rows, &err) && err.contains("not 4 records"), "a wrong record count is refused: " + err.toStdString());
    QFile::remove(S + ".x");
    check(writeClu(S + ".clu.stderiv.6", QVector<int32_t>{2, 3, 3}, &err), "writeClu");
    check(readRaw<int32_t>(S + ".clu.stderiv.6") == std::vector<int32_t>{2, 2, 3, 3}, "the seed .clu: distinct count 2, then the ids");
    {
        QVector<int32_t> back;
        check(readClu(S + ".clu.stderiv.6", &back, &err) && back == QVector<int32_t>{2, 3, 3}, "readClu reads the ids back (header skipped)");
        writeRaw<int8_t>(S + ".bad", {1, 2, 3});
        check(!readClu(S + ".bad", &back, &err) && err.contains("no binary"), "readClu refuses a file that is no binary .clu");
    }
    QFile::link("rat.res.6", S + ".fil");
    QDir(dir).mkdir("sub.dir");
    const int n = removeAll(S);
    check(n == 7, "removeAll removed the 5 files, the link and the directory (" + std::to_string(n) + ")");
    check(QDir(dir).entryList(QStringList{"sub.*"}, QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).isEmpty(), "nothing of sub.* is left");
    check(QFile::exists(B + ".res.6") && QFile::exists(B + ".spk.stderiv.6"), "the session's files are untouched");
    std::printf("%s (%d checks, %d failed)\n", g_fail ? "FAILED" : "OK", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
