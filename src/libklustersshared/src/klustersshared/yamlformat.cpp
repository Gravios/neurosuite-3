/***************************************************************************
 * yamlformat.cpp  -  see yamlformat.h
 ***************************************************************************/

#include "yamlformat.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace yamlformat {

namespace {

// ---------------------------------------------------------------------------
// Paths
//
// A node is identified by the keys and list items leading to it, joined with
// a separator that cannot occur in a key.  A list item is "name=<value>" when
// its first key is `name` (programs, parameters: stable when items are added,
// removed or reordered), otherwise "#<index>".  The scanner and the emitter
// build paths the same way.
// ---------------------------------------------------------------------------
const char SEP = '\x1f';

std::string itemId(const std::string& nameValue, bool hasName, int index)
{
    return hasName ? "name=" + nameValue : "#" + std::to_string(index);
}

// What the source text says about one path.
struct PathInfo {
    std::vector<std::string> before;  // comment / blank lines preceding it, verbatim
    std::string eol;                  // end-of-line comment incl. leading spaces
    char quote = 0;                   // ' or " when the value was quoted
    std::string value;                // the value as written (unquoted), scalars only
    std::string raw;                  // the value exactly as written (flow / scalar)
    std::string gap = " ";            // blanks between ':' (or '-') and the value
    int childOffset = -1;             // block child's indentation relative to the key
    bool emptyValue = false;          // `key:` with nothing after it
};

struct Layout {
    std::vector<std::string> header;  // comments before the first content line
    std::vector<std::string> trailer; // comments after the last one
    std::map<std::string, PathInfo> at;
};

std::string trimLeft(const std::string& s)
{
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string trimRight(const std::string& s)
{
    size_t n = s.size();
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) --n;
    return s.substr(0, n);
}

// Index of the '#' that starts an end-of-line comment in @p s, or npos.  A '#'
// starts a comment only after whitespace and outside quotes; a quote opens a
// quoted scalar only where a scalar can start (so "don't" is not a quote).
size_t commentStart(const std::string& s)
{
    bool sq = false, dq = false;
    char prevSig = 0;  // last non-blank character outside quotes
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (dq) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') { dq = false; prevSig = c; }
            continue;
        }
        if (sq) {
            if (c == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
                sq = false; prevSig = c;
            }
            continue;
        }
        const bool scalarStart = prevSig == 0 || prevSig == ':' || prevSig == '[' ||
                                 prevSig == '{' || prevSig == ',' || prevSig == '-';
        if (c == '"' && scalarStart) { dq = true; continue; }
        if (c == '\'' && scalarStart) { sq = true; continue; }
        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) return i;
        if (c != ' ' && c != '\t') prevSig = c;
    }
    return std::string::npos;
}

// Splits "key: value  # c" (or "key:") into its parts.  Returns false when the
// text is not a mapping entry.
bool splitKey(const std::string& t, std::string& key, std::string& value, std::string& eol,
              std::string* gapOut = nullptr)
{
    // The key ends at the first ':' followed by a blank or end of line, outside
    // quotes.  A quoted key is unquoted.
    size_t i = 0;
    std::string k;
    if (!t.empty() && (t[0] == '"' || t[0] == '\'')) {
        const char q = t[0];
        size_t j = 1;
        for (; j < t.size(); ++j) {
            if (q == '\'' && t[j] == '\'' && j + 1 < t.size() && t[j + 1] == '\'') { k += '\''; ++j; continue; }
            if (q == '"' && t[j] == '\\' && j + 1 < t.size()) { k += t[++j]; continue; }
            if (t[j] == q) break;
            k += t[j];
        }
        if (j >= t.size()) return false;
        i = j + 1;
        while (i < t.size() && t[i] == ' ') ++i;
        if (i >= t.size() || t[i] != ':') return false;
    } else {
        if (t.empty() || t[0] == '[' || t[0] == '{' || t[0] == '#') return false;
        for (; i < t.size(); ++i) {
            if (t[i] == ':' && (i + 1 == t.size() || t[i + 1] == ' ' || t[i + 1] == '\t')) break;
            if (t[i] == ' ' && i + 1 < t.size() && t[i + 1] == '#') return false;
        }
        if (i >= t.size()) return false;
        k = trimRight(t.substr(0, i));
    }
    key = k;
    std::string rest = t.substr(i + 1);
    const size_t cs = commentStart(rest);
    std::string v = cs == std::string::npos ? rest : rest.substr(0, cs);
    v = trimRight(v);
    const std::string vt = trimLeft(v);
    if (vt.empty()) {
        eol = cs == std::string::npos ? std::string() : trimRight(rest);
    } else {
        // spaces between the value and the '#', then the comment
        eol = cs == std::string::npos ? std::string()
                                      : trimRight(rest.substr(v.size()));
    }
    value = vt;
    if (gapOut) *gapOut = vt.empty() ? std::string(" ") : v.substr(0, v.size() - vt.size());
    return true;
}

