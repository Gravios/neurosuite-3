// Conformance test for the expression form of a custom spatial pattern
// (channel_expr.hpp) against sdiff_expr_vectors.tsv, plus the transform it
// plugs into (stderiv_transform.hpp, spatial order 6).
//
// Usage: sdiff_expr_test <path-to-sdiff_expr_vectors.tsv>
#include <neurosuite/core/channel_expr.hpp>
#include <neurosuite/core/stderiv_transform.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace neurosuite::core;

static int fails = 0;
static void check(bool c, const std::string& m)
{
    if (!c) { std::printf("FAIL  %s\n", m.c_str()); ++fails; }
}

static std::vector<std::string> splitTabs(const std::string& line)
{
    std::vector<std::string> f;
    size_t p = 0;
    while (true) {
        const size_t t = line.find('\t', p);
        f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
        if (t == std::string::npos) break;
        p = t + 1;
    }
    return f;
}

static std::vector<double> numbers(const std::string& s)
{
    std::vector<double> v;
    std::istringstream in(s);
    double x;
    while (in >> x) v.push_back(x);
    return v;
}

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "sdiff_expr_vectors.tsv";
    std::ifstream in(path);
    if (!in) { std::printf("FAIL  cannot open %s\n", path.c_str()); return 1; }
    int evals = 0, rejects = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = splitTabs(line);
        if (f[0] == "eval" && f.size() >= 4) {
            ++evals;
            ChannelExprSet set;
            std::string err;
            if (!parseChannelExprs(f[1], set, &err)) { check(false, "parses: " + f[1] + " -- " + err); continue; }
            const std::vector<double> x = numbers(f[2]), want = numbers(f[3]);
            check(set.fits(static_cast<int>(x.size())), "fits its row: " + f[1]);
            check(set.size() == static_cast<int>(want.size()), "one output per element: " + f[1]);
            for (int k = 0; k < set.size() && k < static_cast<int>(want.size()); ++k) {
                const double got = set.eval(k, x.data());
                const double w = want[static_cast<size_t>(k)];
                check(std::fabs(got - w) <= 1e-9 * std::max(1.0, std::fabs(w)),
                      f[1] + " output " + std::to_string(k) + ": got " + std::to_string(got)
                      + " want " + std::to_string(w));
            }
        } else if (f[0] == "reject" && f.size() >= 2) {
            ++rejects;
            ChannelExprSet set;
            std::string err;
            check(!parseChannelExprs(f[1], set, &err), "rejected: " + f[1]);
            check(!err.empty(), "a reason is given for: " + f[1]);
        } else {
            check(false, "unknown vector line: " + line);
        }
    }
    check(evals >= 10 && rejects >= 10, "the vector file has its cases");

    // Recognising the form, and fitting a group.
    check(isChannelExprSpec("c0-c1") && isChannelExprSpec("C0-c1") && !isChannelExprSpec("0-1,1-2")
          && !isChannelExprSpec("0-1+2"), "the expression form is told from the older forms");
    ChannelExprSet four;
    parseChannelExprs("c0-c1,c1-c2,c2-c3,c3-c2", four);
    std::string why;
    check(!four.fits(3, &why) && !why.empty(), "a pattern for 4 channels does not fit 3");
    ChannelExprSet far;
    parseChannelExprs("c0-c5,c1", far);
    check(!far.fits(2, &why) && why.find("c5") != std::string::npos, "a channel the group lacks is named");

    // Too deep to evaluate on the fixed stack.
    std::string deep = "c0";
    for (int i = 0; i < 70; ++i) deep = "c0+(" + deep + ")";
    ChannelExprSet d;
    check(!parseChannelExprs(deep, d, &why), "nesting beyond the evaluation stack is refused");

    // The transform: order 6 uses the expressions; a set that does not fit falls back
    // to all-pairs like every custom order.
    {
        const double row[4] = {3, 5, 11, -2};
        check(spatialDeriv(SdiffOrder::Expr, row, 2, 4, nullptr, nullptr, nullptr, &four) == 13.0,
              "spatialDeriv applies the expression");
        ChannelExprSet three;
        parseChannelExprs("c0-c1,c1-c2,c2-c0", three);
        check(spatialDeriv(SdiffOrder::Expr, row, 0, 4, nullptr, nullptr, nullptr, &three)
              == spatialDeriv(SdiffOrder::AllPairs, row, 0, 4),
              "an expression set that does not fit the group falls back to all-pairs");
        check(spatialDeriv(SdiffOrder::Expr, row, 0, 4) == spatialDeriv(SdiffOrder::AllPairs, row, 0, 4),
              "no expression set: all-pairs");

        // applyStderivTransform with order 6 equals the same expression written as
        // order 4 (c_k - c_partner) on a waveform.
        const int nChan = 4, nSamp = 5;
        std::vector<int16_t> wav(nChan * nSamp);
        for (int i = 0; i < nChan * nSamp; ++i) wav[static_cast<size_t>(i)] = static_cast<int16_t>((i * 37) % 101 - 50);
        std::vector<int16_t> a(wav.size()), b(wav.size());
        const int partner[4] = {1, 2, 3, 2};
        applyStderivTransform(SdiffOrder::Custom, wav.data(), nChan, nSamp, a.data(), partner);
        applyStderivTransform(SdiffOrder::Expr, wav.data(), nChan, nSamp, b.data(), nullptr, nullptr,
                              nullptr, nullptr, &four);
        check(a == b, "order 6 reproduces order 4 when written as the same differences");
    }

    // Method 9.
    check(methodValid(9) && !methodValid(10), "method 9 (stderiv_C6) is valid, 10 is not");
    check(spatialOrder(Method::StderivExpr) == SdiffOrder::Expr && hasTemporalDiff(Method::StderivExpr)
          && std::string(methodTag(Method::StderivExpr)) == "stderiv", "method 9 is the stderiv expression form");

    if (fails) std::printf("\n%d FAILED\n", fails);
    else std::printf("sdiff_expr: all passed (%d eval, %d reject vectors)\n", evals, rejects);
    return fails ? 1 : 0;
}
