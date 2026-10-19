/***************************************************************************
 * fileops_test.cpp — neurosuite/core/fileops.hpp.
 *
 *   - replaceFile onto a new name, and onto an EXISTING file (which
 *     std::rename refuses on Windows): the destination gets the source's
 *     content and the source is gone;
 *   - replaceFile from a missing file fails with a reason;
 *   - syncFile on a written file; processId() is positive.
 * Exits 0 on success, 1 if any check fails.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include <neurosuite/core/fileops.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace neurosuite::core;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static void put(const fs::path& p, const std::string& text)
{
    std::ofstream(p, std::ios::binary) << text;
}
static std::string get(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ns_fileops_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    put(dir / "a.tmp", "new");
    check(replaceFile((dir / "a.tmp").string(), (dir / "a").string()), "replaceFile onto a new name");
    check(get(dir / "a") == "new" && !fs::exists(dir / "a.tmp"), "  the content moved");

    put(dir / "a.tmp", "newer");
    std::string err;
    check(replaceFile((dir / "a.tmp").string(), (dir / "a").string(), &err),
          "replaceFile onto an existing file");
    check(get(dir / "a") == "newer" && !fs::exists(dir / "a.tmp"), "  the destination was replaced");

    err.clear();
    check(!replaceFile((dir / "missing").string(), (dir / "b").string(), &err) && !err.empty(),
          "replaceFile from a missing file fails with a reason");

    std::FILE* f = std::fopen((dir / "s").string().c_str(), "wb");
    std::fputs("sync", f);
    check(syncFile(f), "syncFile on a written file");
    std::fclose(f);
    check(get(dir / "s") == "sync", "  the data is in the file");

    check(processId() > 0, "processId() is positive");

    fs::remove_all(dir, ec);
    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
