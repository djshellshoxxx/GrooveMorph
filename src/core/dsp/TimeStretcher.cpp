#include "core/dsp/TimeStretcher.h"

#include <algorithm>
#include <cmath>

namespace groovemorph::core
{
namespace TimeStretcher
{
std::int64_t outputLength(std::int64_t inputSamples, double ratio) noexcept
{
    return static_cast<std::int64_t>(std::llround(static_cast<double>(inputSamples) * ratio));
}

namespace
{
constexpr double pi = 3.14159265358979323846;

// Normalized similarity of cand[0..len) against tmpl[0..len) with a stride. Returns dot and candidate energy.
inline void correlate(const float* cand, const float* tmpl, std::int64_t len, std::int64_t stride, double& dot,
                      double& energy) noexcept
{
    double d = 0.0;
    double e = 0.0;
    for (std::int64_t i = 0; i < len; i += stride)
    {
        d += static_cast<double>(cand[i]) * static_cast<double>(tmpl[i]);
        e += static_cast<double>(cand[i]) * static_cast<double>(cand[i]);
    }
    dot = d;
    energy = e;
}
} // namespace

bool stretch(const float* const* in, int numChannels, std::int64_t numSamples, const StretchOptions& options,
             std::vector<std::vector<float>>& out, const std::atomic<bool>* cancel)
{
    const double ratio = options.ratio;
    if (in == nullptr || numChannels < 1 || numChannels > 8 || numSamples < 16 || !std::isfinite(ratio)
        || ratio < minRatio || ratio > maxRatio || !(options.sampleRate >= 1000.0) || !std::isfinite(options.sampleRate))
        return false;
    for (int c = 0; c < numChannels; ++c)
        if (in[c] == nullptr)
            return false;
    const std::int64_t n = numSamples;
    const std::int64_t m = outputLength(n, ratio);
    if (m < 1)
        return false;

    out.assign(static_cast<std::size_t>(numChannels), std::vector<float>());
    if (std::abs(ratio - 1.0) < 1.0e-12)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            auto& o = out[static_cast<std::size_t>(c)];
            o.resize(static_cast<std::size_t>(n));
            for (std::int64_t i = 0; i < n; ++i)
                o[static_cast<std::size_t>(i)] = std::isfinite(in[c][i]) ? in[c][i] : 0.0f;
        }
        return true;
    }

    const std::int64_t Hs = std::max<std::int64_t>(64, std::llround(0.020 * options.sampleRate));
    const std::int64_t N = 2 * Hs;
    const std::int64_t tol = Hs / 2;
    const std::int64_t D = std::max<std::int64_t>(1, std::llround(options.sampleRate / 8000.0));
    std::int64_t pad = 3 * N;
    pad = (pad + D - 1) / D * D; // keeps padded coordinates aligned with the decimated mix
    const std::int64_t paddedLength = n + 2 * pad;

    // Padded copies: zeros around the signal, or the signal wrapped for a loop.
    std::vector<std::vector<float>> padded(static_cast<std::size_t>(numChannels));
    std::vector<float> mix(static_cast<std::size_t>(paddedLength), 0.0f);
    for (int c = 0; c < numChannels; ++c)
    {
        auto& p = padded[static_cast<std::size_t>(c)];
        p.assign(static_cast<std::size_t>(paddedLength), 0.0f);
        for (std::int64_t i = 0; i < paddedLength; ++i)
        {
            std::int64_t src = i - pad;
            if (options.circular)
            {
                src %= n;
                if (src < 0)
                    src += n;
            }
            else if (src < 0 || src >= n)
                continue;
            const float v = in[c][src];
            p[static_cast<std::size_t>(i)] = std::isfinite(v) ? v : 0.0f;
        }
        for (std::int64_t i = 0; i < paddedLength; ++i)
            mix[static_cast<std::size_t>(i)] += p[static_cast<std::size_t>(i)] / static_cast<float>(numChannels);
    }
    const std::int64_t decLength = paddedLength / D;
    std::vector<float> dec(static_cast<std::size_t>(decLength), 0.0f);
    for (std::int64_t j = 0; j < decLength; ++j)
    {
        double s = 0.0;
        for (std::int64_t k = 0; k < D; ++k)
            s += mix[static_cast<std::size_t>(j * D + k)];
        dec[static_cast<std::size_t>(j)] = static_cast<float>(s / static_cast<double>(D));
    }