// Unquotes a scalar as written; reports the quote character.
std::string unquote(const std::string& v, char& quote)
{
    quote = 0;
    if (v.size() >= 2 && (v[0] == '\'' || v[0] == '"') && v.back() == v[0]) {
        quote = v[0];
        std::string out;
        for (size_t i = 1; i + 1 < v.size(); ++i) {
            if (quote == '\'' && v[i] == '\'' && i + 2 < v.size() && v[i + 1] == '\'') { out += '\''; ++i; continue; }
            if (quote == '"' && v[i] == '\\' && i + 2 < v.size()) {
                const char e = v[++i];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                continue;
            }
            out += v[i];
        }
        return out;
    }
    return v;
}

int flowBalance(const std::string& v)
{
    int depth = 0;
    bool sq = false, dq = false;
    for (size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (dq) { if (c == '\\') ++i; else if (c == '"') dq = false; continue; }
        if (sq) { if (c == '\'') sq = false; continue; }
        if (c == '"') dq = true;
        else if (c == '\'') sq = true;
        else if (c == '[' || c == '{') ++depth;
        else if (c == ']' || c == '}') --depth;
    }
    return depth;
}

// ---------------------------------------------------------------------------
// Scanner: reads the layout of block-style YAML text.  It only has to follow
// the structure well enough to name paths; anything it cannot place is left
// out (its comments are then lost, never misplaced into the data).
// ---------------------------------------------------------------------------
Layout scan(const std::string& text)
{
    Layout L;
    struct Frame { int col; std::string path; bool seq; int idx; };
    std::vector<Frame> st;
    st.push_back({-1, std::string(), false, 0});

    std::vector<std::string> pending;
    bool seenContent = false;
    bool haveOpen = false;
    std::string openPath;
    int openCol = -1;
    bool openIsItem = false;  // the open node is a bare "-" item
    int skipCol = -1;       // inside a block scalar: skip lines deeper than this
    int flowDepth = 0;      // inside a multi-line flow collection
    std::vector<std::string> skipBlanks;  // blank lines seen while skipping

    auto attach = [&](const std::string& path) -> PathInfo& {
        PathInfo& info = L.at[path];
        info.before.insert(info.before.end(), pending.begin(), pending.end());
        pending.clear();
        return info;
    };
    auto noteValue = [&](PathInfo& info, const std::string& value, const std::string& eol, int col,
                         const std::string& gap) {
        info.eol = eol;
        info.gap = gap.empty() ? std::string(" ") : gap;
        info.emptyValue = value.empty();
        info.raw = value;
        info.value = unquote(value, info.quote);
        if (!value.empty() && (value[0] == '|' || value[0] == '>')) {
            skipCol = col;
            info.quote = 0;
        } else if (!value.empty() && (value[0] == '[' || value[0] == '{')) {
            flowDepth = flowBalance(value);
        }
    };

    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = trimRight(raw);
        const std::string t = trimLeft(line);
        const int c = static_cast<int>(line.size() - t.size());

        if (skipCol >= 0) {
            // Blank lines inside a block scalar are content; blank lines after
            // it separate it from what follows -- which is which is only known
            // at the next non-blank line.
            if (t.empty()) { skipBlanks.push_back(line); continue; }
            if (c > skipCol) { skipBlanks.clear(); continue; }
            skipCol = -1;
            pending.insert(pending.end(), skipBlanks.begin(), skipBlanks.end());
            skipBlanks.clear();
        }
        if (flowDepth > 0) {
            flowDepth += flowBalance(t);
            continue;
        }
        if (t.empty() || t[0] == '#') { pending.push_back(line); continue; }
        if (t == "---" || t == "...") continue;

        if (!seenContent) {
            L.header = pending;
            pending.clear();
            seenContent = true;
        }

        while (st.size() > 1 && st.back().col > c) st.pop_back();

        const bool isItem = t[0] == '-' && (t.size() == 1 || t[1] == ' ' || t[1] == '\t');
        if (isItem) {
            Frame* f = nullptr;
            if (st.back().seq && st.back().col == c) {
                f = &st.back();
            } else if (haveOpen && (c > openCol || (c == openCol && !openIsItem))) {
                L.at[openPath].childOffset = c - openCol;
                st.push_back({c, openPath, true, 0});
                f = &st.back();
            } else {
                continue;  // cannot place it
            }
            haveOpen = false;
            const std::string afterDash = t.substr(1);
            const std::string rest = trimLeft(afterDash);
            const int restCol = c + 1 + static_cast<int>(afterDash.size() - rest.size());

            std::string key, value, eol, gap;
            const bool hasKey = !rest.empty() && splitKey(rest, key, value, eol, &gap);
            char q = 0;
            const std::string id = itemId(hasKey ? unquote(value, q) : std::string(),
                                          hasKey && key == "name", f->idx++);
            const std::string itemPath = f->path + SEP + id;
            PathInfo& itemInfo = attach(itemPath);

            if (rest.empty()) {
                haveOpen = true; openPath = itemPath; openCol = c; openIsItem = true;
            } else if (hasKey) {
                const std::string parent = itemPath;
                st.push_back({restCol, parent, false, 0});
                PathInfo& info = L.at[parent + SEP + key];
                noteValue(info, value, eol, restCol, gap);
                if (value.empty()) { haveOpen = true; openPath = parent + SEP + key; openCol = restCol; openIsItem = false; }
            } else {
                std::string v = rest, e;
                const size_t cs = commentStart(rest);
                if (cs != std::string::npos) {
                    v = trimRight(rest.substr(0, cs));
                    e = trimRight(rest.substr(v.size()));
                }
                noteValue(itemInfo, v, e, c, afterDash.substr(0, afterDash.size() - rest.size()));
            }
            continue;
        }

        std::string key, value, eol, gap;
        if (!splitKey(t, key, value, eol, &gap)) continue;  // continuation of a scalar
        Frame* f = nullptr;
        if (st.size() == 1 && st.back().col < 0) st.back().col = c;  // root map
        if (!st.back().seq && st.back().col == c) {
            f = &st.back();
        } else if (haveOpen && c > openCol) {
            L.at[openPath].childOffset = c - openCol;
            st.push_back({c, openPath, false, 0});
            f = &st.back();
        } else {
            continue;
        }
        haveOpen = false;
        const std::string keyPath = f->path + SEP + key;
        PathInfo& info = attach(keyPath);
        noteValue(info, value, eol, c, gap);
        if (value.empty()) { haveOpen = true; openPath = keyPath; openCol = c; openIsItem = false; }
    }
    L.trailer = pending;
    return L;
}

