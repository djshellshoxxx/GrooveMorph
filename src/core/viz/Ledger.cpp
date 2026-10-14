#include "core/viz/Ledger.h"

#include "core/OperationParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace groovemorph::core::viz
{
namespace
{
std::string fmt(const char* format, double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

std::string fmt2(const char* format, double a, double b)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, format, a, b);
    return buf;
}

const char* timingSourceName(TimingSource s)
{
    switch (s)
    {
        case TimingSource::Host: return "Host";
        case TimingSource::Mixed: return "Host + manual";
        case TimingSource::Manual: return "Manual";
        case TimingSource::FreeRun: return "Sample clock";
    }
    return "Unknown";
}
} // namespace

std::string formatDb(double g)
{
    if (!(g > 1.0e-4))
        return "-inf dB";
    return fmt("%.1f dB", 20.0 * std::log10(g));
}

std::string sceneName(int index)
{
    return index >= 0 && index < 4 ? std::string(1, static_cast<char>('A' + index)) : "?";
}

std::string missingFieldsText(std::uint32_t mask)
{
    struct Item { std::uint32_t bit; const char* name; };
    static constexpr Item items[] = {{MissingField::tempo, "tempo"},       {MissingField::meter, "meter"},
                                     {MissingField::playState, "play state"}, {MissingField::position, "position"},
                                     {MissingField::ppq, "PPQ"},           {MissingField::barAnchor, "bar anchor"}};
    std::string s;
    for (const auto& it : items)
        if (mask & it.bit)
            s += (s.empty() ? "" : ", ") + std::string(it.name);
    return s;
}

