// F03/F10 Linkwitz-Riley crossover (docs/specs/W3-LR-CROSSOVER.md).
#include "core/dsp/Crossover.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace groovemorph::core::dsp;

namespace {
int failures = 0;
void expect(const std::string& name, bool ok) {
    if (!ok) { std::cerr << name << ": check failed\n"; ++failures; }
}
constexpr double pi = 3.14159265358979323846;

std::complex<double> dft(const std::vector<float>& x, double f, double fs) {
    std::complex<double> acc = 0.0;
    const double w = 2.0 * pi * f / fs;
    for (std::size_t i = 0; i < x.size(); ++i)
        acc += static_cast<double>(x[i]) * std::polar(1.0, -w * static_cast<double>(i));
    return acc;
}
double db(double v) { return 20.0 * std::log10(std::max(v, 1.0e-12)); }

struct Bands { std::vector<float> low, high; };
Bands direct(double fs, double fc, Topology topo, std::size_t n) {
    LrChannel ch;
    ch.setTopology(topo);
    ch.reset();
    ch.setCutoff(fc, fs);
    Bands b; b.low.resize(n); b.high.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        float lo = 0, hi = 0;
        ch.process(i == 0 ? 1.0f : 0.0f, lo, hi);
        b.low[i] = lo; b.high[i] = hi;
    }
    return b;
}

BandSplit make(Topology t, int mode, double fc, double fs = 48000.0, int block = 1024) {
    BandSplit s;
    s.prepare(fs, block);
    s.setTopology(t);
    s.setMode(mode);
    s.setCutoff(fc);
    s.reset();
    return s;
}
} // namespace

