// ════════════════════════════════════════════════════════════════════════════
//  channel_expr.hpp — the expression form of a custom spatial pattern
//  (sdiffPairs written with channel names; spatial order 6, method stderiv_C6).
//
//  A spike group's pattern is a comma-separated list with one arithmetic
//  expression per output channel, in channel order: element k is output
//  channel k.  Channels of the group are c0, c1, … (0-based positions in the
//  group's channel list); numbers are constants.
//
//      sdiffPairs: c0 - c1, c1 - (c0 + c2)/2, c2 - 0.5*c3, (c3 - c2)^2
//
//  Grammar, in the usual order of precedence (loosest first):
//
//      list    := expr { ',' expr }
//      expr    := term { ('+' | '-') term }          left-associative
//      term    := unary { ('*' | '/') unary }        left-associative
//      unary   := ('+' | '-') unary | power
//      power   := primary [ '^' unary ]              right-associative
//      primary := number | channel | '(' expr ')'
//      channel := ('c' | 'C') digits
//      number  := digits [ '.' digits ] [ exponent ] | '.' digits [ exponent ]
//
//  so -c0^2 is -(c0^2), 2^3^2 is 2^(3^2) and a-b-c is (a-b)-c.  Blanks (any
//  ASCII whitespace) are allowed anywhere between tokens.  Digits are ASCII
//  0-9, and numbers are read independently of the C locale (a German locale's
//  decimal comma does not turn 0.5 into 0).  Every element must use at least
//  one channel.  There is no implicit multiplication (2c1 is an error: write
//  2*c1).  Parentheses and signs nest at most kMaxNest deep.
//
//  Any arithmetic is allowed, so the transform need not be linear.  A result
//  that is not finite (a division by zero, an overflowing power) is 0, and a
//  result beyond +-1e9 is held there, so no caller's integer conversion can
//  overflow.  The value is then rounded and saturated to int16 like every
//  other order (stderiv_transform.hpp), which the hold does not change.
//
//  Compiled once into a small stack program; eval() takes the group's channel
//  values through an accessor, so callers holding a compact row, a strided
//  frame or a channel-major window share one evaluator.  Header-only, std-only.
// ════════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_CHANNEL_EXPR_HPP
#define NEUROSUITE_CORE_CHANNEL_EXPR_HPP

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

namespace neurosuite {
namespace core {

// One output channel's expression, compiled to postfix.
struct ChannelExpr {
    enum class Op : uint8_t { Num, Chan, Neg, Add, Sub, Mul, Div, Pow };
    struct Ins { Op op; int chan; double num; };

    static constexpr int    kMaxDepth = 64;    // evaluation stack; deeper is refused
    static constexpr int    kMaxNest  = 64;    // nesting of ( ) and signs while parsing
    static constexpr double kLimit    = 1e9;   // results are held within +-kLimit

    std::vector<Ins> code;
    int maxChan = -1;          // highest channel position used
    int depth   = 0;           // stack slots the program needs
    std::string text;          // the element as written, for messages

    // Value for the channel values x(0), x(1), … (x(i) returns a double).
    template <class Get>
    double eval(Get x) const {
        double st[kMaxDepth];
        int sp = 0;
        for (const Ins& in : code) {
            switch (in.op) {
                case Op::Num:  st[sp++] = in.num; break;
                case Op::Chan: st[sp++] = static_cast<double>(x(in.chan)); break;
                case Op::Neg:  st[sp - 1] = -st[sp - 1]; break;
                case Op::Add:  --sp; st[sp - 1] = st[sp - 1] + st[sp]; break;
                case Op::Sub:  --sp; st[sp - 1] = st[sp - 1] - st[sp]; break;
                case Op::Mul:  --sp; st[sp - 1] = st[sp - 1] * st[sp]; break;
                case Op::Div:  --sp; st[sp - 1] = st[sp - 1] / st[sp]; break;
                case Op::Pow:  --sp; st[sp - 1] = std::pow(st[sp - 1], st[sp]); break;
            }
        }
        const double v = sp == 1 ? st[0] : 0.0;
        if (!std::isfinite(v)) return 0.0;
        return v > kLimit ? kLimit : (v < -kLimit ? -kLimit : v);
    }
};

// One spike group's pattern: output channel k = exprs[k].
struct ChannelExprSet {
    std::vector<ChannelExpr> exprs;
    int maxChan = -1;          // highest channel position any element uses

    int  size()  const { return static_cast<int>(exprs.size()); }
    bool empty() const { return exprs.empty(); }