    std::vector<float> window(static_cast<std::size_t>(N));
    for (std::int64_t i = 0; i < N; ++i)
        window[static_cast<std::size_t>(i)] =
            static_cast<float>(0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i) / static_cast<double>(N)));

    const std::int64_t X = options.circular ? std::min(Hs, m) : 0; // extra samples for the seam cross-fade
    const std::int64_t total = m + X;
    std::vector<std::vector<float>> acc(static_cast<std::size_t>(numChannels),
                                        std::vector<float>(static_cast<std::size_t>(total), 0.0f));
    const std::int64_t frames = (total - 1) / Hs + 1; // frame k covers output [(k-1)Hs, (k+1)Hs)
    const double Ha = static_cast<double>(Hs) / ratio;
    const std::int64_t Lh = std::max<std::int64_t>(1, Hs / D);
    const std::int64_t searchD = tol / D + 1;
    const std::int64_t sMinPad = 0;
    const std::int64_t sMaxPad = paddedLength - N;

    std::int64_t prevStartPad = pad - Hs; // frame 0: input [-Hs, Hs)
    for (std::int64_t k = 0; k <= frames; ++k)
    {
        if (cancel != nullptr && (k & 7) == 0 && cancel->load(std::memory_order_relaxed))
            return false;
        std::int64_t startPad = prevStartPad;
        if (k > 0)
        {
            const std::int64_t nominalPad = pad + std::llround(static_cast<double>(k) * Ha) - Hs;
            const std::int64_t tmplPad = std::clamp<std::int64_t>(prevStartPad + Hs, 0, paddedLength - Hs);
            // Coarse search on the decimated mix.
            const std::int64_t tq = std::min(tmplPad / D, decLength - Lh);
            double tmplEnergy = 0.0;
            for (std::int64_t i = 0; i < Lh; ++i)
                tmplEnergy += static_cast<double>(dec[static_cast<std::size_t>(tq + i)]) * dec[static_cast<std::size_t>(tq + i)];
            const std::int64_t qn = nominalPad / D;
            double best = -1.0e300;
            std::int64_t bestQ = std::clamp<std::int64_t>(qn, 0, std::max<std::int64_t>(0, decLength - Lh - 1));
            for (std::int64_t q = qn - searchD; q <= qn + searchD; ++q)
            {
                if (q < 0 || q + Lh > decLength || (q * D) + N > paddedLength)
                    continue;
                double dot = 0.0;
                double energy = 0.0;
                correlate(dec.data() + q, dec.data() + tq, Lh, 1, dot, energy);
                const double score = dot / std::sqrt(energy * tmplEnergy + 1.0e-12)
                                     - 1.0e-3 * static_cast<double>(std::abs(q - qn)) / static_cast<double>(searchD);
                if (score > best)
                {
                    best = score;
                    bestQ = q;
                }
            }
            // Full-rate refinement around the coarse winner.
            std::int64_t bestC = bestQ * D;
            if (D > 1)
            {
                double tE = 0.0;
                for (std::int64_t i = 0; i < Hs; i += 2)
                    tE += static_cast<double>(mix[static_cast<std::size_t>(tmplPad + i)]) * mix[static_cast<std::size_t>(tmplPad + i)];
                double refineBest = -1.0e300;
                for (std::int64_t c = bestQ * D - D; c <= bestQ * D + D; ++c)
                {
                    if (c < sMinPad || c > sMaxPad)
                        continue;
                    double dot = 0.0;
                    double energy = 0.0;
                    correlate(mix.data() + c, mix.data() + tmplPad, Hs, 2, dot, energy);
                    const double score = dot / std::sqrt(energy * tE + 1.0e-12)
                                         - 1.0e-3 * static_cast<double>(std::abs(c - nominalPad)) / static_cast<double>(tol);
                    if (score > refineBest)
                    {
                        refineBest = score;
                        bestC = c;
                    }
                }
            }
            startPad = std::clamp(bestC, sMinPad, sMaxPad);
        }
        prevStartPad = startPad;

        const std::int64_t outStart = (k - 1) * Hs;
        const std::int64_t i0 = std::max<std::int64_t>(0, -outStart);
        const std::int64_t i1 = std::min<std::int64_t>(N, total - outStart);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = padded[static_cast<std::size_t>(c)].data() + startPad;
            float* dst = acc[static_cast<std::size_t>(c)].data() + outStart;
            for (std::int64_t i = i0; i < i1; ++i)
                dst[i] += window[static_cast<std::size_t>(i)] * src[i];
        }
    }

    for (int c = 0; c < numChannels; ++c)
    {
        auto& o = out[static_cast<std::size_t>(c)];
        auto& a = acc[static_cast<std::size_t>(c)];
        o.assign(a.begin(), a.begin() + m);
        // Loop seam: the stream continues past the end; fade from that continuation into the head.
        for (std::int64_t i = 0; i < X; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(pi * (static_cast<double>(i) + 0.5) / static_cast<double>(X));
            o[static_cast<std::size_t>(i)] = static_cast<float>(w * a[static_cast<std::size_t>(i)]
                                                               + (1.0 - w) * a[static_cast<std::size_t>(m + i)]);
        }
        for (auto& v : o)
            if (!std::isfinite(v))
                v = 0.0f;
    }
    return true;
}
} // namespace TimeStretcher