// ---------------------------------------------------------------------------
// Scalars
// ---------------------------------------------------------------------------
bool numberLike(const std::string& s)
{
    if (s.empty()) return false;
    static const std::set<std::string> special = {
        ".inf", ".Inf", ".INF", "+.inf", "-.inf", "+.Inf", "-.Inf", ".nan", ".NaN", ".NAN"};
    if (special.count(s)) return true;
    const char* b = s.c_str();
    char* e = nullptr;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'o')) {
        std::strtoll(b + 2, &e, s[1] == 'x' ? 16 : 8);
        return e && *e == '\0';
    }
    if (std::isspace(static_cast<unsigned char>(s[0]))) return false;
    std::strtod(b, &e);
    return e && *e == '\0';
}

bool boolOrNullLike(const std::string& s)
{
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const std::set<std::string> words = {
        "", "~", "null", "true", "false", "yes", "no", "on", "off", "y", "n"};
    return words.count(l) != 0;
}

// True when @p s cannot be written as a plain scalar in the given context.
bool needsQuoting(const std::string& s, bool flow)
{
    if (s.empty()) return true;
    if (numberLike(s)) return false;
    if (s.front() == ' ' || s.back() == ' ' || s.front() == '\t' || s.back() == '\t') return true;
    static const std::string lead = "?:,[]{}#&*!|>'\"%@`";
    if (lead.find(s[0]) != std::string::npos) return true;
    if (s[0] == '-' && (s.size() == 1 || s[1] == ' ')) return true;
    if (s.back() == ':') return true;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7f) return true;
        if (s[i] == ':' && i + 1 < s.size() && s[i + 1] == ' ') return true;
        if (s[i] == '#' && i > 0 && s[i - 1] == ' ') return true;
        if (flow && (s[i] == ',' || s[i] == '[' || s[i] == ']' || s[i] == '{' || s[i] == '}')) return true;
    }
    return false;
}

