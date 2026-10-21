#include "core/dsp/LoopBlob.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace groovemorph::core::LoopBlob
{
namespace
{
constexpr std::size_t headerBytes = 4 + 2 + 1 + 1 + 4 + 4 + 8 + 8 + 4; // up to and including payloadBytes
constexpr std::size_t crcBytes = 4;
constexpr int blockSamples = 4096;
constexpr int maxRiceK = 24;
constexpr std::uint64_t maxZigzag = 1u << 22; // residuals of a 17-bit side channel stay far below this

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(std::vector<std::uint8_t>& v, std::uint16_t x)
{
    v.push_back(static_cast<std::uint8_t>(x & 0xFFu));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFFu));
}
void putDouble(std::vector<std::uint8_t>& v, double d)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &d, sizeof bits);
    for (int i = 0; i < 8; ++i)
        v.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
}
std::uint32_t get32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
double getDouble(const std::uint8_t* p) noexcept
{
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i)
        bits |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    double d = 0.0;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

class BitWriter
{
public:
    void write(std::uint32_t value, int bits)
    {
        for (int i = bits - 1; i >= 0; --i)
            bit((value >> i) & 1u);
    }
    void unary(std::uint32_t q)
    {
        for (std::uint32_t i = 0; i < q; ++i)
            bit(1);
        bit(0);
    }
    std::vector<std::uint8_t> finish()
    {
        if (used != 0)
            bytes.push_back(static_cast<std::uint8_t>(current << (8 - used)));
        used = 0;
        return std::move(bytes);
    }

private:
    void bit(std::uint32_t b)
    {
        current = static_cast<std::uint8_t>((current << 1) | (b & 1u));
        if (++used == 8)
        {
            bytes.push_back(current);
            current = 0;
            used = 0;
        }
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t current = 0;
    int used = 0;
};

class BitReader
{
public:
    BitReader(const std::uint8_t* d, std::size_t size) : data(d), totalBits(size * 8) {}
    bool read(int bits, std::uint32_t& value)
    {
        if (pos + static_cast<std::size_t>(bits) > totalBits)
            return false;
        std::uint32_t v = 0;
        for (int i = 0; i < bits; ++i)
            v = (v << 1) | bitAt(pos++);
        value = v;
        return true;
    }
    bool unary(std::uint32_t limit, std::uint32_t& q)
    {
        std::uint32_t count = 0;
        for (;;)
        {
            if (pos >= totalBits)
                return false;
            if (bitAt(pos++) == 0)
                break;
            if (++count > limit)
                return false;
        }
        q = count;
        return true;
    }
    // True when the unread remainder is only zero padding of the last byte.
    [[nodiscard]] bool finishedCleanly() const
    {
        if ((pos + 7) / 8 != totalBits / 8)
            return false;
        for (std::size_t p = pos; p < totalBits; ++p)
            if (bitAt(p) != 0)
                return false;
        return true;
    }

private:
    [[nodiscard]] std::uint32_t bitAt(std::size_t p) const { return (data[p >> 3] >> (7 - (p & 7))) & 1u; }
    const std::uint8_t* data;
    std::size_t totalBits;
    std::size_t pos = 0;
};

inline std::int32_t predict(int order, std::int32_t x1, std::int32_t x2, std::int32_t x3) noexcept
{
    switch (order)
    {
        case 1: return x1;
        case 2: return 2 * x1 - x2;
        case 3: return 3 * x1 - 3 * x2 + x3;
        default: return 0;
    }
}
inline std::uint32_t zigzag(std::int32_t v) noexcept
{
    return (static_cast<std::uint32_t>(v) << 1) ^ static_cast<std::uint32_t>(v >> 31);
}
inline std::int32_t unzigzag(std::uint32_t u) noexcept
{
    return static_cast<std::int32_t>(u >> 1) ^ -static_cast<std::int32_t>(u & 1u);
}

void encodeChannel(const std::vector<std::int32_t>& x, BitWriter& writer)
{
    std::int32_t h1 = 0, h2 = 0, h3 = 0; // history carried across blocks
    std::vector<std::uint32_t> u[4];
    for (auto& v : u)
        v.resize(blockSamples);
    for (std::size_t start = 0; start < x.size(); start += blockSamples)
    {
        const std::size_t count = std::min<std::size_t>(blockSamples, x.size() - start);
        std::uint64_t bestBits = ~0ull;
        int bestOrder = 0;
        int bestK = 0;
        for (int order = 0; order < 4; ++order)
        {
            std::int32_t a = h1, b = h2, c = h3;
            std::uint64_t sum = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::int32_t s = x[start + i];
                u[order][i] = zigzag(s - predict(order, a, b, c));
                sum += u[order][i];
                c = b;
                b = a;
                a = s;
            }
            const double mean = static_cast<double>(sum) / static_cast<double>(count);
            int estimate = 0;
            while (estimate < maxRiceK && static_cast<double>(1u << estimate) < mean)
                ++estimate;
            for (int k = std::max(0, estimate - 2); k <= std::min(maxRiceK, estimate + 1); ++k)
            {
                std::uint64_t bits = 7;
                for (std::size_t i = 0; i < count; ++i)
                    bits += (u[order][i] >> k) + 1 + static_cast<std::uint64_t>(k);
                if (bits < bestBits)
                {
                    bestBits = bits;
                    bestOrder = order;
                    bestK = k;
                }
            }
        }
        writer.write(static_cast<std::uint32_t>(bestOrder), 2);
        writer.write(static_cast<std::uint32_t>(bestK), 5);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t v = u[bestOrder][i];
            writer.unary(v >> bestK);
            writer.write(v & ((1u << bestK) - 1u), bestK);
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            h3 = h2;
            h2 = h1;
            h1 = x[start + i];
        }
    }
}

