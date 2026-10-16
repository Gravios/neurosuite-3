/* hierarchyfiles.cpp -- see hierarchyfiles.h. */
#include "hierarchyfiles.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

#include <unistd.h>

namespace hierarchyfiles {

namespace fs = std::filesystem;

NestingScan scanNesting(const std::vector<int>& parent, const std::vector<int>& child)
{
    NestingScan out;
    if (parent.size() != child.size()) return out;
    std::set<int> bad;
    for (std::size_t i = 0; i < parent.size(); ++i) {
        const auto ins = out.childToParent.emplace(child[i], parent[i]);
        if (!ins.second && ins.first->second != parent[i]) bad.insert(child[i]);
    }
    out.straddlers.assign(bad.begin(), bad.end());
    out.ok = true;
    return out;
}

std::vector<int32_t> clpFromMap(const std::map<int, int>& childToParent)
{
    int nAtoms = 0;
    if (!childToParent.empty()) nAtoms = std::max(0, childToParent.rbegin()->first);
    std::vector<int32_t> clp(static_cast<std::size_t>(nAtoms), 0);
    for (const auto& [c, p] : childToParent)
        if (c >= 1) clp[static_cast<std::size_t>(c - 1)] = static_cast<int32_t>(p);
    return clp;
}

int distinctCount(const std::vector<int>& ids)
{
    std::vector<int> s(ids);
    std::sort(s.begin(), s.end());
    return static_cast<int>(std::unique(s.begin(), s.end()) - s.begin());
}

Promotion promoteFlat(const std::vector<int>& clu)
{
    Promotion out;
    out.clc = clu;
    out.clcCount = distinctCount(clu);
    std::map<int, int> self;
    for (int id : clu) self.emplace(id, id);
    out.clp = clpFromMap(self);
    return out;
}

std::string siblingPath(const std::string& cluPath, const std::string& type)
{
    const std::size_t slash = cluPath.find_last_of('/');
    const std::size_t nameStart = (slash == std::string::npos) ? 0 : slash + 1;
    const std::size_t at = cluPath.rfind(".clu.");
    if (at == std::string::npos || at < nameStart) return std::string();
    std::string out = cluPath;
    out.replace(at, 5, "." + type + ".");
    return out;
}

std::string stagedPath(const std::string& path) { return path + ".saving"; }

namespace {
bool stageImpl(const std::string& path, int32_t header, const std::vector<int32_t>& ids,
               std::string* error)
{
    const std::string tmp = stagedPath(path);
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot create " + tmp + ": " + std::strerror(errno);
        return false;
    }
    bool ok = std::fwrite(&header, sizeof(int32_t), 1, f) == 1;
    if (ok && !ids.empty()) {
        ok = std::fwrite(ids.data(), sizeof(int32_t), ids.size(), f) == ids.size();
    }
    // Flushed to the disk before it can be installed: a staged file that a
    // crash could leave half-written must never be renamed over a good one.
    if (ok) ok = std::fflush(f) == 0 && ::fsync(::fileno(f)) == 0;
    const int writeErr = ok ? 0 : errno;
    ok = (std::fclose(f) == 0) && ok;        // a full disk can surface only at the flush
    if (!ok) {
        if (error) *error = "cannot write " + tmp + ": "
                          + std::strerror(writeErr ? writeErr : errno);
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}
} // namespace

bool stageIdFile(const std::string& path, int32_t header, const std::vector<int32_t>& ids,
                 std::string* error)
{
    return stageImpl(path, header, ids, error);
}

bool installStaged(const std::string& path, bool keepBackup, std::string* error)
{
    const std::string tmp = stagedPath(path);
    std::error_code ec;
    if (!fs::exists(tmp, ec)) {
        if (error) *error = "nothing staged for " + path;
        return false;
    }
    if (keepBackup && fs::exists(path, ec)) {
        fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) *error = "cannot back up " + path + " to .bak: " + ec.message();
            return false;
        }
    }
    // The rename gives the target the staged file's inode, created with the
    // process umask; carry the old file's mode over so a group-writable .clc in
    // a shared session directory stays group-writable.
    std::error_code permEc;
    const fs::file_status old = fs::status(path, permEc);
    if (!permEc && fs::exists(old))
        fs::permissions(tmp, old.permissions(), fs::perm_options::replace, permEc);
    fs::rename(tmp, path, ec);
    if (ec) {
        if (error) *error = "cannot replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

void discardStaged(const std::string& path)
{
    std::error_code ec;
    fs::remove(stagedPath(path), ec);
}

bool writeIdFile(const std::string& path, int32_t header, const std::vector<int32_t>& ids,
                 bool keepBackup, std::string* error)
{
    if (!stageIdFile(path, header, ids, error)) return false;
    if (!installStaged(path, keepBackup, error)) { discardStaged(path); return false; }
    return true;
}

IdFile readIdFile(const std::string& path)
{
    IdFile out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;
    in.seekg(0, std::ios::end);
    const std::streamoff bytes = in.tellg();
    in.seekg(0, std::ios::beg);
    if (bytes < 4 || bytes % 4 != 0) return out;
    in.read(reinterpret_cast<char*>(&out.header), 4);
    out.ids.resize(static_cast<std::size_t>((bytes - 4) / 4));
    if (!out.ids.empty())
        in.read(reinterpret_cast<char*>(out.ids.data()),
                static_cast<std::streamsize>(out.ids.size() * 4));
    out.ok = static_cast<bool>(in);
    return out;
}

} // namespace hierarchyfiles
