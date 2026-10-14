// VF-T07: ledger text, units and the honest idle / invalid / preview states.
#include "core/viz/Ledger.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::trace;
using namespace groovemorph::core::viz;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
template <typename A, typename B>
void expectEq(std::string_view name, const A& actual, const B& expected) {
    if (!(actual == expected)) { std::cerr << name << ": got \"" << actual << "\" expected \"" << expected << "\"\n"; ++failures; }
}

constexpr double sr = 48000.0, spb = 24000.0; // 120 BPM

// 2 s of frames, hop 128, steady state.
std::vector<TraceFrame> steady(bool timed = true) {
    std::vector<TraceFrame> f(750);
    for (std::size_t i = 0; i < f.size(); ++i) {
        f[i].sample = static_cast<std::int64_t>(127 + 128 * i);
        f[i].epoch = 1;
        f[i].flags = timed ? TimingOk : 0;
        f[i].beat = static_cast<double>(f[i].sample) / spb;
        f[i].samplesPerBeat = timed ? static_cast<float>(spb) : 0.0f;
        f[i].intensity = 1.0f;
        f[i].inPeak = 0.5f;
        f[i].outPeak = 0.5f;
    }
    return f;
}

StatusSnapshot status() {
    StatusSnapshot s;
    s.timingSource = TimingSource::Host;
    s.timingValid = true;
    s.transportPlaying = true;
    s.barInPhrase = 2.4;
    s.beatInBar = 1.4;
    s.sampleRate = sr;
    return s;
}

const LedgerRow* row(const Ledger& l, std::string_view label) {
    for (const auto& r : l.rows) if (r.label == label) return &r;
    return nullptr;
}
} // namespace