    // True when the pattern applies to a group of @p nChan channels: one element
    // per channel, and no element names a channel the group does not have.
    bool fits(int nChan, std::string* why = nullptr) const {
        if (size() == nChan && maxChan < nChan) return true;   // the per-sample test: O(1)
        if (size() != nChan) {
            if (why) *why = std::to_string(size()) + " expressions for " + std::to_string(nChan)
                          + " channels (one per channel, in channel order)";
            return false;
        }
        for (int k = 0; k < size(); ++k)
            if (exprs[static_cast<size_t>(k)].maxChan >= nChan) {
                if (why) *why = "expression " + std::to_string(k) + " ('"
                              + exprs[static_cast<size_t>(k)].text + "') uses c"
                              + std::to_string(exprs[static_cast<size_t>(k)].maxChan)
                              + " but the group has c0..c" + std::to_string(nChan - 1);
                return false;
            }
        return true;
    }

    // Output channel @p out from a compact row of the group's channel values.
    double eval(int out, const double* row) const {
        return exprs[static_cast<size_t>(out)].eval([row](int i) { return row[i]; });
    }
    double eval(int out, double* row) const { return eval(out, static_cast<const double*>(row)); }
    template <class Get>
    double eval(int out, Get x) const { return exprs[static_cast<size_t>(out)].eval(x); }
};

// True when @p spec is written in the expression form (it names a channel cN).
// The older forms ("a-b,…", "a-b+c,…") use bare positions and no letters.
namespace detail {
inline bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
// A character for a message: itself, or a description when it is not printable ASCII.
inline std::string shown(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20 || u >= 0x7f) return "a non-ASCII or control character";
    return "'" + std::string(1, c) + "'";
}
}  // namespace detail

inline bool isChannelExprSpec(const std::string& spec) {
    for (char ch : spec)
        if (ch == 'c' || ch == 'C') return true;
    return false;
}

namespace detail {

class ChannelExprParser {
public:
    ChannelExprParser(const std::string& s, ChannelExpr& out) : s_(s), out_(out) {}

    bool run(std::string* error) {
        bool ok = expr();
        if (ok) {
            skip();
            if (p_ != s_.size())
                ok = err(s_[p_] == ')' ? "has a ')' without its '('"
                                       : "has " + shown(s_[p_]) + " where an operator is expected");
            else if (out_.maxChan < 0)
                ok = err("uses no channel (channels are written c0, c1, ...)", false);
            else if (out_.depth > ChannelExpr::kMaxDepth)
                ok = err("is nested too deeply", false);
        }
        if (!ok && error) {
            // A long element is shown by its start: the column still locates the problem.
            const std::string shownText = s_.size() > 60 ? s_.substr(0, 57) + "..." : s_;
            *error = "'" + shownText + "' " + msg_ + (col_ ? " (column " + std::to_string(p_ + 1) + ")" : "");
        }
        return ok;
    }

private:
    const std::string& s_;
    ChannelExpr& out_;
    size_t p_ = 0;
    int sp_ = 0;
    int nest_ = 0;
    std::string msg_;
    bool col_ = true;

    bool err(const std::string& m, bool col = true) { msg_ = m; col_ = col; return false; }
    void skip() { while (p_ < s_.size() && isBlank(s_[p_])) ++p_; }
    bool peek(char c) { skip(); return p_ < s_.size() && s_[p_] == c; }
    // Not named emit: Qt defines emit as an empty macro, and Qt files include this.
    void push(ChannelExpr::Op op, int chan = -1, double num = 0.0) {
        out_.code.push_back({op, chan, num});
        if (op == ChannelExpr::Op::Num || op == ChannelExpr::Op::Chan) {
            if (++sp_ > out_.depth) out_.depth = sp_;
        } else if (op != ChannelExpr::Op::Neg) {
            --sp_;
        }
    }

