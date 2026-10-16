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
//  so -c0^2 is -(c0^2), 2^3^2 is 2^(3^2) and a-b-c is (a-b)-c.  Blanks are
//  allowed anywhere between tokens.  Every element must use at least one
//  channel.  There is no implicit multiplication (2c1 is an error: write 2*c1).
//
//  Any arithmetic is allowed, so the transform need not be linear.  A result
//  that is not finite (a division by zero, an overflowing power) is 0.  The
//  value is then rounded and saturated to int16 like every other order
//  (stderiv_transform.hpp).
//
//  Compiled once into a small stack program; eval() takes the group's channel
//  values through an accessor, so callers holding a compact row, a strided
//  frame or a channel-major window share one evaluator.  Header-only, std-only.
// ════════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_CHANNEL_EXPR_HPP
#define NEUROSUITE_CORE_CHANNEL_EXPR_HPP

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace neurosuite {
namespace core {

// One output channel's expression, compiled to postfix.
struct ChannelExpr {
    enum class Op : uint8_t { Num, Chan, Neg, Add, Sub, Mul, Div, Pow };
    struct Ins { Op op; int chan; double num; };

    static constexpr int kMaxDepth = 64;   // evaluation stack; deeper is refused

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
        return std::isfinite(v) ? v : 0.0;
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
    template <class Get>
    double eval(int out, Get x) const { return exprs[static_cast<size_t>(out)].eval(x); }
};

// True when @p spec is written in the expression form (it names a channel cN).
// The older forms ("a-b,…", "a-b+c,…") use bare positions and no letters.
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
                                       : "has '" + std::string(1, s_[p_]) + "' where an operator is expected");
            else if (out_.maxChan < 0)
                ok = err("uses no channel (channels are written c0, c1, ...)", false);
            else if (out_.depth > ChannelExpr::kMaxDepth)
                ok = err("is nested too deeply", false);
        }
        if (!ok && error)
            *error = "'" + s_ + "' " + msg_ + (col_ ? " (column " + std::to_string(p_ + 1) + ")" : "");
        return ok;
    }

private:
    const std::string& s_;
    ChannelExpr& out_;
    size_t p_ = 0;
    int sp_ = 0;
    std::string msg_;
    bool col_ = true;

    bool err(const std::string& m, bool col = true) { msg_ = m; col_ = col; return false; }
    void skip() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t')) ++p_; }
    bool peek(char c) { skip(); return p_ < s_.size() && s_[p_] == c; }
    void emit(ChannelExpr::Op op, int chan = -1, double num = 0.0) {
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
            emit(op == '+' ? ChannelExpr::Op::Add : ChannelExpr::Op::Sub);
        }
        return true;
    }
    bool term() {
        if (!unary()) return false;
        while (peek('*') || peek('/')) {
            const char op = s_[p_++];
            if (!unary()) return false;
            emit(op == '*' ? ChannelExpr::Op::Mul : ChannelExpr::Op::Div);
        }
        return true;
    }
    bool unary() {
        if (peek('-')) { ++p_; if (!unary()) return false; emit(ChannelExpr::Op::Neg); return true; }
        if (peek('+')) { ++p_; return unary(); }
        return power();
    }
    bool power() {
        if (!primary()) return false;
        if (peek('^')) {
            ++p_;
            if (!unary()) return false;          // right-associative: 2^3^2 = 2^(3^2)
            emit(ChannelExpr::Op::Pow);
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
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) {
                v = v * 10 + (s_[p_] - '0');
                if (v > 1000000) return err("has a channel number that is too large");
                ++p_;
            }
            if (p_ == start) { p_ = start - 1; return err("has a 'c' without a channel number"); }
            const int ch = static_cast<int>(v);
            if (ch > out_.maxChan) out_.maxChan = ch;
            emit(ChannelExpr::Op::Chan, ch);
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const size_t start = p_;
            while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
            if (p_ < s_.size() && s_[p_] == '.') {
                ++p_;
                while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
            }
            if (p_ - start == 1 && c == '.') { p_ = start; return err("has a '.' that is not a number"); }
            if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
                size_t q = p_ + 1;
                if (q < s_.size() && (s_[q] == '+' || s_[q] == '-')) ++q;
                if (q < s_.size() && std::isdigit(static_cast<unsigned char>(s_[q]))) {
                    p_ = q;
                    while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) ++p_;
                }
            }
            const std::string num = s_.substr(start, p_ - start);
            emit(ChannelExpr::Op::Num, -1, std::strtod(num.c_str(), nullptr));
            // "2c1": a number followed directly by a channel or '(' is not a product.
            skip();
            if (p_ < s_.size() && (s_[p_] == 'c' || s_[p_] == 'C' || s_[p_] == '('))
                return err("needs a '*' between a number and what follows it");
            return true;
        }
        return err("has '" + std::string(1, c) + "' where a channel, number or '(' is expected");
    }
};

}  // namespace detail

// Compiles one element.  False with @p error (the element, the problem and its
// column) when it does not follow the grammar.
inline bool parseChannelExpr(const std::string& element, ChannelExpr& out, std::string* error = nullptr) {
    out = ChannelExpr();
    // Trim, so messages and text show the element as written without padding.
    size_t a = element.find_first_not_of(" \t"), b = element.find_last_not_of(" \t");
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
