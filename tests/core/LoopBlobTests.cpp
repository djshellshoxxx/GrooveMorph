#include "core/dsp/LoopBlob.h"
#include "core/dsp/WaveformPeaks.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::LoopBlob;

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

std::vector<float> music(int n, double hz, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-0.02f, 0.02f);
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * pi * hz * i / 48000.0)) + noise(rng);
    return v;
}
bool roundTrip(const std::vector<float>& l, const std::vector<float>* r, double rate, double beats, double origin,
               std::size_t* size = nullptr)
{
    const float* ch[2] = {l.data(), r != nullptr ? r->data() : l.data()};
    std::vector<std::uint8_t> blob;
    if (encode(ch, r != nullptr ? 2 : 1, static_cast<std::int64_t>(l.size()), rate, beats, origin, blob) != Status::Ok)
        return false;
    if (size != nullptr)
        *size = blob.size();
    Audio a;
    if (decode(blob.data(), blob.size(), a) != Status::Ok)
        return false;
    if (a.numSamples() != static_cast<std::int64_t>(l.size()) || a.right.size() != l.size() || a.sampleRate != std::round(rate)
        || a.loopBeats != beats || a.originBeats != origin)
        return false;
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        const float el = std::clamp(l[i], -1.0f, 1.0f), er = std::clamp(r != nullptr ? (*r)[i] : l[i], -1.0f, 1.0f);
        if (std::abs(a.left[i] - el) > 1.0f / 32767.0f || std::abs(a.right[i] - er) > 1.0f / 32767.0f)
            return false;
    }
    return true;
}
} // namespace

