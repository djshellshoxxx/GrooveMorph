// F03 Classic band split (docs/specs/F03-GROOVE-PROTECTION.md section 2, TODO B-09).
#include "core/dsp/ClassicBandSplit.h"

#include <algorithm>
#include <cmath>
#include <complex>
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

ClassicBandSplit make(ClassicSlope slope, int mode, double fc, double fs = 48000.0, int block = 1024) {
    ClassicBandSplit s;
    s.prepare(fs, block);
    s.setSlope(slope);
    s.setMode(mode);
    s.setCutoff(fc);
    s.reset();
    return s;
}

// Impulse response of the effected band (mode 1 = high band, mode 2 = low band).
std::vector<float> effectedImpulse(ClassicSlope slope, int mode, double fc, double fs, std::size_t n) {
    auto s = make(slope, mode, fc, fs, static_cast<int>(n));
    std::vector<float> x(n, 0.0f);
    x[0] = 1.0f;
    float* ch[] = {x.data()};
    s.split(ch, 1, static_cast<int>(n));
    return x;
}
std::string tag(ClassicSlope slope, int mode, double fs, double fc) {
    return std::string(slope == ClassicSlope::Butterworth12 ? "12dB " : "6dB ") + (mode == 1 ? "high " : "low ")
         + std::to_string(static_cast<int>(fs)) + " " + std::to_string(static_cast<int>(fc));
}
} // namespace

