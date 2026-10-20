#include "core/control/GestureLane.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace groovemorph::core::control
{
namespace
{
int binOf(double fraction) noexcept
{
    if (!std::isfinite(fraction))
        return 0;
    double f = fraction - std::floor(fraction);
    return std::clamp(static_cast<int>(f * GestureLane::binCount), 0, GestureLane::binCount - 1);
}

bool parseFixedDecimal(std::string_view text, double& value) noexcept
{
    if (text.empty())
        return false;
    std::size_t pos = 0;
    bool negative = false;
    if (text[pos] == '-')
    {
        negative = true;
        if (++pos == text.size())
            return false;
    }

    double parsed = 0.0;
    bool hasDigit = false;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
    {
        hasDigit = true;
        parsed = parsed * 10.0 + static_cast<double>(text[pos++] - '0');
        if (!std::isfinite(parsed))
            return false;
    }
    if (pos < text.size() && text[pos] == '.')
    {
        ++pos;
        double place = 0.1;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
        {
            hasDigit = true;
            parsed += static_cast<double>(text[pos++] - '0') * place;
            place *= 0.1;
        }
    }
    if (!hasDigit || pos != text.size())
        return false;
    value = negative ? -parsed : parsed;
    return std::isfinite(value);
}
} // namespace

void GestureLane::clear() noexcept
{
    values.fill(0.0f);
    has.fill(false);
    filled = 0;
}

void GestureLane::record(double phraseFraction, float value) noexcept
{
    const int bin = binOf(phraseFraction);
    if (!has[static_cast<std::size_t>(bin)])
    {
        has[static_cast<std::size_t>(bin)] = true;
        ++filled;
    }
    values[static_cast<std::size_t>(bin)] = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

std::string GestureLane::serialize() const
{
    std::string out;
    char buffer[48];
    for (int bin = 0; bin < binCount; ++bin)
    {
        if (!has[static_cast<std::size_t>(bin)])
            continue;
        // Integer to_chars is available across the supported C++20 standard libraries.
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), bin);
        if (r.ec != std::errc{})
            continue;
        *r.ptr++ = ':';
        const auto scaled = static_cast<unsigned>(std::lround(values[static_cast<std::size_t>(bin)] * 100000.0f));
        *r.ptr++ = static_cast<char>('0' + scaled / 100000u);
        *r.ptr++ = '.';
        auto fraction = scaled % 100000u;
        for (int digit = 0; digit < 5; ++digit)
        {
            const unsigned divisor = digit == 0 ? 10000u : (digit == 1 ? 1000u : (digit == 2 ? 100u : (digit == 3 ? 10u : 1u)));
            *r.ptr++ = static_cast<char>('0' + (fraction / divisor) % 10u);
        }
        if (!out.empty())
            out += ' ';
        out.append(buffer, static_cast<std::size_t>(r.ptr - buffer));
    }
    return out;
}

std::size_t GestureLane::deserialize(std::string_view text) noexcept
{
    clear();
    if (text.size() > 16384)
        return 0;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
        const auto end = std::min(text.find(' ', pos), text.size());
        const auto item = text.substr(pos, end - pos);
        pos = end;
        const auto colon = item.find(':');
        if (colon == std::string_view::npos || colon == 0)
            continue;
        int bin = -1;
        const auto binResult = std::from_chars(item.data(), item.data() + colon, bin);
        if (binResult.ec != std::errc{} || binResult.ptr != item.data() + colon || bin < 0 || bin >= binCount)
            continue;
        const auto valueText = item.substr(colon + 1);
        double value = 0.0;
        if (!parseFixedDecimal(valueText, value))
            continue;
        record((static_cast<double>(bin) + 0.5) / binCount, static_cast<float>(value));
    }
    return static_cast<std::size_t>(filled);
}

float GestureLane::sample(double phraseFraction) const noexcept
{
    if (filled == 0)
        return -1.0f;
    const int bin = binOf(phraseFraction);
    // Previous and next recorded bins (wrapping), interpolated by distance.
    int prev = -1, next = -1, dPrev = 0, dNext = 0;
    for (int d = 0; d < binCount && prev < 0; ++d)
    {
        const int b = (bin - d + binCount) % binCount;
        if (has[static_cast<std::size_t>(b)]) { prev = b; dPrev = d; }
    }
    for (int d = 1; d <= binCount && next < 0; ++d)
    {
        const int b = (bin + d) % binCount;
        if (has[static_cast<std::size_t>(b)]) { next = b; dNext = d; }
    }
    if (prev < 0 || next < 0 || dPrev == 0)
        return values[static_cast<std::size_t>(prev < 0 ? next : prev)];
    const float a = values[static_cast<std::size_t>(prev)], b = values[static_cast<std::size_t>(next)];
    const float t = static_cast<float>(dPrev) / static_cast<float>(dPrev + dNext);
    return a + (b - a) * t;
}
} // namespace groovemorph::core::control
