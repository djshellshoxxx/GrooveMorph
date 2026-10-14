#include "core/control/GestureLane.h"
#include "core/dsp/LoopSource.h"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}
} // namespace

int main()
{
    // ---- LoopSource: beat-locked playback ----
    {
        LoopSource loop;
        loop.prepare(48000.0);
        expect("no loop initially", !loop.hasLoop());
        float tmp[4] = {};
        float* out[1] = {tmp};
        expect("render without loop leaves buffer", !loop.render(out, 1, 4, 0.0, 24000.0));
        // 2-beat ramp loop (0..1 over 2 beats of 24000 samples => 48000 samples).
        std::vector<float> ramp(48000);
        for (std::size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = static_cast<float>(i) / 48000.0f;
        const float* in[1] = {ramp.data()};
        expect("invalid loads rejected", !loop.load(in, 1, 4, 2.0, 0.0) && !loop.load(in, 1, 48000, 0.0, 0.0)
                                              && !loop.load(nullptr, 1, 48000, 2.0, 0.0));
        expect("load ok", loop.load(in, 1, 48000, 2.0, 0.0) && loop.hasLoop());
        std::vector<float> l(8), r(8);
        float* ch[2] = {l.data(), r.data()};
        expect("render", loop.render(ch, 2, 8, 1.0, 24000.0)); // beat 1 = halfway through the loop
        expect("position follows the beat", std::abs(l[0] - 0.5f) < 1e-3f && l[0] == r[0]);
        loop.render(ch, 2, 8, 3.0, 24000.0); // beat 3 wraps to the same phase
        expect("loop wraps", std::abs(l[0] - 0.5f) < 1e-3f);
        loop.render(ch, 2, 8, -1.0, 24000.0); // negative preroll wraps too
        expect("negative positions wrap", std::abs(l[0] - 0.5f) < 1e-3f);
        loop.render(ch, 2, 4, std::nan(""), 24000.0);
        expect("invalid timing does not render", true);
        // Origin shifts the loop start.
        expect("reload with origin", loop.load(in, 1, 48000, 2.0, 0.5));
        loop.render(ch, 2, 1, 0.5, 24000.0);
        expect("origin beat plays the loop start", std::abs(l[0]) < 1e-3f);
        // Second reload exercises the double buffer.
        std::vector<float> flat(48000, 0.25f);
        const float* in2[1] = {flat.data()};
        expect("double-buffer reload", loop.load(in2, 1, 48000, 4.0, 0.0));
        loop.render(ch, 2, 1, 1.0, 24000.0);
        expect("new loop plays", std::abs(l[0] - 0.25f) < 1e-6f);
        loop.clear();
        expect("clear", !loop.hasLoop());
    }

    // ---- LoopCapture: starts on a bar line, records exactly N bars ----
    {
        LoopCapture capture;
        capture.prepare(48000.0);
        expect("arm invalid", !capture.arm(0, 4) && !capture.arm(2, 0));
        expect("arm", capture.arm(1, 4)); // 1 bar of 4 beats @ 24000 spb = 96000 samples
        expect("no double arm", !capture.arm(1, 4));
        const double spb = 24000.0;
        std::vector<float> block(480);
        std::int64_t position = 0;
        const double startBeat = 0.9; // not on a bar line: capture must wait for beat 4
        double beat = startBeat;
        int blocks = 0;
        float counter = 0.0f;
        while (capture.state() != LoopCapture::State::Done && blocks++ < 2000)
        {
            for (auto& v : block)
                v = counter++;
            const float* ch[1] = {block.data()};
            capture.process(ch, 1, 480, beat, spb, true);
            beat += 480.0 / spb;
            position += 480;
        }
        expect("capture finished", capture.state() == LoopCapture::State::Done);
        expect("exact length", capture.recordedSamples() == 96000);
        expect("aligned to the next bar line", std::abs(capture.startBeat() - 4.0) < 1e-9 && capture.loopBeats() == 4.0);
        // First recorded sample is the sample at beat 4.0: (4.0-0.9)*24000 = 74400 samples after start
        // (a multiple of the 480 block => sample index 74400, value 74400).
        expect("first sample is the bar-line sample", std::abs(capture.channel(0)[0] - 74400.0f) < 1.0f);
        capture.release();
        expect("released", capture.state() == LoopCapture::State::Idle);
        // Not playing: nothing happens.
        expect("arm again", capture.arm(1, 4));
        const float* ch[1] = {block.data()};
        capture.process(ch, 1, 480, 4.0, spb, false);
        expect("stopped transport does not record", capture.state() == LoopCapture::State::Armed);
        capture.cancel();
        expect("cancel", capture.state() == LoopCapture::State::Idle);
    }

    // ---- GestureLane ----
    {
        control::GestureLane lane;
        expect("empty lane", !lane.hasData() && lane.sample(0.3) < 0.0f);
        lane.record(0.0, 0.0f);
        lane.record(0.5, 1.0f);
        expect("two bins", lane.filledBins() == 2);
        expect("exact bin", std::abs(lane.sample(0.5) - 1.0f) < 1e-6f);
        expect("interpolates up", std::abs(lane.sample(0.25) - 0.5f) < 0.02f);
        expect("wraps back down", std::abs(lane.sample(0.75) - 0.5f) < 0.02f);
        lane.record(1.25, 5.0f); // fraction wraps to 0.25; value clamps
        expect("clamped and wrapped", std::abs(lane.sample(0.25) - 1.0f) < 1e-6f);
        lane.record(std::nan(""), std::nan(""));
        lane.clear();
        expect("cleared", !lane.hasData());
    }

    if (failures == 0)
        std::cout << "LoopSourceTests passed\n";
    return failures == 0 ? 0 : 1;
}