std::string quoted(const std::string& s, char q)
{
    bool printable = true;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7f) printable = false;
    if (q == '\'' && printable) {
        std::string out = "'";
        for (char c : s) { if (c == '\'') out += '\''; out += c; }
        return out + "'";
    }
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                static const char* hex = "0123456789abcdef";
                out += "\\x"; out += hex[c >> 4]; out += hex[c & 15];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

// ---------------------------------------------------------------------------
// Emitter
// ---------------------------------------------------------------------------
class Writer {
public:
    explicit Writer(const Layout& layout) : L(layout) {}

    std::string run(const YAML::Node& doc)
    {
        for (const auto& l : L.header) out += l + "\n";
        if (doc.IsMap() && doc.size() > 0) {
            emitMap(doc, 0, std::string(), false);
        } else if (doc.IsSequence() && doc.size() > 0) {
            emitSeq(doc, 0, std::string());
        } else {
            out += scalarOrFlow(doc, std::string(), false) + "\n";
        }
        for (const auto& l : L.trailer) out += l + "\n";
        return out;
    }

private:
    const Layout& L;
    std::string out;
    std::set<std::string> used;

    const PathInfo* info(const std::string& path) const
    {
        const auto it = L.at.find(path);
        return it == L.at.end() ? nullptr : &it->second;
    }

    void emitBefore(const std::string& path)
    {
        const PathInfo* i = info(path);
        if (!i || used.count(path)) return;
        used.insert(path);
        for (const auto& l : i->before) out += l + "\n";
    }

    static bool allScalars(const YAML::Node& n)
    {
        for (const auto& e : n) if (!e.IsScalar() && !e.IsNull()) return false;
        return true;
    }

    // Collections written inline: those that were inline in the source, and
    // new lists of scalars (channels, positions).
    static bool isFlow(const YAML::Node& n)
    {
        if (!n.IsSequence() && !n.IsMap()) return false;
        if (n.size() == 0) return true;
        if (n.Style() == YAML::EmitterStyle::Flow) return true;
        return n.Style() == YAML::EmitterStyle::Default && n.IsSequence() && allScalars(n);
    }