int main()
{
    // PB-1 round trips.
    const auto l = music(48000 * 3, 220.0, 1);
    const auto r = music(48000 * 3, 331.0, 2);
    std::size_t stereoSize = 0, monoSize = 0;
    expect("PB-1 stereo", roundTrip(l, &r, 48000.0, 8.0, 1.5, &stereoSize));
    expect("PB-1 mono / dual mono", roundTrip(l, nullptr, 44100.0, 4.0, 0.0, &monoSize));
    expect("PB-1 dual mono stored once", monoSize < stereoSize * 0.7);
    expect("PB-1 silence", roundTrip(std::vector<float>(30000, 0.0f), nullptr, 48000.0, 2.0, 0.0));
    {
        std::vector<float> full(20000), over(20000);
        for (std::size_t i = 0; i < full.size(); ++i)
        {
            full[i] = (i & 1) ? 1.0f : -1.0f;
            over[i] = (i & 1) ? 3.0f : -3.0f; // clipped to full scale by design
        }
        expect("PB-1 full scale alternating", roundTrip(full, &full, 48000.0, 4.0, 0.0));
        expect("PB-1 over-range is clamped", roundTrip(over, &over, 48000.0, 4.0, 0.0));
        std::vector<float> opposite(20000);
        for (std::size_t i = 0; i < full.size(); ++i)
            opposite[i] = -full[i];
        expect("PB-1 opposite full-scale stereo", roundTrip(full, &opposite, 96000.0, 4.0, 3.25));
    }
    // PB-3 compression and refusals.
    {
        const std::size_t raw = l.size() * 2 * 2;
        expect("PB-3 smaller than raw 16-bit", stereoSize < raw);
        const float* ch[1] = {l.data()};
        std::vector<std::uint8_t> blob;
        expect("PB-3 too long", encode(ch, 1, static_cast<std::int64_t>(48000 * 21), 48000.0, 4.0, 0.0, blob) == Status::TooLong);
        expect("PB-3 nan beats", encode(ch, 1, 1000, 48000.0, std::nan(""), 0.0, blob) == Status::Invalid);
        expect("PB-3 zero beats", encode(ch, 1, 1000, 48000.0, 0.0, 0.0, blob) == Status::Invalid);
        expect("PB-3 inf origin", encode(ch, 1, 1000, 48000.0, 4.0, std::numeric_limits<double>::infinity(), blob) == Status::Invalid);
        expect("PB-3 bad rate", encode(ch, 1, 1000, 1000.0, 4.0, 0.0, blob) == Status::Invalid);
        expect("PB-3 too few samples", encode(ch, 1, 4, 48000.0, 4.0, 0.0, blob) == Status::Invalid);
        expect("PB-3 null", encode(nullptr, 1, 1000, 48000.0, 4.0, 0.0, blob) == Status::Invalid);
        // Incompressible 20 s stereo noise at 192 kHz exceeds the 10 MiB cap.
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> d(-1.0f, 1.0f);
        std::vector<float> a(static_cast<std::size_t>(192000 * 20)), b(a.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            a[i] = d(rng);
            b[i] = d(rng);
        }
        const float* both[2] = {a.data(), b.data()};
        expect("PB-3 oversized blob refused", encode(both, 2, static_cast<std::int64_t>(a.size()), 192000.0, 16.0, 0.0, blob) == Status::TooLarge
                                                  && blob.empty());
    }

    // PB-2 hostile blobs.
    {
        const float* ch[2] = {l.data(), r.data()};
        std::vector<std::uint8_t> good;
        encode(ch, 2, 20000, 48000.0, 4.0, 0.5, good);
        Audio a;
        expect("PB-2 good decodes", decode(good.data(), good.size(), a) == Status::Ok);
        expect("PB-2 empty", decode(nullptr, 0, a) == Status::Corrupt);
        expect("PB-2 tiny", decode(good.data(), 10, a) == Status::Corrupt);
        bool allTruncated = true;
        for (std::size_t len = 0; len < good.size(); len += (len < 80 ? 1 : 97))
            allTruncated = allTruncated && decode(good.data(), len, a) == Status::Corrupt;
        expect("PB-2 every truncation rejected", allTruncated);
        auto extra = good;
        extra.push_back(0);
        expect("PB-2 trailing byte", decode(extra.data(), extra.size(), a) == Status::Corrupt);
        bool flips = true;
        for (std::size_t i = 0; i < good.size(); i += 53)
        {
            auto bad = good;
            bad[i] ^= 0x10;
            flips = flips && decode(bad.data(), bad.size(), a) == Status::Corrupt;
        }
        expect("PB-2 flipped bytes rejected (CRC)", flips);
        // Header tampering with a recomputed-looking-valid structure still fails range checks: patch fields then fix nothing (CRC trips),
        // so also build blobs whose CRC is valid but fields are hostile.
        auto fixCrc = [](std::vector<std::uint8_t>& v) {
            std::uint32_t c = 0xFFFFFFFFu;
            for (std::size_t i = 0; i + 4 < v.size(); ++i)
            {
                c ^= v[i];
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            c ^= 0xFFFFFFFFu;
            for (int i = 0; i < 4; ++i)
                v[v.size() - 4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((c >> (8 * i)) & 0xFF);
        };
        auto hostile = [&](auto patch) {
            auto v = good;
            patch(v);
            fixCrc(v);
            return decode(v.data(), v.size(), a) == Status::Corrupt;
        };
        expect("PB-2 fixCrc helper is correct", [&] { auto v = good; fixCrc(v); return decode(v.data(), v.size(), a) == Status::Ok; }());
        expect("PB-2 bad magic", hostile([](auto& v) { v[0] = 'X'; }));
        expect("PB-2 version 2", hostile([](auto& v) { v[4] = 2; }));
        expect("PB-2 channels 3", hostile([](auto& v) { v[6] = 3; }));
        expect("PB-2 rate 100", hostile([](auto& v) { v[8] = 100; v[9] = 0; v[10] = 0; v[11] = 0; }));
        expect("PB-2 rate huge", hostile([](auto& v) { v[11] = 0x7f; }));
        expect("PB-2 huge sample count", hostile([](auto& v) { v[15] = 0x7f; }));
        expect("PB-2 sample count over 20 s", hostile([](auto& v) { v[12] = 0; v[13] = 0x80; v[14] = 0x0e; v[15] = 0; }));
        expect("PB-2 sample count beyond payload", hostile([](auto& v) { v[12] = 0x20; v[13] = 0x4e; v[14] = 0x0f; v[15] = 0; }));
        expect("PB-2 nan beats", hostile([](auto& v) { for (int i = 0; i < 8; ++i) v[16 + static_cast<std::size_t>(i)] = 0xff; }));
        expect("PB-2 negative beats", hostile([](auto& v) { v[23] = 0xc0; }));
        expect("PB-2 payload size lies", hostile([](auto& v) { v[32] ^= 1; }));
        expect("PB-2 garbage payload", hostile([](auto& v) { for (std::size_t i = 36; i + 4 < v.size(); ++i) v[i] = 0xff; }));
        expect("PB-2 random payload never crashes", [&] {
            std::mt19937 rng(3);
            bool ok = true;
            for (int t = 0; t < 200; ++t)
            {
                auto v = good;
                for (std::size_t i = 36; i + 4 < v.size(); ++i)
                    v[i] = static_cast<std::uint8_t>(rng());
                fixCrc(v);
                Audio x;
                const auto s = decode(v.data(), v.size(), x);
                ok = ok && (s == Status::Corrupt || s == Status::Ok);
            }
            return ok;
        }());
        std::vector<std::uint8_t> huge(maxEncodedBytes + 1000, 0);
        expect("PB-2 oversized input", decode(huge.data(), huge.size(), a) == Status::Corrupt);
    }

    // Plugin state envelope: 1 MiB no longer rejected, anything over 16 MiB (or empty) still is.
    expect("state limit accepts a 3 MiB state", stateSizeAcceptable(3u << 20));
    expect("state limit accepts the largest blob in base64 plus headroom", stateSizeAcceptable(maxEncodedBytes / 3 * 4 + (1u << 20)));
    expect("state limit rejects oversized state", !stateSizeAcceptable(maxStateBytes + 1) && !stateSizeAcceptable(std::size_t{1} << 31));
    expect("state limit rejects empty", !stateSizeAcceptable(0));

    // WF-1 peaks.
    {
        std::vector<float> x(1000);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float>(i) / 1000.0f;
        x[500] = std::nanf("");
        const float* ch[1] = {x.data()};
        std::vector<float> lo, hi;
        expect("WF-1 columns", computePeaks(ch, 1, 1000, 10, lo, hi) == 10 && lo.size() == 10 && hi.size() == 10);
        expect("WF-1 exact min/max", lo[0] == 0.0f && std::abs(hi[0] - 0.099f) < 1e-6f && std::abs(lo[9] - 0.9f) < 1e-6f
                                         && std::abs(hi[9] - 0.999f) < 1e-6f);
        expect("WF-1 nan counted as 0", lo[5] == 0.0f);
        expect("WF-1 columns clamp to samples", computePeaks(ch, 1, 7, 100, lo, hi) == 7);
        expect("WF-1 invalid", computePeaks(ch, 1, 0, 10, lo, hi) == 0 && computePeaks(nullptr, 1, 10, 10, lo, hi) == 0
                                   && computePeaks(ch, 1, 10, 0, lo, hi) == 0 && lo.empty());
        // Every sample belongs to exactly one column: a single spike is the max of exactly one column.
        std::vector<float> s(997, 0.0f);
        s[613] = 1.0f;
        const float* sc[1] = {s.data()};
        computePeaks(sc, 1, 997, 64, lo, hi);
        int spikes = 0;
        for (float v : hi)
            spikes += v == 1.0f;
        expect("WF-1 partition covers each sample once", spikes == 1);
        std::vector<float> m(100, 0.25f), q(100, -0.5f);
        const float* st[2] = {m.data(), q.data()};
        computePeaks(st, 2, 100, 4, lo, hi);
        expect("WF-1 channels combined by extremes", lo[0] == -0.5f && hi[0] == 0.25f);
    }
    // WF-2 grid and playhead.
    {
        auto g = computeBeatGrid(8.0, 0.0, 4);
        expect("WF-2 8 beats from 0 in 4/4", g.size() == 8 && g[0].bar && !g[1].bar && g[4].bar && std::abs(g[4].fraction - 0.5) < 1e-12);
        g = computeBeatGrid(8.0, 2.0, 4);
        expect("WF-2 origin 2: first bar line halfway to beat 4", g.size() == 8 && !g[0].bar && g[2].bar
                                                                   && std::abs(g[2].fraction - 0.25) < 1e-12);
        g = computeBeatGrid(6.0, 0.5, 3);
        expect("WF-2 fractional origin: lines start half a beat in", g.size() == 6 && std::abs(g[0].fraction - 0.5 / 6.0) < 1e-12
                                                                      && !g[0].bar && g[2].bar);
        g = computeBeatGrid(6.0, 0.0, 3);
        expect("WF-2 3/4 bars", g.size() == 6 && g[0].bar && g[3].bar && !g[1].bar);
        g = computeBeatGrid(8.0, 0.0, 6);
        expect("WF-2 meter change moves bars", g[0].bar && g[6].bar && !g[4].bar);
        expect("WF-2 invalid", computeBeatGrid(0.0, 0.0, 4).empty() && computeBeatGrid(8.0, 0.0, 0).empty()
                                   && computeBeatGrid(std::nan(""), 0.0, 4).empty());
        expect("WF-2 negative origin", computeBeatGrid(4.0, -3.0, 4).size() == 4);
        expect("WF-2 play fraction", std::abs(loopPlayFraction(5.0, 1.0, 8.0) - 0.5) < 1e-12
                                         && std::abs(loopPlayFraction(-1.0, 0.0, 8.0) - 0.875) < 1e-12
                                         && loopPlayFraction(std::nan(""), 0.0, 8.0) == 0.0
                                         && loopPlayFraction(9.0, 1.0, 8.0) == 0.0);
    }

    if (failures == 0)
        std::cout << "LoopBlobTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