Ledger buildLedger(const FrameWindow& w, const StatusSnapshot& st)
{
    Ledger L;
    const bool hasFrames = w.frames != nullptr && w.count > 0;
    const trace::TraceFrame* last = hasFrames ? &w.frames[w.count - 1] : nullptr;

    // Banner: the most important honest state first.
    if (st.previewing)
    {
        L.banner = BannerKind::Preview;
        L.bannerText = "PREVIEW: not the committed scene";
    }
    else if (!st.timingValid)
    {
        L.banner = BannerKind::Invalid;
        const std::string why = missingFieldsText(st.missingFields);
        L.bannerText = "Timing invalid: effects bypassed" + (why.empty() ? std::string() : " (missing " + why + ")");
    }
    else if (!st.transportPlaying)
    {
        L.banner = BannerKind::Idle;
        L.bannerText = "Bypassed: transport stopped";
    }
    else if (!hasFrames)
    {
        L.banner = BannerKind::NoData;
        L.bannerText = "No trace data yet";
    }

    const bool timed = last != nullptr && (last->flags & trace::TimingOk) != 0 && last->samplesPerBeat > 0.0f;
    const double spb = timed ? static_cast<double>(last->samplesPerBeat) : 0.0;

    // Hearing audio from.
    {
        LedgerRow r{"Hearing audio from", "-", ""};
        if (timed)
        {
            const ResolvedThread t = resolveThread(w, static_cast<double>(last->sample));
            if (t.noData)
            {
                r.value = "unknown";
                r.note = "no history recorded yet";
            }
            else
            {
                const double lag = static_cast<double>(last->sample) - t.sourceSample;
                if (lag < 1.0)
                {
                    r.value = "now (live)";
                    r.note = "no time displacement";
                }
                else
                {
                    r.value = fmt("%.2f beats ago", lag / spb);
                    r.note = fmt("%.0f ms", lag / st.sampleRate * 1000.0);
                }
            }
        }
        else
        {
            r.value = "bypassed";
        }
        L.rows.push_back(r);
    }

    // Reading speed: the stage that moves the sound most visibly (TapeStop > HalfTime > Reverse > Repeat).
    {
        LedgerRow r{"Reading speed", "1x", "live speed"};
        if (timed)
        {
            struct Stage { OperationType op; int slot; const char* name; };
            static constexpr Stage order[] = {{OperationType::TapeStop, 3, "tape-stop"},
                                              {OperationType::HalfTime, 2, "half-time"},
                                              {OperationType::ReverseSlice, 1, "reverse"},
                                              {OperationType::Repeat, 0, "repeat"}};
            for (const auto& s : order)
            {
                const float a = last->amount[static_cast<std::size_t>(s.op)];
                const float rate = last->rate[s.slot];
                if (a >= 0.5f && (last->lagSamples[s.slot] > 0.5f || rate != 1.0f))
                {
                    r.value = fmt("%.2fx", static_cast<double>(rate));
                    r.note = rate < 0.0f ? std::string("backwards (") + s.name + ")" : std::string(s.name);
                    break;
                }
            }
        }
        else
        {
            r = {"Reading speed", "-", "bypassed"};
        }
        L.rows.push_back(r);
    }

    // Gate.
    {
        LedgerRow r{"Gate", "off", ""};
        if (timed && last->amount[static_cast<std::size_t>(OperationType::Gate)] >= 0.5f)
        {
            std::size_t closed = 0, counted = 0;
            const double barSamples = static_cast<double>(std::max(1, st.meterNumerator)) * spb;
            for (std::size_t i = w.count; i-- > 0;)
            {
                if (static_cast<double>(last->sample - w.frames[i].sample) > barSamples)
                    break;
                ++counted;
                if (w.frames[i].gateGain < 0.25f)
                    ++closed;
            }
            r.value = formatDb(static_cast<double>(1.0f - last->amount[2] + last->amount[2] * last->gateGain));
            r.note = counted ? fmt("closed %.0f %% of the last bar", 100.0 * static_cast<double>(closed) / static_cast<double>(counted)) : "";
        }
        L.rows.push_back(r);
    }

    // Wet / intensity.
    {
        LedgerRow r{"Wet / intensity", "-", ""};
        if (hasFrames)
        {
            float maxAmount = 0.0f;
            for (float a : last->amount)
                maxAmount = std::max(maxAmount, a);
            r.value = fmt2("%.0f %% / %.0f %%", 100.0 * static_cast<double>(maxAmount), 100.0 * static_cast<double>(last->intensity));
            r.note = last->intensity < 1.0f ? "dry path mixed in" : "dry path untouched";
        }
        L.rows.push_back(r);
    }

    // Level in / out: peak levels over the last 100 ms. Not a gain figure: time-displacement effects
    // legitimately play loud past audio while the current input is quiet.
    {
        LedgerRow r{"Level in / out", "-", "peak, last 100 ms"};
        if (hasFrames)
        {
            float inMax = 0.0f, outMax = 0.0f;
            const double window = 0.1 * st.sampleRate;
            for (std::size_t i = w.count; i-- > 0;)
            {
                if (static_cast<double>(last->sample - w.frames[i].sample) > window)
                    break;
                inMax = std::max(inMax, w.frames[i].inPeak);
                outMax = std::max(outMax, w.frames[i].outPeak);
            }
            r.value = formatDb(static_cast<double>(inMax)) + " / " + formatDb(static_cast<double>(outMax));
        }
        L.rows.push_back(r);
    }

    // History fill and tail.
    {
        const double seconds = hasFrames ? std::min(st.historySeconds, static_cast<double>(last->sample + 1) / st.sampleRate) : 0.0;
        LedgerRow r{"History", fmt2("%.1f / %.1f s", seconds, st.historySeconds), seconds >= st.historySeconds ? "full" : "filling"};
        L.rows.push_back(r);
        L.rows.push_back({"Reported tail", fmt("%.1f s", st.tailSeconds), "host sees it"});
    }

    // Timing and position.
    {
        std::string v = timingSourceName(st.timingSource);
        v += st.timingValid ? " (valid)" : " (invalid)";
        L.rows.push_back({"Timing", v, st.timingValid ? "" : missingFieldsText(st.missingFields)});
        L.rows.push_back({"Position", fmt2("bar %.0f of %.0f", std::floor(st.barInPhrase) + 1.0, static_cast<double>(st.phraseBars)),
                          fmt("beat %.1f", st.beatInBar + 1.0)});
    }

    // Scene.
    {
        LedgerRow r{"Scene", sceneName(st.activeScene) + " sounding", ""};
        if (st.previewing)
            r.note = "preview";
        else if (st.queuedScene >= 0)
        {
            r.note = "-> " + sceneName(st.queuedScene) + " queued";
            if (st.samplesUntilSwitch >= 0 && spb > 0.0)
                r.note += fmt(" in %.1f beats", static_cast<double>(st.samplesUntilSwitch) / spb);
        }
        L.rows.push_back(r);
    }
    return L;
}

std::string ledgerAsText(const Ledger& ledger)
{
    std::string s;
    if (!ledger.bannerText.empty())
        s += ledger.bannerText + "\n";
    for (const auto& r : ledger.rows)
    {
        s += r.label + ": " + r.value;
        if (!r.note.empty())
            s += " (" + r.note + ")";
        s += "\n";
    }
    return s;
}
} // namespace groovemorph::core::viz