int main() {
    // Helpers.
    expectEq("db", formatDb(0.5), std::string("-6.0 dB"));
    expectEq("db zero", formatDb(0.0), std::string("-inf dB"));
    expectEq("scene names", sceneName(0) + sceneName(3) + sceneName(7), std::string("AD?"));
    expectEq("missing fields", missingFieldsText(MissingField::tempo | MissingField::ppq), std::string("tempo, PPQ"));

    // Neutral steady state: live, no effects.
    {
        auto f = steady();
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expect("no banner", l.banner == BannerKind::None && l.bannerText.empty());
        expectEq("hearing live", row(l, "Hearing audio from")->value, std::string("now (live)"));
        expectEq("speed live", row(l, "Reading speed")->value, std::string("1x"));
        expectEq("gate off", row(l, "Gate")->value, std::string("off"));
        expectEq("wet", row(l, "Wet / intensity")->value, std::string("0 % / 100 %"));
        expectEq("added", row(l, "Added vs input")->value, std::string("+0.0 dB"));
        expectEq("history", row(l, "History")->value, std::string("2.0 / 4.0 s"));
        expectEq("history note", row(l, "History")->note, std::string("filling"));
        expectEq("tail", row(l, "Reported tail")->value, std::string("4.0 s"));
        expectEq("timing", row(l, "Timing")->value, std::string("Host (valid)"));
        expectEq("position", row(l, "Position")->value, std::string("bar 3 of 4"));
        expectEq("position beat", row(l, "Position")->note, std::string("beat 2.4"));
        expectEq("scene", row(l, "Scene")->value, std::string("A sounding"));
    }

    // Repeat reading 1.5 beats back; reverse tape-stop priority; gate closed.
    {
        auto f = steady();
        for (auto& fr : f) { fr.amount[0] = 1.0f; fr.lagSamples[0] = 36000.0f; }
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expectEq("hearing 1.5 beats ago", row(l, "Hearing audio from")->value, std::string("1.50 beats ago"));
        expectEq("hearing ms", row(l, "Hearing audio from")->note, std::string("750 ms"));
        expectEq("speed repeat", row(l, "Reading speed")->note, std::string("repeat"));
        expectEq("wet shows max amount", row(l, "Wet / intensity")->value, std::string("100 % / 100 %"));
    }
    {
        auto f = steady();
        for (auto& fr : f) { fr.amount[1] = 1.0f; fr.lagSamples[1] = 2400.0f; fr.rate[1] = -1.0f; fr.amount[4] = 1.0f; fr.lagSamples[3] = 1000.0f; fr.rate[3] = 0.5f; }
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expectEq("tape-stop wins priority", row(l, "Reading speed")->value, std::string("0.50x"));
        expectEq("tape-stop note", row(l, "Reading speed")->note, std::string("tape-stop"));
    }
    {
        auto f = steady();
        for (auto& fr : f) { fr.amount[1] = 1.0f; fr.lagSamples[1] = 2400.0f; fr.rate[1] = -1.0f; }
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expectEq("reverse note", row(l, "Reading speed")->note, std::string("backwards (reverse)"));
    }
    {
        auto f = steady();
        for (std::size_t i = 0; i < f.size(); ++i) { f[i].amount[2] = 1.0f; f[i].gateGain = (i % 2) ? 0.0f : 1.0f; }
        f.back().gateGain = 0.0f;
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expectEq("gate closed value", row(l, "Gate")->value, std::string("-inf dB"));
        expect("gate closed percentage", row(l, "Gate")->note == "closed 50 % of the last bar" || row(l, "Gate")->note == "closed 51 % of the last bar");
    }

    // Level change.
    {
        auto f = steady();
        for (auto& fr : f) { fr.inPeak = 0.25f; fr.outPeak = 0.5f; }
        expectEq("added +6", row(buildLedger(FrameWindow{f.data(), f.size()}, status()), "Added vs input")->value, std::string("+6.0 dB"));
        for (auto& fr : f) { fr.inPeak = 0.0f; fr.outPeak = 0.0f; }
        const auto l = buildLedger(FrameWindow{f.data(), f.size()}, status());
        expectEq("silent input", row(l, "Added vs input")->note, std::string("input silent"));
    }

    // Scene queue, countdown and preview.
    {
        auto f = steady();
        auto s = status();
        s.queuedScene = 2;
        s.samplesUntilSwitch = 31200; // 1.3 beats
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, s);
        expectEq("queued note", row(l, "Scene")->note, std::string("-> C queued in 1.3 beats"));
        s.previewing = true;
        const Ledger p = buildLedger(FrameWindow{f.data(), f.size()}, s);
        expect("preview banner", p.banner == BannerKind::Preview && p.bannerText == "PREVIEW: not the committed scene");
        expectEq("preview scene note", row(p, "Scene")->note, std::string("preview"));
    }

    // Honest idle states.
    {
        auto f = steady(false);
        auto s = status();
        s.transportPlaying = false;
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, s);
        expect("stopped banner", l.banner == BannerKind::Idle && l.bannerText == "Bypassed: transport stopped");
        expectEq("stopped hearing", row(l, "Hearing audio from")->value, std::string("bypassed"));
        expectEq("stopped speed", row(l, "Reading speed")->value, std::string("-"));
    }
    {
        auto f = steady(false);
        auto s = status();
        s.timingValid = false;
        s.timingSource = TimingSource::Manual;
        s.missingFields = MissingField::tempo | MissingField::position;
        const Ledger l = buildLedger(FrameWindow{f.data(), f.size()}, s);
        expect("invalid banner", l.banner == BannerKind::Invalid);
        expectEq("invalid text", l.bannerText, std::string("Timing invalid: effects bypassed (missing tempo, position)"));
        expectEq("invalid timing row", row(l, "Timing")->value, std::string("Manual (invalid)"));
        expectEq("invalid timing note", row(l, "Timing")->note, std::string("tempo, position"));
    }
    {
        const Ledger l = buildLedger(FrameWindow{}, status());
        expect("no data banner", l.banner == BannerKind::NoData && l.bannerText == "No trace data yet");
        expectEq("history empty", row(l, "History")->value, std::string("0.0 / 4.0 s"));
    }

    // Copy-as-text golden.
    {
        auto f = steady();
        const std::string text = ledgerAsText(buildLedger(FrameWindow{f.data(), f.size()}, status()));
        expect("text has rows and notes", text.find("Hearing audio from: now (live) (no time displacement)\n") == 0
                                              && text.find("Position: bar 3 of 4 (beat 2.4)\n") != std::string::npos
                                              && text.find("Scene: A sounding\n") != std::string::npos);
    }

    if (failures == 0) std::cout << "LedgerTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