    // Same content and quoting, so the text as written can stand.  A node the
    // caller created (no tag) accepts either quoting of the same text.
    static bool equivalent(const YAML::Node& a, const YAML::Node& b)
    {
        if (a.Type() != b.Type()) return false;
        if (a.IsScalar()) {
            if (a.Scalar() != b.Scalar()) return false;
            if (a.Tag().empty()) return true;
            return (a.Tag() == "!") == (b.Tag() == "!");
        }
        if (a.IsSequence() || a.IsMap()) {
            if (a.size() != b.size()) return false;
            if (a.IsSequence()) {
                for (size_t k = 0; k < a.size(); ++k) if (!equivalent(a[k], b[k])) return false;
                return true;
            }
            auto ib = b.begin();
            for (auto ia = a.begin(); ia != a.end(); ++ia, ++ib)
                if (ia->first.Scalar() != ib->first.Scalar() || !equivalent(ia->second, ib->second))
                    return false;
            return true;
        }
        return true;  // both null
    }

    // The value text as written in the source, when it still says what @p n says.
    static bool rawFits(const YAML::Node& n, const PathInfo* i)
    {
        if (!i || i->raw.empty() || i->raw[0] == '|' || i->raw[0] == '>' ||
            i->raw[0] == '&' || i->raw[0] == '*' || i->raw[0] == '!')
            return false;
        try {
            return equivalent(n, YAML::Load(i->raw));
        } catch (...) {
            return false;
        }
    }

    std::string scalarText(const YAML::Node& n, const PathInfo* i, bool flow) const
    {
        if (n.IsNull() || !n.IsDefined()) return (i && i->emptyValue && !flow) ? std::string() : "~";
        const std::string& s = n.Scalar();
        const std::string& tag = n.Tag();
        char q = 0;
        if (tag == "!") {
            // quoted in its source document
            q = (i && i->quote) ? i->quote : (s.find('\'') == std::string::npos ? '\'' : '"');
        } else if (tag.empty()) {
            // set by code: keep the quoting the path had, unless a number
            // replaced a quoted string (then the type changed on purpose)
            if (i && i->quote && (!numberLike(s) || numberLike(i->value))) q = i->quote;
            else if (boolOrNullLike(s)) q = '\'';
        }
        if (q) return quoted(s, q);
        if (tag == "?" && !flow) return s;  // plain in the source: still plain
        return needsQuoting(s, flow) ? quoted(s, s.find('\'') == std::string::npos ? '\'' : '"') : s;
    }

    std::string flowText(const YAML::Node& n, const std::string& path)
    {
        if (n.IsSequence()) {
            std::string s = "[";
            int idx = 0;
            for (const auto& e : n) {
                if (idx) s += ", ";
                const std::string p = path + SEP + itemId(std::string(), false, idx++);
                s += (e.IsSequence() || e.IsMap()) ? flowText(e, p) : scalarText(e, info(p), true);
            }
            return s + "]";
        }
        std::string s = "{";
        bool first = true;
        for (const auto& kv : n) {
            if (!first) s += ", ";
            first = false;
            const std::string k = kv.first.Scalar();
            const std::string p = path + SEP + k;
            s += (needsQuoting(k, true) ? quoted(k, '"') : k) + ": ";
            s += (kv.second.IsSequence() || kv.second.IsMap()) ? flowText(kv.second, p)
                                                               : scalarText(kv.second, info(p), true);
        }
        return s + "}";
    }

    std::string scalarOrFlow(const YAML::Node& n, const std::string& path, bool flow)
    {
        if (isFlow(n)) return flowText(n, path);
        return scalarText(n, info(path), flow);
    }

    static std::string spaces(int n) { return std::string(static_cast<size_t>(n), ' '); }

