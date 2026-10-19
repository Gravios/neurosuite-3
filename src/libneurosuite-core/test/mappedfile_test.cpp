/***************************************************************************
 * mappedfile_test.cpp — neurosuite/core/mappedfile.hpp and fileio64.hpp.
 *
 *   - a written file maps with the same bytes and size;
 *   - an empty file opens with size 0 and no data;
 *   - a missing file fails with a reason;
 *   - a moved-from map is empty, the moved-to one keeps the view;
 *   - fseek64 / ftell64 / fileSize64 handle a file past 4 GiB (sparse where
 *     the file system allows it), the case 32-bit long / off_t get wrong.
 * Exits 0 on success, 1 if any check fails.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include <neurosuite/core/fileio64.hpp>
#include <neurosuite/core/mappedfile.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using neurosuite::core::MappedFile;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ns_mappedfile_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Bytes that text mode would change: CR, LF, 0x1A (Ctrl-Z) and NUL.
    std::vector<unsigned char> bytes(70000);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>((i * 37) & 0xff);
    bytes[10] = '\r'; bytes[11] = '\n'; bytes[12] = 0x1a; bytes[13] = 0;
    const std::string full = (dir / "data.bin").string();
    {
        std::FILE* f = std::fopen(full.c_str(), "wb");
        std::fwrite(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);
    }

    MappedFile m;
    std::string err;
    check(m.open(full, &err), "a file maps");
    check(m.size() == static_cast<std::int64_t>(bytes.size()), "its size is the file's size");
    check(m.data() && std::memcmp(m.data(), bytes.data(), bytes.size()) == 0,
          "its bytes are the file's bytes (CR, LF, 0x1A, NUL untouched)");
    m.adviseSequential();
    check(neurosuite::core::fileSize64(full) == static_cast<std::int64_t>(bytes.size()),
          "fileSize64 agrees");

    MappedFile moved(std::move(m));
    check(!m.data() && m.size() == 0, "a moved-from map is empty");
    check(moved.data() && moved.size() == static_cast<std::int64_t>(bytes.size()),
          "the moved-to map keeps the view");
    moved.close();
    check(!moved.data() && !moved.isOpen(), "close() releases the map");

    const std::string empty = (dir / "empty.bin").string();
    { std::FILE* f = std::fopen(empty.c_str(), "wb"); std::fclose(f); }
    MappedFile e;
    check(e.open(empty) && e.size() == 0 && e.data() == nullptr, "an empty file opens with size 0");

    MappedFile missing;
    err.clear();
    check(!missing.open((dir / "no-such-file").string(), &err) && !err.empty(),
          "a missing file fails with a reason");
    check(neurosuite::core::fileSize64((dir / "no-such-file").string()) == -1,
          "fileSize64 of a missing file is -1");

    // Past 4 GiB: write one marker byte at offset 2^32 + 5.
    const std::int64_t past4G = (std::int64_t{1} << 32) + 5;
    const std::string big = (dir / "big.bin").string();
    bool wrote = false;
    {
        std::FILE* f = std::fopen(big.c_str(), "wb");
        if (f && neurosuite::core::fseek64(f, past4G, SEEK_SET) == 0 && std::fputc(0x5a, f) == 0x5a)
            wrote = true;
        if (f) std::fclose(f);
    }
    if (!wrote) {
        std::printf("  [SKIP] no room for a 4 GiB file here\n");
    } else {
        check(neurosuite::core::fileSize64(big) == past4G + 1, "fileSize64 past 4 GiB");
        std::FILE* f = std::fopen(big.c_str(), "rb");
        check(neurosuite::core::fseek64(f, past4G, SEEK_SET) == 0 && std::fgetc(f) == 0x5a,
              "fseek64 reaches a byte past 4 GiB");
        check(neurosuite::core::ftell64(f) == past4G + 1, "ftell64 past 4 GiB");
        std::fclose(f);
    }
    fs::remove_all(dir, ec);

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