int main() {
    // F10-T03 / W3-LR-T01/T02/T04: flat sum, -6 dB at fc, slope, across sample rates.
    for (Topology topo : {Topology::LR2, Topology::LR4})
        for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
            for (double fc : {40.0, 120.0, 400.0}) {
                const std::size_t n = static_cast<std::size_t>(fs * 0.6);
                const auto b = direct(fs, fc, topo, n);
                const std::string tag = std::string(topo == Topology::LR4 ? "LR4 " : "LR2 ") + std::to_string(static_cast<int>(fs)) + " " + std::to_string(static_cast<int>(fc));
                double worst = 0.0;
                const double top = std::min(20000.0, 0.45 * fs);
                for (int k = 0; k < 40; ++k) {
                    const double f = 20.0 * std::pow(top / 20.0, k / 39.0);
                    worst = std::max(worst, std::abs(db(std::abs(dft(b.low, f, fs) + dft(b.high, f, fs)))));
                }
                expect("F10-T03 flat sum " + tag, worst < 0.1);
                expect("F10-T03 low -6 dB at fc " + tag, std::abs(db(std::abs(dft(b.low, fc, fs))) + 6.02) < 0.1);
                expect("F10-T03 high -6 dB at fc " + tag, std::abs(db(std::abs(dft(b.high, fc, fs))) + 6.02) < 0.1);
                const double hz = 4.0 * fc;
                const double w = hz / fc;
                const double analytic = topo == Topology::LR4 ? -20.0 * std::log10(1.0 + w * w * w * w) / 2.0 * 1.0
                                                              : -20.0 * std::log10(1.0 + w * w);
                // LR4 magnitude = 1/(1+w^4)^(1/2)*... use exact Butterworth^2: |H|^2 = 1/(1+w^4) per stage => LR4 |H| = 1/(1+w^4).
                const double lr4 = -20.0 * std::log10(1.0 + std::pow(w, 4));
                const double lr2 = -10.0 * std::log10(std::pow(1.0 + w * w, 2));
                (void) analytic;
                const double expected = topo == Topology::LR4 ? lr4 : lr2;
                expect("W3-LR-T02 low rolloff " + tag, std::abs(db(std::abs(dft(b.low, hz, fs))) - expected) < 1.0);
            }

    // F10-T04 / W3-LR-T03: channels are independent, identical input gives identical output.
    {
        auto s = make(Topology::LR4, 1, 200.0, 48000.0, 512);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        std::vector<float> a(512), b(512);
        for (int i = 0; i < 512; ++i) { a[i] = d(rng); b[i] = d(rng); }
        std::vector<float> l = a, r = b, mono = a;
        float* ch[] = {l.data(), r.data()};
        s.split(ch, 2, 512);
        auto m = make(Topology::LR4, 1, 200.0, 48000.0, 512);
        float* mc[] = {mono.data()};
        m.split(mc, 1, 512);
        expect("W3-LR-T03 left channel unaffected by right", l == mono);
        auto same = make(Topology::LR4, 1, 200.0, 48000.0, 512);
        std::vector<float> x = a, y = a;
        float* c2[] = {x.data(), y.data()};
        same.split(c2, 2, 512);
        expect("W3-LR-T03 identical input gives identical channels", x == y);
    }

    // F03-T08 / W3-LR-T06: unchanged band is bit-exact, both topologies and modes, random block sizes.
    for (Topology topo : {Topology::LR2, Topology::LR4})
        for (int mode : {1, 2}) {
            auto s = make(topo, mode, 150.0);
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> d(-1.0f, 1.0f);
            std::uniform_int_distribution<int> sz(1, 1024);
            bool exact = true;
            for (int blk = 0; blk < 1000 && exact; ++blk) {
                const int n = sz(rng);
                std::vector<float> in(static_cast<std::size_t>(n));
                for (auto& v : in) v = d(rng);
                std::vector<float> l = in, r = in;
                float* two[] = {l.data(), r.data()};
                s.split(two, 2, n);
                s.merge(two, 2, n); // an engine that changes nothing
                exact = l == in && r == in;
            }
            expect("F03-T08 LR bit-exact when unchanged", exact);
        }

    // W3-LR-T07: when the engine changes the band, the recombined steady-state sine keeps its amplitude (all-pass).
    for (Topology topo : {Topology::LR2, Topology::LR4})
        for (int mode : {1, 2})
            for (double f : {50.0, 150.0, 1000.0, 8000.0}) {
                auto s = make(topo, mode, 150.0, 48000.0, 48000);
                const int n = 48000;
                std::vector<float> x(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * pi * f * i / 48000.0));
                float* ch[] = {x.data()};
                s.split(ch, 1, n);
                for (auto& v : x) v *= 1.0000001f; // the engine "changes" every sample by an ulp or so
                bool changed = false;
                s.merge(ch, 1, n);
                (void) changed;
                double sq = 0.0;
                for (int i = n / 2; i < n; ++i) sq += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
                const double rms = std::sqrt(sq / (n - n / 2));
                expect("W3-LR-T07 allpass amplitude", std::abs(db(rms / (0.5 / std::sqrt(2.0)))) < 0.1);
            }

    // W3-LR-T08: effect onset produces no click.
    for (Topology topo : {Topology::LR2, Topology::LR4}) {
        auto s = make(topo, 1, 150.0, 48000.0, 4096);
        const int n = 4096;
        std::vector<float> in(n);
        double maxInStep = 0.0;
        for (int i = 0; i < n; ++i) { in[i] = 0.5f * static_cast<float>(std::sin(2.0 * pi * 440.0 * i / 48000.0)); if (i) maxInStep = std::max<double>(maxInStep, std::abs(in[i] - in[i - 1])); }
        std::vector<float> x = in;
        float* ch[] = {x.data()};
        s.split(ch, 1, n);
        for (int i = 2000; i < n; ++i) x[i] *= 0.5f; // an effect starts at 2000
        s.merge(ch, 1, n);
        double maxStep = 0.0;
        for (int i = 1; i < n; ++i) maxStep = std::max<double>(maxStep, std::abs(x[i] - x[i - 1]));
        expect("W3-LR-T08 no onset click", maxStep < 2.0 * maxInStep + 1.0e-3);
    }

    // F10-T06 / W3-LR-T05: cutoff sweep and a topology switch stay bounded and finite.
    {
        auto s = make(Topology::LR4, 1, 40.0, 48000.0, 256);
        double maxStep = 0.0, maxIn = 0.0;
        float prev = 0.0f, prevIn = 0.0f;
        bool finite = true;
        int t = 0;
        for (int blk = 0; blk < 400; ++blk) {
            std::vector<float> x(256);
            for (auto& v : x) { v = 0.5f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t / 48000.0)); ++t; }
            s.setCutoff(blk < 200 ? 40.0 + 360.0 * blk / 200.0 : 400.0 - 360.0 * (blk - 200) / 200.0);
            if (blk == 300) s.setTopology(Topology::LR2);
            const std::vector<float> in = x;
            float* ch[] = {x.data()};
            s.split(ch, 1, 256);
            for (auto& v : x) v += 1.0e-3f;
            s.merge(ch, 1, 256);
            for (int i = 0; i < 256; ++i) {
                finite = finite && std::isfinite(x[i]);
                if (blk > 2) { maxStep = std::max<double>(maxStep, std::abs(x[i] - prev)); maxIn = std::max<double>(maxIn, std::abs(in[i] - prevIn)); }
                prev = x[i]; prevIn = in[i];
            }
        }
        expect("F10-T06 sweep finite", finite);
        expect("F10-T06 sweep step bounded", maxStep < 2.0 * maxIn + 5.0e-3);
    }

    // W3-LR-T09: NaN/Inf input never poisons the state.
    {
        auto s = make(Topology::LR4, 2, 120.0, 48000.0, 64);
        std::vector<float> x(64, 0.25f);
        x[3] = std::nanf(""); x[10] = INFINITY; x[11] = -INFINITY;
        float* ch[] = {x.data()};
        s.split(ch, 1, 64);
        s.merge(ch, 1, 64);
        bool finite = true;
        for (float v : x) finite = finite && std::isfinite(v);
        expect("W3-LR-T09 finite after garbage", finite);
        std::vector<float> y(64, 0.25f);
        float* c2[] = {y.data()};
        s.split(c2, 1, 64);
        s.merge(c2, 1, 64);
        bool ok = true;
        for (float v : y) ok = ok && std::isfinite(v);
        expect("W3-LR-T09 later block finite", ok);
    }

    if (failures == 0) std::cout << "CrossoverTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
