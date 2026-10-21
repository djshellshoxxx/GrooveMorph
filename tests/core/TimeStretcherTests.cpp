#include "core/dsp/TimeStretcher.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <limits>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace groovemorph::core;

namespace
{
int failures = 0;
void expect(const std::string& name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}
constexpr double pi = 3.14159265358979323846;
constexpr double rate = 48000.0;

std::vector<float> tone(double hz, int n, bool harmonics, double phase = 0.0)
{
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        double s = std::sin(2 * pi * hz * t + phase);
        if (harmonics)
            s = 0.6 * s + 0.3 * std::sin(2 * pi * 2 * hz * t + 1.0) + 0.2 * std::sin(2 * pi * 3 * hz * t + 2.0);
        v[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * s);
    }
    return v;
}

// Fundamental by autocorrelation over the middle of the signal, parabolic interpolation, smallest lag
// within 90 % of the best peak (avoids octave errors).
double estimateHz(const std::vector<float>& x)
{
    const int n = static_cast<int>(x.size());
    const int begin = n / 4, len = std::min(n / 2, 12000);
    const int minLag = static_cast<int>(rate / 1000.0), maxLag = static_cast<int>(rate / 60.0);
    std::vector<double> r(static_cast<std::size_t>(maxLag + 2), 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double d = 0, e1 = 0, e2 = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = x[static_cast<std::size_t>(begin + i)], b = x[static_cast<std::size_t>(begin + i + lag)];
            d += a * b;
            e1 += a * a;
            e2 += b * b;
        }
        r[static_cast<std::size_t>(lag)] = d / std::sqrt(e1 * e2 + 1e-12);
    }
    double best = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        best = std::max(best, r[static_cast<std::size_t>(lag)]);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double v = r[static_cast<std::size_t>(lag)];
        if (v >= 0.9 * best && v >= r[static_cast<std::size_t>(lag - 1)] && v >= r[static_cast<std::size_t>(lag + 1)])
        {
            const double a = r[static_cast<std::size_t>(lag - 1)], b = v, c = r[static_cast<std::size_t>(lag + 1)];
            const double denom = a - 2 * b + c;
            const double shift = std::abs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
            return rate / (lag + shift);
        }
    }
    return 0.0;
}
double cents(double a, double b) { return 1200.0 * std::log2(a / b); }
double rms(const std::vector<float>& x)
{
    double s = 0;
    for (float v : x)
        s += static_cast<double>(v) * v;
    return std::sqrt(s / static_cast<double>(x.size()));
}
bool finite(const std::vector<std::vector<float>>& o)
{
    for (const auto& c : o)
        for (float v : c)
            if (!std::isfinite(v))
                return false;
    return true;
}
} // namespace