int main() {
    // F03-T06: effected-band rolloff two octaves into the stop band. 12 dB/oct is a 2nd-order
    // Butterworth (|H| = 1/sqrt(1 + w^4): -24.1 dB at w = 4); 6 dB/oct is one-pole (|H| = 1/sqrt(1 + w^2):
    // -12.3 dB). The pass band two octaves out stays within 0.5 dB of unity and fc sits at -3 dB.
    for (ClassicSlope slope : {ClassicSlope::OnePole6, ClassicSlope::Butterworth12})
        for (double fs : {44100.0, 48000.0, 96000.0})
            for (double fc : {40.0, 120.0, 400.0})
                for (int mode : {1, 2}) {
                    const std::size_t n = static_cast<std::size_t>(fs * 0.6);
                    const auto h = effectedImpulse(slope, mode, fc, fs, n);
                    const std::string t = tag(slope, mode, fs, fc);
                    const double stop = mode == 1 ? fc / 4.0 : fc * 4.0;
                    const double pass = mode == 1 ? fc * 4.0 : fc / 4.0;
                    const double w = 4.0;
                    const double expected = slope == ClassicSlope::Butterworth12
                                              ? -10.0 * std::log10(1.0 + std::pow(w, 4.0))
                                              : -10.0 * std::log10(1.0 + w * w);
                    const double stopDb = db(std::abs(dft(h, stop, fs)));
                    expect("F03-T06 stop band two octaves out " + t + " (" + std::to_string(stopDb) + " dB)",
                           std::abs(stopDb - expected) < 1.0);
                    expect("F03-T06 pass band two octaves out " + t, std::abs(db(std::abs(dft(h, pass, fs)))) < 0.75);
                    expect("F03-T06 -3 dB at fc " + t, std::abs(db(std::abs(dft(h, fc, fs))) + 3.01) < 0.3);
                    // The 12 dB option must be steeper than the 6 dB option by about 12 dB two octaves out.
                    if (slope == ClassicSlope::Butterworth12) {
                        const auto h6 = effectedImpulse(ClassicSlope::OnePole6, mode, fc, fs, n);
                        const double diff = stopDb - db(std::abs(dft(h6, stop, fs)));
                        expect("F03-T06 12 dB steeper than 6 dB by 12 dB " + t, std::abs(diff + 11.8) < 1.5);
                    }
                }

    // F03 section 2: the bypassed band is the exact complement, so effected + bypass is flat (within
    // float rounding) at every frequency, for both slopes and both modes.
    for (ClassicSlope slope : {ClassicSlope::OnePole6, ClassicSlope::Butterworth12})
        for (int mode : {1, 2}) {
            const int n = 4096;
            auto s = make(slope, mode, 120.0, 48000.0, n);
            std::vector<float> x(n, 0.0f);
            x[0] = 1.0f;
            float* ch[] = {x.data()};
            s.split(ch, 1, n);
            std::vector<float> sum(n);
            for (int i = 0; i < n; ++i) sum[i] = x[i] + s.bypassBand(0)[i];
            double worst = 0.0;
            for (int k = 0; k < 40; ++k) {
                const double f = 20.0 * std::pow(1000.0, k / 39.0);
                worst = std::max(worst, std::abs(db(std::abs(dft(sum, f, 48000.0)))));
            }
            expect("F03 complement sum flat " + tag(slope, mode, 48000.0, 120.0), worst < 0.01);
        }

    // F03-T08 (Classic): unchanged band is bit-exact, both slopes and modes, random block sizes.
    for (ClassicSlope slope : {ClassicSlope::OnePole6, ClassicSlope::Butterworth12})
        for (int mode : {1, 2}) {
            auto s = make(slope, mode, 150.0);
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
                const float* orig[] = {in.data(), in.data()};
                s.split(two, 2, n);
                s.merge(two, orig, 2, n); // an engine that changes nothing
                exact = l == in && r == in;
            }
            expect("F03-T08 Classic bit-exact when unchanged", exact);
        }

    // When the engine changes the band, the recombined steady-state sine keeps its amplitude.
    for (ClassicSlope slope : {ClassicSlope::OnePole6, ClassicSlope::Butterworth12})
        for (int mode : {1, 2})
            for (double f : {50.0, 150.0, 1000.0, 8000.0}) {
                const int n = 48000;
                auto s = make(slope, mode, 150.0, 48000.0, n);
                std::vector<float> x(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * pi * f * i / 48000.0));
                const std::vector<float> in = x;
                float* ch[] = {x.data()};
                const float* orig[] = {in.data()};
                s.split(ch, 1, n);
                for (auto& v : x) v *= 1.0000001f; // the engine "changes" every sample by an ulp or so
                s.merge(ch, orig, 1, n);
                double sq = 0.0;
                for (int i = n / 2; i < n; ++i) sq += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
                const double rms = std::sqrt(sq / (n - n / 2));
                expect("F03 recombined amplitude " + tag(slope, mode, 48000.0, 150.0) + " f=" + std::to_string(static_cast<int>(f)),
                       std::abs(db(rms / (0.5 / std::sqrt(2.0)))) < 0.05);
            }

    // Channels are independent and identical input gives identical channels.
    {
        auto s = make(ClassicSlope::Butterworth12, 1, 200.0, 48000.0, 512);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        std::vector<float> a(512), b(512);
        for (int i = 0; i < 512; ++i) { a[i] = d(rng); b[i] = d(rng); }
        std::vector<float> l = a, r = b, mono = a;
        float* ch[] = {l.data(), r.data()};
        s.split(ch, 2, 512);
        auto m = make(ClassicSlope::Butterworth12, 1, 200.0, 48000.0, 512);
        float* mc[] = {mono.data()};
        m.split(mc, 1, 512);
        expect("F03 left channel unaffected by right", l == mono);
        auto same = make(ClassicSlope::Butterworth12, 1, 200.0, 48000.0, 512);
        std::vector<float> x = a, y = a;
        float* c2[] = {x.data(), y.data()};
        same.split(c2, 2, 512);
        expect("F03 identical input gives identical channels", x == y);
    }

    // Mode and slope changes clear the filter states (a stale state would click); a cutoff change does not.
    {
        auto s = make(ClassicSlope::OnePole6, 1, 120.0, 48000.0, 256);
        std::vector<float> dc(256, 0.8f);
        float* ch[] = {dc.data()};
        s.split(ch, 1, 256); // charge the one-pole state
        s.setSlope(ClassicSlope::Butterworth12);
        s.setSlope(ClassicSlope::OnePole6);
        std::vector<float> after(256, 0.8f);
        float* ch2[] = {after.data()};
        s.split(ch2, 1, 256);
        auto fresh = make(ClassicSlope::OnePole6, 1, 120.0, 48000.0, 256);
        std::vector<float> ref(256, 0.8f);
        float* ch3[] = {ref.data()};
        fresh.split(ch3, 1, 256);
        expect("F03 slope change clears state", after == ref);

        auto t = make(ClassicSlope::Butterworth12, 2, 120.0, 48000.0, 256);
        std::vector<float> dc2(256, 0.8f);
        float* ch4[] = {dc2.data()};
        t.split(ch4, 1, 256);
        t.setMode(1);
        t.setMode(2);
        std::vector<float> after2(256, 0.8f);
        float* ch5[] = {after2.data()};
        t.split(ch5, 1, 256);
        auto fresh2 = make(ClassicSlope::Butterworth12, 2, 120.0, 48000.0, 256);
        std::vector<float> ref2(256, 0.8f);
        float* ch6[] = {ref2.data()};
        fresh2.split(ch6, 1, 256);
        expect("F03 mode change clears state", after2 == ref2);

        auto u = make(ClassicSlope::OnePole6, 2, 120.0, 48000.0, 256);
        std::vector<float> dc3(256, 0.8f);
        float* ch7[] = {dc3.data()};
        u.split(ch7, 1, 256);
        u.setCutoff(121.0);
        std::vector<float> cont(256, 0.8f);
        float* ch8[] = {cont.data()};
        u.split(ch8, 1, 256);
        expect("F03 cutoff change keeps state (low band stays charged)", cont[0] > 0.7f); // fresh state would give 0.8 * coefficient
    }

    // Full band (mode 0) leaves the audio untouched in split and merge.
    {
        auto s = make(ClassicSlope::Butterworth12, 0, 120.0, 48000.0, 64);
        std::vector<float> x(64, 0.3f);
        const std::vector<float> in = x;
        float* ch[] = {x.data()};
        const float* orig[] = {in.data()};
        s.split(ch, 1, 64);
        expect("F03 full band split is a no-op", x == in);
        for (auto& v : x) v *= 0.5f;
        s.merge(ch, orig, 1, 64);
        bool halved = true;
        for (int i = 0; i < 64; ++i) halved = halved && x[i] == 0.15f;
        expect("F03 full band merge is a no-op", halved);
    }

    // F03-T05 (core part): NaN/Inf input is sanitised and never poisons the state; the cutoff clamps.
    for (ClassicSlope slope : {ClassicSlope::OnePole6, ClassicSlope::Butterworth12}) {
        auto s = make(slope, 2, 120.0, 48000.0, 64);
        std::vector<float> x(64, 0.25f);
        x[3] = std::nanf(""); x[10] = INFINITY; x[11] = -INFINITY;
        const std::vector<float> in = x;
        float* ch[] = {x.data()};
        const float* orig[] = {in.data()};
        s.split(ch, 1, 64);
        bool finite = true;
        for (float v : x) finite = finite && std::isfinite(v);
        for (int i = 0; i < 64; ++i) finite = finite && std::isfinite(s.bypassBand(0)[i]);
        expect("F03-T05 split output finite after NaN/Inf", finite);
        for (auto& v : x) v *= 0.5f;
        s.merge(ch, orig, 1, 64);
        bool mergedFinite = true;
        for (int i = 0; i < 64; ++i)
            if (i != 3 && i != 10 && i != 11) mergedFinite = mergedFinite && std::isfinite(x[i]);
        expect("F03-T05 merge output finite where the input was finite", mergedFinite);
        std::vector<float> later(64, 0.25f);
        float* ch2[] = {later.data()};
        s.split(ch2, 1, 64);
        bool stillFinite = true;
        for (float v : later) stillFinite = stillFinite && std::isfinite(v);
        expect("F03-T05 state stays finite in the next block", stillFinite);

        s.setCutoff(5.0);
        expect("F03 cutoff clamps low", s.cutoffHz() == 20.0);
        s.setCutoff(5000.0);
        expect("F03 cutoff clamps high", s.cutoffHz() == 1000.0);
        s.setCutoff(std::nan(""));
        expect("F03 NaN cutoff falls back", std::isfinite(s.cutoffHz()) && s.cutoffHz() >= 20.0 && s.cutoffHz() <= 1000.0);
    }

    // Oversized blocks and extra channels are bounded by prepare()'s capacity.
    {
        auto s = make(ClassicSlope::OnePole6, 1, 120.0, 48000.0, 32);
        std::vector<float> a(64, 0.5f), b(64, 0.5f), c(64, 0.5f);
        float* ch[] = {a.data(), b.data(), c.data()};
        s.split(ch, 3, 64);
        bool tailUntouched = true;
        for (int i = 32; i < 64; ++i) tailUntouched = tailUntouched && a[i] == 0.5f && b[i] == 0.5f;
        bool thirdUntouched = true;
        for (float v : c) thirdUntouched = thirdUntouched && v == 0.5f;
        expect("F03 split bounded by capacity", tailUntouched);
        expect("F03 split bounded by maxChannels", thirdUntouched);
    }

    if (failures == 0)
        std::cout << "ClassicBandSplitTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
