// colors_test.cpp — ItemColors / ChannelColors copy semantics.
//
// Both containers own their entries through raw pointers.  A copy, by
// construction or by assignment, must be deep: the two objects never share an
// entry, so changing or destroying one leaves the other intact (the implicit
// operator= used to copy the pointers, and both destructors then freed them).
// ChannelColors' copy constructor also used to copy the FIRST channel N times.
// Run under AddressSanitizer to catch a double free directly.  Self-contained
// (own main), run via ctest.

#include "itemcolors.h"
#include "channelcolors.h"

#include <QColor>
#include <QString>

#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main() {
    const QColor red(255, 0, 0), green(0, 255, 0), blue(0, 0, 255), grey(80, 80, 80);

    // ---- ItemColors ------------------------------------------------------
    {
        ItemColors a;
        a.append(2, QString("two"), red);
        a.append(5, QString("five"), green);
        a.setColorChanged(true);

        ItemColors b;
        b.append(9, blue);                  // replaced by the assignment
        b = a;
        check(b.numberOfItems() == 2 && b.color(2) == red && b.color(5) == green,
              "ItemColors assignment copies the items");
        check(b.itemLabelById(5) == QString("five"), "ItemColors assignment copies labels");
        check(b.isColorChanged(), "ItemColors assignment copies colorChanged");

        b.setColor(2, blue);
        check(a.color(2) == red, "ItemColors: changing the copy leaves the original");

        {
            ItemColors c;
            c = a;
        }                                   // c destroyed: a must be untouched
        check(a.numberOfItems() == 2 && a.color(5) == green,
              "ItemColors: destroying an assigned copy leaves the original");

        ItemColors& self = a;
        a = self;
        check(a.numberOfItems() == 2 && a.color(2) == red, "ItemColors self-assignment");

        ItemColors d(a);
        a.remove(2);
        check(d.numberOfItems() == 2 && d.color(2) == red, "ItemColors copy construction is deep");
    }

    // ---- ChannelColors ---------------------------------------------------
    {
        ChannelColors a;
        a.append(0, red, green, blue);
        a.append(1, green, blue, red);
        a.append(2, blue, red, grey);

        ChannelColors b(a);
        check(b.numberOfChannels() == 3, "ChannelColors copy keeps the count");
        check(b.channelId(0) == 0 && b.channelId(1) == 1 && b.channelId(2) == 2,
              "ChannelColors copy keeps every channel (not the first one repeated)");
        check(b.color(1) == green && b.groupColor(2) == red && b.spikeGroupColor(2) == grey,
              "ChannelColors copy keeps the colours");

        ChannelColors c;
        c.append(7, grey);
        c = a;
        check(c.numberOfChannels() == 3 && !c.contains(7) && c.color(2) == blue,
              "ChannelColors assignment replaces the contents");
        c.setColor(0, grey);
        check(a.color(0) == red, "ChannelColors: changing the copy leaves the original");
        {
            ChannelColors d;
            d = a;
        }
        check(a.numberOfChannels() == 3 && a.color(1) == green,
              "ChannelColors: destroying an assigned copy leaves the original");
        ChannelColors& self = a;
        a = self;
        check(a.numberOfChannels() == 3 && a.color(2) == blue, "ChannelColors self-assignment");
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