StretchPlan planStretch(double nativeBpm, double hostBpm, double ratioInUse, double maxRatioForCapacity) noexcept
{
    StretchPlan plan;
    if (!(nativeBpm > 0.0) || !(hostBpm > 0.0) || !std::isfinite(nativeBpm) || !std::isfinite(hostBpm))
        return plan;
    const double inUse = ratioInUse > 0.0 && std::isfinite(ratioInUse) ? ratioInUse : 1.0;
    const double wanted = nativeBpm / hostBpm;
    double ratio = std::clamp(wanted, TimeStretcher::minRatio, TimeStretcher::maxRatio);
    if (maxRatioForCapacity > 0.0 && std::isfinite(maxRatioForCapacity))
        ratio = std::min(ratio, maxRatioForCapacity);
    ratio = std::max(ratio, TimeStretcher::minRatio);
    plan.ratio = ratio;
    plan.limited = std::abs(ratio - wanted) > 1.0e-6 * wanted;
    plan.needsNew = std::abs(ratio - inUse) / inUse > 0.002;
    return plan;
}

StretchWorker::StretchWorker() : thread(&StretchWorker::run, this) {}

StretchWorker::~StretchWorker()
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        cancelRunning.store(true, std::memory_order_relaxed);
    }
    wake.notify_all();
    if (thread.joinable())
        thread.join();
}

void StretchWorker::submit(Job&& job)
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        pending = std::move(job);
        hasPending = true;
        hasResult = false;
        if (running)
            cancelRunning.store(true, std::memory_order_relaxed);
    }
    wake.notify_one();
}

void StretchWorker::cancelAll()
{
    const std::lock_guard<std::mutex> lock(mutex);
    hasPending = false;
    hasResult = false;
    pending = Job{};
    if (running)
        cancelRunning.store(true, std::memory_order_relaxed);
}

bool StretchWorker::tryTake(Result& result)
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (!hasResult)
        return false;
    result = std::move(finished);
    hasResult = false;
    return true;
}

bool StretchWorker::busy() const
{
    const std::lock_guard<std::mutex> lock(mutex);
    return running || hasPending;
}

void StretchWorker::run()
{
    std::unique_lock<std::mutex> lock(mutex);
    for (;;)
    {
        wake.wait(lock, [this] { return stopping || hasPending; });
        if (stopping)
            return;
        Job job = std::move(pending);
        pending = Job{};
        hasPending = false;
        running = true;
        cancelRunning.store(false, std::memory_order_relaxed);
        lock.unlock();

        Result r;
        r.id = job.id;
        r.ratio = job.options.ratio;
        std::vector<const float*> pointers;
        std::int64_t samples = job.input.empty() ? 0 : static_cast<std::int64_t>(job.input[0].size());
        bool consistent = !job.input.empty();
        for (const auto& channel : job.input)
        {
            pointers.push_back(channel.data());
            consistent = consistent && static_cast<std::int64_t>(channel.size()) == samples;
        }
        r.ok = consistent
               && TimeStretcher::stretch(pointers.data(), static_cast<int>(pointers.size()), samples, job.options,
                                         r.output, &cancelRunning);

        lock.lock();
        running = false;
        if (!cancelRunning.load(std::memory_order_relaxed) && !stopping)
        {
            finished = std::move(r);
            hasResult = true;
        }
    }
}
} // namespace groovemorph::core