bool decodeChannel(BitReader& reader, std::vector<std::int32_t>& x, std::int32_t limit)
{
    std::int32_t h1 = 0, h2 = 0, h3 = 0;
    for (std::size_t start = 0; start < x.size(); start += blockSamples)
    {
        const std::size_t count = std::min<std::size_t>(blockSamples, x.size() - start);
        std::uint32_t order = 0, k = 0;
        if (!reader.read(2, order) || !reader.read(5, k) || k > static_cast<std::uint32_t>(maxRiceK))
            return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            std::uint32_t q = 0, rem = 0;
            if (!reader.unary(static_cast<std::uint32_t>(maxZigzag >> k) + 1, q) || !reader.read(static_cast<int>(k), rem))
                return false;
            const std::uint64_t u = (static_cast<std::uint64_t>(q) << k) | rem;
            if (u > maxZigzag)
                return false;
            const std::int64_t value = static_cast<std::int64_t>(unzigzag(static_cast<std::uint32_t>(u)))
                                       + predict(static_cast<int>(order), h1, h2, h3);
            if (value < -limit || value > limit)
                return false;
            x[start + i] = static_cast<std::int32_t>(value);
            h3 = h2;
            h2 = h1;
            h1 = static_cast<std::int32_t>(value);
        }
    }
    return true;
}

