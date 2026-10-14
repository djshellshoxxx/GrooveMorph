#include "core/trace/TraceRing.h"

#include <atomic>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

using namespace groovemorph::core::trace;

namespace {
int failures = 0;

void expect(std::string_view name, bool condition) {
    if (!condition) {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Every field is derived from one number so a torn frame is detectable.
TraceFrame make(std::int64_t i, std::uint32_t epoch = 1) {
    TraceFrame f{};
    f.sample = i;
    f.epoch = epoch;
    f.flags = static_cast<std::uint32_t>(i & 0xFF);
    f.beat = static_cast<double>(i) * 0.5;
    f.samplesPerBeat = static_cast<float>(i);
    f.intensity = static_cast<float>(i);
    for (auto& a : f.amount) a = static_cast<float>(i);
    for (auto& l : f.lagSamples) l = static_cast<float>(i);
    for (auto& r : f.rate) r = static_cast<float>(i);
    f.gateGain = static_cast<float>(i);
    f.inPeak = static_cast<float>(i);
    f.outPeak = static_cast<float>(i);
    return f;
}

bool consistent(const TraceFrame& f) {
    const auto i = f.sample;
    const auto v = static_cast<float>(i);
    bool ok = f.flags == static_cast<std::uint32_t>(i & 0xFF) && f.beat == static_cast<double>(i) * 0.5
              && f.samplesPerBeat == v && f.intensity == v && f.gateGain == v && f.inPeak == v && f.outPeak == v;
    for (auto a : f.amount) ok = ok && a == v;
    for (auto l : f.lagSamples) ok = ok && l == v;
    for (auto r : f.rate) ok = ok && r == v;
    return ok;
}
} // namespace

int main() {
    static_assert(sizeof(TraceFrame) == 96);
    static_assert(hopSamples == 128);

    TraceRing ring;
    expect("inactive by default", !ring.active());
    ring.setActive(true);
    expect("active after setActive", ring.active());
    ring.setActive(false);
    expect("inactive again", !ring.active());

    std::vector<TraceFrame> out(ringCapacity);
    expect("empty ring copies nothing", ring.copyLatest(out.data(), out.size()) == 0);

    // Ordering and partial fill.
    for (std::int64_t i = 0; i < 10; ++i) ring.push(make(i));
    auto n = ring.copyLatest(out.data(), out.size());
    expect("10 frames stored", n == 10);
    bool ascending = n == 10;
    for (std::size_t k = 0; k < n; ++k) ascending = ascending && out[k].sample == static_cast<std::int64_t>(k) && consistent(out[k]);
    expect("ascending and intact", ascending);

    // maxFrames returns the newest ones.
    n = ring.copyLatest(out.data(), 4);
    expect("newest 4", n == 4 && out[0].sample == 6 && out[3].sample == 9);

    // Wrap: push far more than capacity; only the newest capacity-1 are readable, still ascending and intact.
    for (std::int64_t i = 10; i < static_cast<std::int64_t>(ringCapacity) * 3 + 5; ++i) ring.push(make(i));
    n = ring.copyLatest(out.data(), out.size());
    expect("wrap returns capacity-1 frames", n == ringCapacity - 1);
    bool ok = n > 0;
    const std::int64_t last = static_cast<std::int64_t>(ringCapacity) * 3 + 4;
    for (std::size_t k = 0; k < n; ++k)
        ok = ok && out[k].sample == last - static_cast<std::int64_t>(n - 1 - k) && consistent(out[k]);
    expect("wrapped frames ascending, contiguous, intact", ok);
    expect("count reports pushes", ring.pushedCount() == ringCapacity * 3 + 5);

    // Epoch trimming keeps only the newest epoch.
    TraceRing epochRing;
    for (std::int64_t i = 0; i < 5; ++i) epochRing.push(make(i, 1));
    for (std::int64_t i = 0; i < 3; ++i) epochRing.push(make(i, 2));
    n = epochRing.copyLatest(out.data(), out.size());
    const auto first = latestEpochStart(out.data(), n);
    expect("epoch start", n == 8 && first == 5);
    expect("empty epoch start", latestEpochStart(out.data(), 0) == 0);

    // Single-writer / single-reader stress: readers must never observe a torn frame and order must ascend.
    TraceRing shared;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> torn{0}, disorder{0}, reads{0};
    std::thread writer([&] {
        for (std::int64_t i = 0; i < 400000; ++i) shared.push(make(i));
        stop.store(true);
    });
    std::vector<TraceFrame> buf(2048);
    while (!stop.load()) {
        const auto got = shared.copyLatest(buf.data(), buf.size());
        reads.fetch_add(1);
        for (std::size_t k = 0; k < got; ++k) {
            if (!consistent(buf[k])) torn.fetch_add(1);
            if (k > 0 && buf[k].sample <= buf[k - 1].sample) disorder.fetch_add(1);
        }
    }
    writer.join();
    expect("no torn frames under concurrent writes", torn.load() == 0);
    expect("frames stay ascending under concurrent writes", disorder.load() == 0);
    expect("reader actually ran", reads.load() > 0);

    if (failures == 0) std::cout << "TraceRingTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
