#include "core/dsp/PrintNaming.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace groovemorph::core::printnaming
{
namespace
{
bool isAlnum(unsigned char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool isReservedDeviceName(const std::string& s)
{
    std::string upper = s;
    for (auto& c : upper)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
        return true;
    if (upper.size() == 4 && (upper.compare(0, 3, "COM") == 0 || upper.compare(0, 3, "LPT") == 0))
        return upper[3] >= '1' && upper[3] <= '9';
    return false;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }
bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
} // namespace

std::string sanitizeComponent(std::string_view text, std::size_t maxLength)
{
    std::string out;
    for (const char raw : text)
    {
        const auto c = static_cast<unsigned char>(raw);
        char mapped = 0;
        if (isAlnum(c) || c == '-')
            mapped = raw;
        else if (c == ' ' || c == '_')
            mapped = '-';
        else
            continue; // separators, dots, control and non-ASCII bytes are dropped
        if (mapped == '-' && (out.empty() || out.back() == '-'))
            continue; // no leading '-' and no runs
        out.push_back(mapped);
    }
    if (out.size() > maxLength)
        out.resize(maxLength);
    while (!out.empty() && out.back() == '-')
        out.pop_back();
    if (out.empty())
        out = "Scene";
    if (isReservedDeviceName(out))
        out.push_back('-');
    return out;
}

std::string formatBpm(double bpm)
{
    if (!std::isfinite(bpm) || bpm <= 0.0 || bpm > 100000.0)
        return "0";
    const long long tenths = std::llround(bpm * 10.0);
    if (tenths <= 0)
        return "0";
    std::string out = std::to_string(tenths / 10);
    if (tenths % 10 != 0)
    {
        out.push_back('.');
        out.push_back(static_cast<char>('0' + tenths % 10));
    }
    return out;
}

std::string formatTimestamp(const Timestamp& ts)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d", std::clamp(ts.year, 0, 9999),
                  std::clamp(ts.month, 0, 99), std::clamp(ts.day, 0, 99), std::clamp(ts.hour, 0, 99),
                  std::clamp(ts.minute, 0, 99), std::clamp(ts.second, 0, 99));
    return buffer;
}

std::string buildPrintFileName(std::string_view scene, double bpm, int bars, const Timestamp& ts)
{
    return "GrooveMorph_" + sanitizeComponent(scene) + "_" + formatBpm(bpm) + "bpm_" + std::to_string(std::max(0, bars))
           + "bars_" + formatTimestamp(ts) + ".wav";
}

std::string withCollisionSuffix(std::string_view name, int n)
{
    if (n < 2)
        return std::string(name);
    constexpr std::string_view ext = ".wav";
    const auto stem = endsWith(name, ext) ? name.substr(0, name.size() - ext.size()) : name;
    return std::string(stem) + "-" + std::to_string(n) + (endsWith(name, ext) ? std::string(ext) : std::string());
}

bool isPrintFileName(std::string_view name)
{
    return startsWith(name, "GrooveMorph_") && endsWith(name, ".wav") && name.size() > 16;
}

ParsedName parsePrintFileName(std::string_view name)
{
    ParsedName result;
    if (!isPrintFileName(name))
        return result;
    std::string_view rest = name.substr(12, name.size() - 12 - 4); // between "GrooveMorph_" and ".wav"
    // <scene>_<bpm>bpm_<bars>bars_<timestamp>[-n]
    const auto lastUnderscore = rest.rfind('_');
    if (lastUnderscore == std::string_view::npos)
        return result;
    rest = rest.substr(0, lastUnderscore); // drop the timestamp
    const auto barsUnderscore = rest.rfind('_');
    if (barsUnderscore == std::string_view::npos)
        return result;
    std::string_view barsText = rest.substr(barsUnderscore + 1);
    rest = rest.substr(0, barsUnderscore);
    const auto bpmUnderscore = rest.rfind('_');
    if (bpmUnderscore == std::string_view::npos)
        return result;
    std::string_view bpmText = rest.substr(bpmUnderscore + 1);
    const std::string_view scene = rest.substr(0, bpmUnderscore);
    if (!endsWith(barsText, "bars") || !endsWith(bpmText, "bpm") || scene.empty())
        return result;
    barsText.remove_suffix(4);
    bpmText.remove_suffix(3);
    int bars = 0;
    const auto barsEnd = barsText.data() + barsText.size();
    if (barsText.empty() || std::from_chars(barsText.data(), barsEnd, bars).ptr != barsEnd || bars < 1 || bars > 16)
        return result;
    // Manual parse (digits with at most one '.'): floating-point from_chars is missing on some Apple toolchains.
    double bpm = 0.0, scale = 0.0;
    for (const char c : bpmText)
    {
        if (c == '.' && scale == 0.0)
            scale = 1.0;
        else if (c >= '0' && c <= '9')
        {
            if (scale == 0.0)
                bpm = bpm * 10.0 + (c - '0');
            else
            {
                scale *= 10.0;
                bpm += (c - '0') / scale;
            }
        }
        else
            return result;
    }
    if (bpmText.empty() || !(bpm > 0.0))
        return result;
    result.scene = std::string(scene);
    result.bpm = bpm;
    result.bars = bars;
    result.valid = true;
    return result;
}
} // namespace groovemorph::core::printnaming
