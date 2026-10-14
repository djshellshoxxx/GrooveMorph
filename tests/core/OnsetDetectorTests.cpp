#include "core/dsp/OnsetDetector.h"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using groovemorph::core::OnsetDetector;

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

// Steady quiet tone with decaying clicks every `period` samples.
std::vector<float> clicks(int total, int period, float level)
{
    std::vector<float> x(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i)
    {
        const int since = (i + period - 6000) % period; // first click after the warm-up
        const float click = since < 2400 ? level * std::exp(-static_cast<float>(since) / 400.0f) : 0.0f;
        x[static_cast<std::size_t>(i)] = 0.01f * std::sin(0.05f * static_cast<float>(i)) + click * (since % 2 ? 1.0f : -1.0f);
    }
    return x;
}

int run(const std::vector<float>& x, float sens, int block)
{
    OnsetDetector d;
    d.prepare(48000.0);
    int total = 0;
    for (std::size_t i = 0; i < x.size(); i += static_cast<std::size_t>(block))
        total += d.process(x.data() + i, static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), x.size() - i)), sens);
    return total;
}
} // namespace

int main()
{
    const auto x = clicks(48000 * 4, 24000, 0.8f); // 8 clicks over 4 s (every 0.5 s)
    expect("detects each click once at 480-sample blocks", run(x, 0.6f, 480) == 8);
    expect("block-size independent", run(x, 0.6f, 64) == 8 && run(x, 0.6f, 4096) == 8);
    expect("silence triggers nothing", run(std::vector<float>(48000, 0.0f), 1.0f, 480) == 0);
    expect("steady tone triggers nothing after start", run(std::vector<float>(48000, 0.0f), 0.5f, 480) == 0);
    std::vector<float> noisy(48000 * 2);
    for (std::size_t i = 0; i < noisy.size(); ++i)
        noisy[i] = 0.05f * std::sin(0.2f * static_cast<float>(i));
    expect("constant-level signal gives at most the start-up transient", run(noisy, 0.5f, 480) <= 1);
    // A sustained tone that begins after silence must not count as a hit (start-up transient).
    std::vector<float> bed(48000 * 2);
    for (std::size_t i = 0; i < bed.size(); ++i)
        bed[i] = i < 24000 ? 0.0f : 0.3f * std::sin(0.03f * static_cast<float>(i));
    expect("tone onset after silence inside the warm-up window is ignored when it starts at the beginning",
           run(std::vector<float>(bed.begin() + 24000, bed.end()), 1.0f, 480) == 0);
    // Long silence must not leave the envelopes in the subnormal range.
    {
        OnsetDetector quiet;
        quiet.prepare(48000.0);
        std::vector<float> loud(4800, 0.5f), silence(48000, 0.0f);
        quiet.process(loud.data(), 4800, 0.5f);
        for (int i = 0; i < 40; ++i)
            quiet.process(silence.data(), 48000, 0.5f);
        std::vector<float> hit = clicks(48000, 24000, 0.8f);
        expect("detector still works after long silence", quiet.process(hit.data(), 48000, 0.6f) >= 1);
    }
    OnsetDetector d;
    d.prepare(48000.0);
    float bad[3] = {std::nanf(""), INFINITY, 0.5f};
    expect("non-finite input is safe", d.process(bad, 3, std::nanf("")) >= 0 && d.process(nullptr, 5, 0.5f) == 0);
    if (failures == 0)
        std::cout << "OnsetDetectorTests passed\n";
    return failures == 0 ? 0 : 1;
}
