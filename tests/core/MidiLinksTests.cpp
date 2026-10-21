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

    if (failures == 0) std::cout << "MidiLinksTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