    bool expr() {
        if (!term()) return false;
        while (peek('+') || peek('-')) {
            const char op = s_[p_++];
            if (!term()) return false;
            push(op == '+' ? ChannelExpr::Op::Add : ChannelExpr::Op::Sub);
        }
        return true;
    }
    bool term() {
        if (!unary()) return false;
        while (peek('*') || peek('/')) {
            const char op = s_[p_++];
            if (!unary()) return false;
            push(op == '*' ? ChannelExpr::Op::Mul : ChannelExpr::Op::Div);
        }
        return true;
    }
    // Every recursion of the descent passes through unary(), so its depth is the
    // parse's: bounded, so no input can exhaust the stack.
    bool unary() {
        struct Nest { int& n; explicit Nest(int& x) : n(x) { ++n; } ~Nest() { --n; } } nest(nest_);
        if (nest_ > ChannelExpr::kMaxNest) return err("is nested too deeply", false);
        if (peek('-')) { ++p_; if (!unary()) return false; push(ChannelExpr::Op::Neg); return true; }
        if (peek('+')) { ++p_; return unary(); }
        return power();
    }
    bool power() {
        if (!primary()) return false;
        if (peek('^')) {
            ++p_;
            if (!unary()) return false;          // right-associative: 2^3^2 = 2^(3^2)
            push(ChannelExpr::Op::Pow);
        }
        return true;
    }
    bool primary() {
        skip();
        if (p_ >= s_.size()) return err("ends where a channel, number or '(' is expected");
        const char c = s_[p_];
        if (c == '(') {
            ++p_;
            if (!expr()) return false;
            if (!peek(')')) return err("is missing a ')'");
            ++p_;
            return true;
        }
        if (c == 'c' || c == 'C') {
            ++p_;
            const size_t start = p_;
            long v = 0;
            while (p_ < s_.size() && isDigit(s_[p_])) {
                v = v * 10 + (s_[p_] - '0');
                if (v > 1000000) return err("has a channel number that is too large");
                ++p_;
            }
            if (p_ == start) { p_ = start - 1; return err("has a 'c' without a channel number"); }
            const int ch = static_cast<int>(v);
            if (ch > out_.maxChan) out_.maxChan = ch;
            push(ChannelExpr::Op::Chan, ch);
            return true;
        }
        if (isDigit(c) || c == '.') {
            const size_t start = p_;
            while (p_ < s_.size() && isDigit(s_[p_])) ++p_;
            if (p_ < s_.size() && s_[p_] == '.') {
                ++p_;
                while (p_ < s_.size() && isDigit(s_[p_])) ++p_;
            }
            if (p_ - start == 1 && c == '.') { p_ = start; return err("has a '.' that is not a number"); }
            if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
                size_t q = p_ + 1;
                if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) ++q;
                if (q < s_.size() && isDigit(s_[q])) {
                    p_ = q;
                    while (p_ < s_.size() && isDigit(s_[p_])) ++p_;
                }
            }
            // from_chars: the C locale's decimal point, whatever the process locale
            // (Qt applications call setlocale), and correctly rounded like Python's float().
            double value = 0.0;
            const std::from_chars_result r =
                std::from_chars(s_.data() + start, s_.data() + p_, value, std::chars_format::general);
            if (r.ec != std::errc() || r.ptr != s_.data() + p_) {
                p_ = start;
                return err("has a number that cannot be read");
            }
            push(ChannelExpr::Op::Num, -1, value);
            // "2c1": a number followed directly by a channel or '(' is not a product.
            skip();
            if (p_ < s_.size() && (s_[p_] == 'c' || s_[p_] == 'C' || s_[p_] == '('))
                return err("needs a '*' between a number and what follows it");
            return true;
        }
        return err("has " + shown(c) + " where a channel, number or '(' is expected");
    }
};

}  // namespace detail

// Compiles one element.  False with @p error (the element, the problem and its
// column) when it does not follow the grammar.
inline bool parseChannelExpr(const std::string& element, ChannelExpr& out, std::string* error = nullptr) {
    out = ChannelExpr();
    // Trim, so messages and text show the element as written without padding.
    size_t a = element.find_first_not_of(" \t\n\r\f\v"), b = element.find_last_not_of(" \t\n\r\f\v");
    out.text = (a == std::string::npos) ? std::string() : element.substr(a, b - a + 1);
    if (out.text.empty()) { if (error) *error = "an empty expression"; return false; }
    detail::ChannelExprParser parser(out.text, out);
    return parser.run(error);
}

// Compiles a comma-separated list: output channel k = element k.  Commas inside
// parentheses do not occur in the grammar, so a plain split is exact.
inline bool parseChannelExprs(const std::string& spec, ChannelExprSet& out, std::string* error = nullptr) {
    out = ChannelExprSet();
    size_t p = 0;
    int k = 0;
    while (true) {
        const size_t c = spec.find(',', p);
        const std::string el = spec.substr(p, c == std::string::npos ? std::string::npos : c - p);
        ChannelExpr e;
        std::string why;
        if (!parseChannelExpr(el, e, &why)) {
            if (error) *error = "sdiffPairs expression " + std::to_string(k) + ": " + why;
            out = ChannelExprSet();
            return false;
        }
        if (e.maxChan > out.maxChan) out.maxChan = e.maxChan;
        out.exprs.push_back(std::move(e));
        ++k;
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return true;
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_CHANNEL_EXPR_HPP