int main()
{
    const double ratios[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    const int n = 48000 * 2;

    // ST-1 length, ST-3 finite, mono and stereo, circular and not.
    for (double ratio : ratios)
        for (bool circular : {false, true})
        {
            const auto a = tone(220.0, n, false);
            const auto b = tone(330.0, n, true);
            const float* in[2] = {a.data(), b.data()};
            std::vector<std::vector<float>> out;
            StretchOptions o;
            o.ratio = ratio;
            o.sampleRate = rate;
            o.circular = circular;
            const bool ok = TimeStretcher::stretch(in, 2, n, o, out);
            const std::string tag = " r=" + std::to_string(ratio) + (circular ? " circ" : "");
            expect("ST-1 ok" + tag, ok);
            if (!ok)
                continue;
            const auto expected = static_cast<std::size_t>(std::llround(n * ratio));
            expect("ST-1 length" + tag, out.size() == 2 && out[0].size() == expected && out[1].size() == expected);
            expect("ST-3 finite" + tag, finite(out));
        }

    // ST-2 pitch (sine and harmonic tone), ST-3 energy, plus ST-5 stereo.
    for (double ratio : {0.5, 0.8, 1.5, 2.0})
        for (bool harmonics : {false, true})
            for (double hz : {110.0, 220.0, 440.0})
            {
                const auto a = tone(hz, n, harmonics);
                const float* in[1] = {a.data()};
                std::vector<std::vector<float>> out;
                StretchOptions o;
                o.ratio = ratio;
                o.sampleRate = rate;
                o.circular = false;
                TimeStretcher::stretch(in, 1, n, o, out);
                const double est = estimateHz(out[0]);
                const std::string tag = " hz=" + std::to_string(hz) + " r=" + std::to_string(ratio) + (harmonics ? " harm" : " sine");
                expect("ST-2 pitch within 10 cents" + tag, est > 0 && std::abs(cents(est, hz)) < 10.0);
                const double db = 20.0 * std::log10(rms(out[0]) / rms(a));
                expect("ST-3 rms within 1.5 dB" + tag, std::abs(db) < 1.5);
            }

    // ST-4 identity and rejection.
    {
        auto a = tone(220.0, 4000, true);
        a[10] = std::nanf("");
        const float* in[1] = {a.data()};
        std::vector<std::vector<float>> out;
        StretchOptions o;
        o.sampleRate = rate;
        o.ratio = 1.0;
        expect("ST-4 identity ok", TimeStretcher::stretch(in, 1, 4000, o, out) && out[0].size() == 4000);
        bool same = true;
        for (int i = 0; i < 4000; ++i)
            same = same && (i == 10 ? out[0][10] == 0.0f : out[0][static_cast<std::size_t>(i)] == a[static_cast<std::size_t>(i)]);
        expect("ST-4 identity exact (NaN zeroed)", same);
        o.ratio = 0.49;
        expect("ST-4 ratio below range rejected", !TimeStretcher::stretch(in, 1, 4000, o, out));
        o.ratio = 2.01;
        expect("ST-4 ratio above range rejected", !TimeStretcher::stretch(in, 1, 4000, o, out));
        o.ratio = std::nan("");
        expect("ST-4 nan ratio rejected", !TimeStretcher::stretch(in, 1, 4000, o, out));
        o.ratio = 1.5;
        expect("ST-4 too short rejected", !TimeStretcher::stretch(in, 1, 8, o, out));
        expect("ST-4 null rejected", !TimeStretcher::stretch(nullptr, 1, 4000, o, out));
        o.sampleRate = 0.0;
        expect("ST-4 bad rate rejected", !TimeStretcher::stretch(in, 1, 4000, o, out));
        // Hostile content must not produce NaN/inf.
        std::vector<float> junk(20000, 1.0e30f);
        junk[5] = std::numeric_limits<float>::infinity();
        const float* jin[1] = {junk.data()};
        o.sampleRate = rate;
        expect("ST-4 junk ok", TimeStretcher::stretch(jin, 1, 20000, o, out));
        expect("ST-4 junk finite", out.size() == 1 && finite(out) || out.empty());
    }

    // ST-5 stereo: identical channels stay identical, different channels keep their own pitch.
    {
        const auto a = tone(220.0, n, true);
        const float* same[2] = {a.data(), a.data()};
        std::vector<std::vector<float>> out;
        StretchOptions o;
        o.ratio = 1.4;
        o.sampleRate = rate;
        TimeStretcher::stretch(same, 2, n, o, out);
        expect("ST-5 identical channels identical", out[0] == out[1]);
        const auto b = tone(330.0, n, false);
        const float* diff[2] = {a.data(), b.data()};
        TimeStretcher::stretch(diff, 2, n, o, out);
        expect("ST-5 left pitch", std::abs(cents(estimateHz(out[0]), 220.0)) < 10.0);
        expect("ST-5 right pitch", std::abs(cents(estimateHz(out[1]), 330.0)) < 10.0);
    }

    // ST-6 circular seam: the step across the wrap is comparable to the largest in-loop step.
    {
        const int len = 48000 * 2;
        const auto a = tone(200.0, len, true); // an exact number of periods: a clean loop
        const float* in[1] = {a.data()};
        for (double ratio : {0.7, 1.3, 1.9})
        {
            std::vector<std::vector<float>> out;
            StretchOptions o;
            o.ratio = ratio;
            o.sampleRate = rate;
            o.circular = true;
            TimeStretcher::stretch(in, 1, len, o, out);
            double maxStep = 0;
            for (std::size_t i = 1; i < out[0].size(); ++i)
                maxStep = std::max(maxStep, static_cast<double>(std::abs(out[0][i] - out[0][i - 1])));
            const double seam = std::abs(out[0].front() - out[0].back());
            expect("ST-6 seam step bounded r=" + std::to_string(ratio), seam <= 1.5 * maxStep + 1e-6);
        }
    }

    // Transient material: no NaN, bounded energy (artifacts are documented, not asserted).
    {
        std::vector<float> clicks(48000 * 2, 0.0f);
        for (std::size_t i = 0; i < clicks.size(); i += 12000)
            for (int k = 0; k < 200; ++k)
                clicks[i + static_cast<std::size_t>(k)] = 0.9f * std::exp(-k / 30.0f) * ((k & 1) ? -1.f : 1.f);
        const float* in[1] = {clicks.data()};
        for (double ratio : {0.6, 1.5})
        {
            std::vector<std::vector<float>> out;
            StretchOptions o;
            o.ratio = ratio;
            o.sampleRate = rate;
            o.circular = true;
            expect("clicks ok", TimeStretcher::stretch(in, 1, static_cast<std::int64_t>(clicks.size()), o, out));
            const double db = 20.0 * std::log10((rms(out[0]) + 1e-12) / rms(clicks));
            expect("clicks finite", finite(out));
            expect("clicks energy bounded (within 6 dB)", std::abs(db) < 6.0);
        }
    }

    // ST-7 cancel and worker.
    {
        const auto a = tone(220.0, n, true);
        const float* in[1] = {a.data()};
        std::vector<std::vector<float>> out;
        StretchOptions o;
        o.ratio = 1.5;
        o.sampleRate = rate;
        std::atomic<bool> cancelled{true};
        expect("ST-7 cancel aborts", !TimeStretcher::stretch(in, 1, n, o, out, &cancelled));

        StretchWorker worker;
        StretchWorker::Result r;
        expect("ST-7 nothing yet", !worker.tryTake(r));
        for (std::uint64_t id = 1; id <= 3; ++id)
        {
            StretchWorker::Job job;
            job.id = id;
            job.input = {a};
            job.options = o;
            job.options.ratio = 1.0 + 0.1 * static_cast<double>(id);
            worker.submit(std::move(job));
        }
        bool got = false;
        for (int i = 0; i < 400 && !got; ++i)
        {
            got = worker.tryTake(r);
            if (!got)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        expect("ST-7 worker delivers newest", got && r.ok && r.id == 3);
        expect("ST-7 worker length", got && r.output.size() == 1
                                         && r.output[0].size() == static_cast<std::size_t>(std::llround(n * 1.3)));
        StretchWorker::Job bad;
        bad.id = 9;
        worker.submit(std::move(bad));
        got = false;
        for (int i = 0; i < 100 && !got; ++i)
        {
            got = worker.tryTake(r);
            if (!got)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        expect("ST-7 empty job reported not ok", got && !r.ok && r.id == 9);
        // Destruction while a job is running must join promptly.
        StretchWorker::Job job;
        job.id = 1;
        job.input = {a};
        job.options = o;
        worker.submit(std::move(job));
        worker.cancelAll();
    }

    // ST-8 planning.
    {
        auto p = planStretch(100.0, 100.0, 1.0, 2.0);
        expect("ST-8 same tempo: ratio 1, nothing to do", std::abs(p.ratio - 1.0) < 1e-12 && !p.needsNew && !p.limited);
        p = planStretch(100.0, 125.0, 1.0, 2.0);
        expect("ST-8 faster host -> shorter", std::abs(p.ratio - 0.8) < 1e-12 && p.needsNew && !p.limited);
        p = planStretch(100.0, 125.0, 0.8, 2.0);
        expect("ST-8 already in use", !p.needsNew);
        p = planStretch(100.0, 125.1, 0.8, 2.0);
        expect("ST-8 hysteresis (0.08 % change)", !p.needsNew);
        p = planStretch(100.0, 150.0, 1.0, 2.0);
        expect("ST-8 clamped low", std::abs(p.ratio - 2.0 / 3.0) < 1e-9 && !p.limited);
        p = planStretch(100.0, 400.0, 1.0, 2.0);
        expect("ST-8 below 0.5 clamps and reports", std::abs(p.ratio - 0.5) < 1e-12 && p.limited);
        p = planStretch(100.0, 40.0, 1.0, 1.6);
        expect("ST-8 capacity clamp", std::abs(p.ratio - 1.6) < 1e-12 && p.limited);
        p = planStretch(100.0, 40.0, 1.6, 1.6);
        expect("ST-8 stays clamped without re-render", !p.needsNew && p.limited);
        p = planStretch(0.0, 100.0, 1.0, 2.0);
        expect("ST-8 invalid native tempo", !p.needsNew && p.ratio == 1.0);
        expect("ST-8 output length helper", TimeStretcher::outputLength(1000, 1.5) == 1500);
    }

    if (failures == 0)
        std::cout << "TimeStretcherTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
