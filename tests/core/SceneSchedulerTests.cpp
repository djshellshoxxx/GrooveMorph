#include "core/SceneScheduler.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using namespace groovemorph::core;

namespace
{
int failures = 0;

void expect(std::string_view name, bool value)
{
    if (!value)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

BlockTiming timing(double ppq = 0.0, double tempo = 120.0)
{
    BlockTiming result;
    result.isPlaying = true;
    result.ppqPosition = ppq;
    result.ppqPositionOfLastBarStart = std::floor(ppq / 4.0) * 4.0;
    result.sampleRate = 48000.0;
    result.grid = PhraseGrid{tempo, 4, 4, 2, 0, 0.0};
    return result;
}
}

int main()
{
    {
        SceneScheduler scheduler;
        expect("accept beat request", scheduler.request(2, Quantization::Beat));
        auto block = timing(0.0);
        auto event = scheduler.process(block, 12000);
        expect("event at exact block end deferred", !event && scheduler.hasPending());
        block.ppqPosition = 0.25;
        event = scheduler.process(block, 12000);
        expect("target is stable across block", event && event->sceneId == 2
                   && event->sampleOffset == 0 && !scheduler.hasPending());
    }

    {
        SceneScheduler scheduler;
        scheduler.request(1, Quantization::Phrase);
        auto block = timing(7.5);
        block.ppqPositionOfLastBarStart.reset();
        auto event = scheduler.process(block, 12000);
        expect("phrase can schedule without bar-start PPQ",
               event && event->sampleOffset == 12000);
    }

    {
        SceneScheduler scheduler;
        scheduler.request(1, Quantization::Bar);
        auto block = timing(2.0);
        block.ppqPositionOfLastBarStart.reset();
        expect("bar waits for a valid bar anchor",
               !scheduler.process(block, 48000) && scheduler.hasPending());
        block.ppqPositionOfLastBarStart = 0.0;
        const auto event = scheduler.process(block, 48000);
        expect("bar request schedules once anchor returns",
               event && event->sampleOffset == 24000);
    }

    {
        SceneScheduler scheduler;
        scheduler.request(3, Quantization::Bar);
        auto block = timing(3.0, 120.0);
        expect("wait before boundary", !scheduler.process(block, 24000));
        block.ppqPosition = 3.5;
        block.grid.tempoBpm = 60.0;
        auto event = scheduler.process(block, 24000);
        expect("tempo refresh recalculates sample offset",
               event && event->sampleOffset == 24000);
    }

    {
        SceneScheduler scheduler;
        scheduler.request(1, Quantization::Beat);
        auto block = timing(0.0);
        block.discontinuity = true;
        auto event = scheduler.process(block, 12000);
        expect("seek/loop block reanchors from fresh PPQ",
               event && event->sampleOffset == 0);
    }

    {
        SceneScheduler scheduler;
        scheduler.request(1, Quantization::Beat);
        auto block = timing(0.0);
        expect("stopped transport holds quantized request",
               !scheduler.process(BlockTiming{}, 128) && scheduler.hasPending());
        scheduler.request(2, Quantization::Phrase);
        expect("new request replaces old request",
               scheduler.pendingSceneId() == 2u);
        scheduler.requestImmediate(4);
        auto event = scheduler.process(BlockTiming{}, 128);
        expect("immediate request fires while stopped",
               event && event->sceneId == 4 && event->sampleOffset == 0);
        expect("fired request clears pending", !scheduler.hasPending());
    }

    {
        SceneScheduler scheduler;
        scheduler.request(1, Quantization::Beat);
        auto block = timing(0.0);
        block.grid.tempoBpm = std::numeric_limits<double>::quiet_NaN();
        expect("invalid tempo does not fire",
               !scheduler.process(block, 48000) && scheduler.hasPending());
        block.grid.tempoBpm = 120.0;
        block.ppqPosition.reset();
        expect("missing PPQ does not fire",
               !scheduler.process(block, 48000) && scheduler.hasPending());
        scheduler.cancel();
        expect("cancel clears pending", !scheduler.hasPending());
    }

    if (failures == 0)
        std::cout << "SceneSchedulerTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
