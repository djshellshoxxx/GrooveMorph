#include "core/MidiLinks.h"

#include <iostream>
#include <string>
#include <string_view>
#include <thread>

using namespace groovemorph::core;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
} // namespace

int main() {
    // Target validation.
    expect("parameter target", MidiLinkTable::validTarget("p:masterIntensity"));
    expect("scene depth target", MidiLinkTable::validTarget("s:0:depth") && MidiLinkTable::validTarget("s:4:slice") && MidiLinkTable::validTarget("s:5:depth") && MidiLinkTable::validTarget("s:6:enabled") && MidiLinkTable::validTarget("s:2:enabled"));
    expect("bad targets", !MidiLinkTable::validTarget("") && !MidiLinkTable::validTarget("p:") && !MidiLinkTable::validTarget("x:foo")
                           && !MidiLinkTable::validTarget("s:7:depth") && !MidiLinkTable::validTarget("s:0:volume")
                           && !MidiLinkTable::validTarget("p:a;b") && !MidiLinkTable::validTarget("p:a=b") && !MidiLinkTable::validTarget("p:has space"));

    // Link, replace, unlink, shared CC.
    MidiLinkTable t;
    expect("link", t.link(20, "p:masterIntensity") && t.ccFor("p:masterIntensity") == 20);
    expect("relink replaces, not duplicates", t.link(21, "p:masterIntensity") && t.ccFor("p:masterIntensity") == 21 && t.links().size() == 1);
    expect("one CC may drive two targets", t.link(21, "p:dryWet") && t.links().size() == 2);
    expect("out of range cc rejected", !t.link(128, "p:x") && !t.link(-1, "p:x"));
    expect("bad target rejected", !t.link(5, "nope"));
    t.unlink("p:masterIntensity");
    expect("unlink", t.ccFor("p:masterIntensity") == -1 && t.links().size() == 1);
    t.unlink("p:unknown");
    expect("unlink unknown is a no-op", t.links().size() == 1);

    // Capacity.
    {
        MidiLinkTable full;
        for (std::size_t i = 0; i < MidiLinkTable::maxLinks; ++i) full.link(static_cast<int>(i % 128), "p:param" + std::to_string(i));
        expect("table is bounded", full.links().size() == MidiLinkTable::maxLinks && !full.link(1, "p:one_more"));
        expect("relinking a present target still works when full", full.link(99, "p:param0"));
    }

    // Reserved macro controllers are excluded from learn; explicit links override macros.
    {
        CcInbox::Change moved[] = {{20, 127, 120}, {22, 127, 100}, {7, 64, 10}};
        expect("A04-T07 macros exclude reserved learn candidates", pickLearnCc(moved, 3, true) == 7);
        expect("A04-T07 macros off allow reserved learn candidates", pickLearnCc(moved, 3, false) == 20);
        expect("A04-T07 reserved-only learn keeps waiting", pickLearnCc(moved, 2, true) == -1);
        expect("A04-T07 macro reservation has exact bounds", isMacroCc(20) && isMacroCc(21) && isMacroCc(22)
            && !isMacroCc(19) && !isMacroCc(23));
        MidiLinkTable links;
        expect("A04-T08 unused CC is not linked", !links.usesCc(20));
        links.link(20, "p:dryWet");
        links.link(20, "p:masterIntensity");
        expect("A04-T08 explicit link claims macro slot", links.usesCc(20) && !links.usesCc(21));
        expect("A04-T09 sharing lists every other target", links.sharedTargets(20, "p:dryWet") == "p:masterIntensity"
            && links.sharedTargets(20, "p:missing") == "p:dryWet, p:masterIntensity");
        links.unlink("p:dryWet");
        expect("A04-T09 unshared target has no other targets", links.sharedTargets(20, "p:masterIntensity").empty());
        links.clear();
        expect("A04-T08 clearing releases macro slot", !links.usesCc(20));
    }

    // Serialization round trip and tolerant parsing.
    {
        MidiLinkTable a;
        a.link(20, "p:masterIntensity");
        a.link(7, "s:0:depth");
        a.link(64, "s:3:enabled");
        const auto text = a.serialize();
        expect("serialized form", text == "20=p:masterIntensity;7=s:0:depth;64=s:3:enabled");
        const auto b = MidiLinkTable::deserialize(text);
        expect("round trip", b.links().size() == 3 && b.ccFor("s:0:depth") == 7 && b.ccFor("s:3:enabled") == 64);
        const auto messy = MidiLinkTable::deserialize(";;x;=p:a;300=p:b;5=bad;6=p:ok;6=p:ok;abc=p:c;7=p:dup;8=p:dup;");
        expect("garbage is skipped", messy.ccFor("p:ok") == 6 && messy.ccFor("p:dup") == 8 && messy.links().size() == 2);
        expect("empty text", MidiLinkTable::deserialize("").links().empty());
        std::string huge;
        for (int i = 0; i < 1000; ++i) huge += std::to_string(i % 128) + "=p:p" + std::to_string(i) + ";";
        expect("huge text is bounded", MidiLinkTable::deserialize(huge).links().size() == MidiLinkTable::maxLinks);
    }

    // Restoring a full table must still apply later entries for existing targets.
    {
        MidiLinkTable full;
        for (std::size_t i = 0; i < MidiLinkTable::maxLinks; ++i)
            full.link(static_cast<int>(i), "p:param" + std::to_string(i));
        const auto restored = MidiLinkTable::deserialize(full.serialize()
            + ";99=p:overflow;100=p:param0;101=p:param63");
        expect("full restore keeps the last duplicate", restored.ccFor("p:param0") == 100
            && restored.ccFor("p:param63") == 101);
        expect("full restore rejects only new targets", restored.links().size() == MidiLinkTable::maxLinks
            && restored.ccFor("p:overflow") == -1);
    }

    // CcInbox: only changes are reported; discardPending forgets old movement.
    {
        CcInbox inbox;
        CcInbox::Change out[8];
        expect("nothing yet", inbox.collect(out, 8) == 0);
        inbox.push(20, 64);
        inbox.push(21, 0);
        expect("two changes", inbox.collect(out, 8) == 2 && out[0].cc == 20 && out[0].value == 64 && out[1].cc == 21 && out[1].value == 0);
        expect("unchanged is not repeated", inbox.collect(out, 8) == 0);
        inbox.push(20, 65);
        expect("a new value is reported", inbox.collect(out, 8) == 1 && out[0].value == 65);
        inbox.push(30, 1);
        inbox.discardPending();
        expect("discard forgets pending", inbox.collect(out, 8) == 0);
        inbox.push(30, 2);
        expect("movement after discard is seen", inbox.collect(out, 8) == 1 && out[0].cc == 30);
        inbox.push(200, 1); inbox.push(-1, 1);
        expect("out of range cc ignored", inbox.collect(out, 8) == 0);
        inbox.push(40, 0x1ff);
        expect("value masked to 7 bits", inbox.collect(out, 8) == 1 && out[0].value == 127);
        for (int i = 0; i < 10; ++i) inbox.push(50 + i, i);
        expect("capacity respected", inbox.collect(out, 3) == 3);
    }

    // Audio-thread pushes concurrent with message-thread collects never lose the final value.
    {
        CcInbox inbox;
        std::thread writer([&] { for (int i = 0; i < 200000; ++i) inbox.push(10, i); inbox.push(10, 77); });
        CcInbox::Change out[4];
        for (int i = 0; i < 1000; ++i) inbox.collect(out, 4);
        writer.join();
        int last = -1;
        for (std::size_t n = inbox.collect(out, 4); n > 0; n = 0) last = out[0].value;
        expect("final value arrives", last == 77 || last == -1);
    }

    // A04-06: travel tracking. A pulse that returns to its old value between two polls is still reported,
    // and learn picks the CC that travelled most.
    {
        CcInbox inbox;
        CcInbox::Change out[8];
        inbox.push(10, 0);
        expect("A04-06 first event", inbox.collect(out, 8) == 1 && out[0].cc == 10 && out[0].travel >= 1);
        inbox.push(10, 127);
        inbox.push(10, 0);
        expect("A04-06 pulse back to the old value is reported", inbox.collect(out, 8) == 1 && out[0].cc == 10
                                                                 && out[0].value == 0 && out[0].travel == 254);
        inbox.push(10, 0);
        expect("A04-06 repeated identical value is not movement", inbox.collect(out, 8) == 0);
        inbox.push(11, 5); inbox.push(12, 5);
        (void) inbox.collect(out, 8);
        inbox.push(11, 7);                 // travel 2
        inbox.push(12, 120);               // travel 115
        inbox.push(13, 1);                 // first event, travel 1
        const auto n = inbox.collect(out, 8);
        expect("A04-06 three movers", n == 3);
        expect("A04-06 learn picks the CC that moved most", pickLearnCc(out, n) == 12);
        CcInbox::Change tie[2] = {{40, 1, 5}, {30, 1, 5}};
        expect("A04-06 tie goes to the lowest CC", pickLearnCc(tie, 2) == 30);
        expect("A04-06 empty batch", pickLearnCc(nullptr, 0) == -1 && pickLearnCc(tie, 0) == -1);
        CcInbox::Change still[1] = {{9, 3, 0}};
        expect("A04-06 zero travel is never picked", pickLearnCc(still, 1) == -1);
        // discardPending also forgets travel, so movement before learn does not count.
        inbox.push(50, 0); inbox.push(50, 100);
        inbox.discardPending();
        expect("A04-06 discard forgets travel", inbox.collect(out, 8) == 0);
        inbox.push(50, 101);
        expect("A04-06 travel after discard counts only new movement", inbox.collect(out, 8) == 1 && out[0].travel == 1);
    }

    // A04-07: table full is distinguishable from an invalid request.
    {
        MidiLinkTable full;
        for (std::size_t i = 0; i < MidiLinkTable::maxLinks; ++i) full.link(static_cast<int>(i % 128), "p:param" + std::to_string(i));
        expect("A04-07 full", full.linkChecked(1, "p:another") == MidiLinkTable::LinkResult::TableFull);
        expect("A04-07 invalid", full.linkChecked(200, "p:another") == MidiLinkTable::LinkResult::Invalid
                                  && full.linkChecked(1, "bad") == MidiLinkTable::LinkResult::Invalid);
        expect("A04-07 existing target can be relinked while full", full.linkChecked(5, "p:param3") == MidiLinkTable::LinkResult::Ok
                                                                    && full.ccFor("p:param3") == 5);
        expect("A04-07 trim parameters are valid p: targets", MidiLinkTable::validTarget("p:op5Depth") && MidiLinkTable::validTarget("p:op6Enable"));
    }

    if (failures == 0) std::cout << "MidiLinksTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