    // A multi-line string as a literal block, indented under its key.
    bool literalBlock(const YAML::Node& n, int indent, const std::string& eol)
    {
        if (!n.IsScalar()) return false;
        const std::string& s = n.Scalar();
        if (s.find('\n') == std::string::npos) return false;
        for (unsigned char c : s) if ((c < 0x20 && c != '\n') || c == 0x7f) return false;
        // A literal block cannot start with a blank-led line under auto-indentation.
        if (s[0] == ' ' || s[0] == '\n') return false;
        size_t trailing = 0;
        while (trailing < s.size() && s[s.size() - 1 - trailing] == '\n') ++trailing;
        const std::string body = s.substr(0, s.size() - trailing);
        if (body.empty()) return false;
        out += std::string(trailing == 0 ? " |-" : trailing == 1 ? " |" : " |+") + eol + "\n";
        std::istringstream lines(body);
        std::string l;
        while (std::getline(lines, l)) out += (l.empty() ? std::string() : spaces(indent + 2) + l) + "\n";
        for (size_t k = 1; k < trailing; ++k) out += "\n";
        return true;
    }

    void emitValue(const YAML::Node& v, int indent, const std::string& path)
    {
        const PathInfo* i = info(path);
        const std::string eol = i ? i->eol : std::string();
        const std::string gap = i ? i->gap : std::string(" ");
        const int child = indent + ((i && i->childOffset >= 0) ? i->childOffset : 2);
        if (rawFits(v, i)) {
            out += gap + i->raw + eol + "\n";
        } else if (isFlow(v)) {
            out += gap + flowText(v, path) + eol + "\n";
        } else if (v.IsMap()) {
            out += eol + "\n";
            emitMap(v, child, path, false);
        } else if (v.IsSequence()) {
            out += eol + "\n";
            emitSeq(v, i && i->childOffset >= 0 ? child : indent + 2, path);
        } else if (!literalBlock(v, indent, eol)) {
            const std::string t = scalarText(v, i, false);
            std::string e = eol;
            if (i && i->emptyValue && !t.empty()) {
                // `key:          # note` filled in: keep the note in its column
                size_t lead = 0;
                while (lead < e.size() && e[lead] == ' ') ++lead;
                const size_t take = std::min(lead > 0 ? lead - 1 : 0, gap.size() + t.size());
                e.erase(0, take);
            }
            out += (t.empty() ? std::string() : gap + t) + e + "\n";
        }
    }

    void emitMap(const YAML::Node& m, int indent, const std::string& path, bool firstInline)
    {
        bool first = true;
        for (const auto& kv : m) {
            const std::string k = kv.first.Scalar();
            const std::string kp = path + SEP + k;
            if (!(first && firstInline)) {
                emitBefore(kp);
                out += spaces(indent);
            }
            first = false;
            out += (needsQuoting(k, false) ? quoted(k, '"') : k) + ":";
            emitValue(kv.second, indent, kp);
        }
    }

    void emitSeq(const YAML::Node& s, int indent, const std::string& path)
    {
        int idx = 0;
        for (const auto& item : s) {
            bool hasName = false;
            std::string nameValue;
            if (item.IsMap() && item.size() > 0) {
                const auto firstKv = item.begin();
                if (firstKv->first.Scalar() == "name" && firstKv->second.IsScalar()) {
                    hasName = true;
                    nameValue = firstKv->second.Scalar();
                }
            }
            const std::string ip = path + SEP + itemId(nameValue, hasName, idx++);
            emitBefore(ip);
            const PathInfo* i = info(ip);
            const std::string eol = i ? i->eol : std::string();
            if (rawFits(item, i)) {
                out += spaces(indent) + "-" + i->gap + i->raw + eol + "\n";
            } else if (isFlow(item) || !(item.IsMap() || item.IsSequence())) {
                out += spaces(indent) + "- " + scalarOrFlow(item, ip, false) + eol + "\n";
            } else if (item.IsMap()) {
                out += spaces(indent) + "- ";
                emitMap(item, indent + 2, ip, true);
            } else {
                out += spaces(indent) + "-" + eol + "\n";
                emitSeq(item, indent + 2, ip);
            }
        }
    }
};

} // namespace

std::string emitWithLayout(const YAML::Node& doc, const std::string& sourceText)
{
    const Layout layout = sourceText.empty() ? Layout() : scan(sourceText);
    return Writer(layout).run(doc);
}

std::string readText(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace yamlformat
