// unitsidecar_test.cpp -- the per-clustering cluster-information file.
// Round trip with awkward text (tabs, newlines, backslashes, UTF-8), rows that
// describe nothing are dropped, CRLF and comments are tolerated, a malformed
// line is refused with its number, and the name follows the .clu.  No Qt.

#include "unitsidecar.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    using unitsidecar::Row;
    std::vector<Row> rows(3);
    rows[0] = {3, 2, "CA1", "pyr", "12.5", "good", "tab\there\nnew line \\ back", "", "", "", ""};
    rows[1] = {3, 7, "", "", "", "", "", "", "", "", ""};                       // describes nothing
    rows[2] = {3, 9, "DG", "int", "", "?", "Zelle \xC3\xBC",
               "hippocampal formation", "archicortex", "WHS SD rat v4", "UBERON:0001885"};
    const std::string text = unitsidecar::format(rows);
    std::vector<Row> back;
    std::string err;
    check(unitsidecar::parse(text, back, &err), "parse: " + err);
    check(back.size() == 2 && back[0] == rows[0] && back[1] == rows[2], "round trip, empty row dropped");
    check(text.find("\t7\t") == std::string::npos, "the empty row is not written");

    check(unitsidecar::parse("# c\r\n3\t4\tCA3\r\n\r\n", back, &err) && back.size() == 1
          && back[0].cluster == 4 && back[0].structure == "CA3" && back[0].notes.empty(),
          "CRLF, comments, missing trailing fields");
    check(!unitsidecar::parse("3\t4\tx\nthree\t5\n", back, &err) && err.find("line 2") != std::string::npos,
          "malformed line refused with its number");
    check(unitsidecar::parse(unitsidecar::header(), back, &err) && back.empty(), "header only: no units");
    check(unitsidecar::parse("# klusters units v1\n3\t4\tCA1\tpyr\t\tgood\tn\n", back, &err) && back.size() == 1
          && back[0].notes == "n" && back[0].region.empty() && back[0].regionId.empty(),
          "a v1 line (seven fields) reads with the new fields empty");
    {
        std::vector<Row> only(1);
        only[0] = {3, 11, "", "", "", "", "", "", "thalamus", "", ""};
        check(unitsidecar::parse(unitsidecar::format(only), back, &err) && back.size() == 1
              && back[0].division == "thalamus", "a unit with only a new field is written");
    }

    check(unitsidecar::pathFor("/d/rat.clu.stderiv_C5.3.mf") == "/d/rat.cli.stderiv_C5.3.mf", "name");
    check(unitsidecar::pathFor("/d/rat.clu.3") == "/d/rat.cli.3", "untagged name");
    check(unitsidecar::pathFor("/d.clu.x/rat.fet.3").empty(), "no .clu. in the file name");

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