std::int32_t quantise(float v) noexcept
{
    if (!std::isfinite(v))
        return 0;
    return static_cast<std::int32_t>(std::lrint(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
}
} // namespace

Status encode(const float* const* channels, int numChannels, std::int64_t numSamples, double sampleRate,
              double loopBeats, double originBeats, std::vector<std::uint8_t>& out)
{
    out.clear();
    if (channels == nullptr || numChannels < 1 || numChannels > 2 || channels[0] == nullptr
        || (numChannels == 2 && channels[1] == nullptr) || numSamples < 16 || !std::isfinite(sampleRate)
        || sampleRate < minSampleRate || sampleRate > maxSampleRate || !std::isfinite(loopBeats) || !(loopBeats > 0.0)
        || loopBeats > maxLoopBeats || !std::isfinite(originBeats) || std::abs(originBeats) > 1.0e9)
        return Status::Invalid;
    if (static_cast<double>(numSamples) > maxSeconds * std::round(sampleRate))
        return Status::TooLong;

    const auto n = static_cast<std::size_t>(numSamples);
    std::vector<std::int32_t> left(n), side;
    for (std::size_t i = 0; i < n; ++i)
        left[i] = quantise(channels[0][i]);
    bool stereo = false;
    if (numChannels == 2)
    {
        side.resize(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            side[i] = quantise(channels[1][i]) - left[i];
            stereo = stereo || side[i] != 0;
        }
    }
    BitWriter writer;
    encodeChannel(left, writer);
    if (stereo)
        encodeChannel(side, writer);
    const auto payload = writer.finish();

    out.reserve(headerBytes + payload.size() + crcBytes);
    out.push_back('G');
    out.push_back('M');
    out.push_back('L');
    out.push_back('B');
    put16(out, version);
    out.push_back(stereo ? 2 : 1);
    out.push_back(stereo ? 0 : 1);
    put32(out, static_cast<std::uint32_t>(std::llround(sampleRate)));
    put32(out, static_cast<std::uint32_t>(numSamples));
    putDouble(out, loopBeats);
    putDouble(out, originBeats);
    put32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put32(out, crc32(out.data(), out.size()));
    if (out.size() > maxEncodedBytes)
    {
        out.clear();
        return Status::TooLarge;
    }
    return Status::Ok;
}

Status decode(const std::uint8_t* data, std::size_t size, Audio& out)
{
    out = Audio{};
    if (data == nullptr || size < headerBytes + crcBytes || size > maxEncodedBytes + headerBytes + crcBytes)
        return Status::Corrupt;
    if (std::memcmp(data, "GMLB", 4) != 0)
        return Status::Corrupt;
    const unsigned fileVersion = static_cast<unsigned>(data[4]) | (static_cast<unsigned>(data[5]) << 8);
    if (fileVersion != version)
        return Status::Corrupt;
    const int stored = data[6];
    const int flags = data[7];
    if (stored < 1 || stored > 2 || (stored == 2 && flags != 0) || (stored == 1 && flags != 1))
        return Status::Corrupt;
    const std::uint32_t rate = get32(data + 8);
    const std::uint32_t samples = get32(data + 12);
    const double beats = getDouble(data + 16);
    const double origin = getDouble(data + 24);
    const std::uint32_t payloadBytes = get32(data + 32);
    if (rate < minSampleRate || rate > maxSampleRate || samples < 16
        || static_cast<double>(samples) > maxSeconds * static_cast<double>(rate) || !std::isfinite(beats)
        || !(beats > 0.0) || beats > maxLoopBeats || !std::isfinite(origin) || std::abs(origin) > 1.0e9)
        return Status::Corrupt;
    if (static_cast<std::size_t>(payloadBytes) != size - headerBytes - crcBytes)
        return Status::Corrupt; // truncated or trailing bytes
    if (crc32(data, size - crcBytes) != get32(data + size - crcBytes))
        return Status::Corrupt;
    // Every sample costs at least one bit: reject sample counts the payload cannot hold before allocating.
    if (static_cast<std::uint64_t>(samples) * static_cast<std::uint64_t>(stored) > static_cast<std::uint64_t>(payloadBytes) * 8u)
        return Status::Corrupt;

    BitReader reader(data + headerBytes, payloadBytes);
    std::vector<std::int32_t> left(samples), side;
    if (!decodeChannel(reader, left, 32767))
        return Status::Corrupt;
    if (stored == 2)
    {
        side.resize(samples);
        if (!decodeChannel(reader, side, 65534))
            return Status::Corrupt;
    }
    if (!reader.finishedCleanly())
        return Status::Corrupt;

    out.sampleRate = static_cast<double>(rate);
    out.loopBeats = beats;
    out.originBeats = origin;
    out.left.resize(samples);
    out.right.resize(samples);
    for (std::size_t i = 0; i < samples; ++i)
    {
        out.left[i] = static_cast<float>(left[i]) / 32767.0f;
        if (stored == 2)
        {
            const std::int32_t r = left[i] + side[i];
            if (r < -32767 || r > 32767)
            {
                out = Audio{};
                return Status::Corrupt;
            }
            out.right[i] = static_cast<float>(r) / 32767.0f;
        }
        else
            out.right[i] = out.left[i];
    }
    return Status::Ok;
}
} // namespace groovemorph::core::LoopBlob
