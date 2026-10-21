#include "core/preset/Preset.h"

#include "core/scene/SceneSerializer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <locale>
#include <sstream>
#include <iomanip>

namespace groovemorph::core::preset
{
namespace
{
constexpr std::array<std::string_view, 8> allowedParams{
    "masterIntensity", "dryWet", "morphTarget", "morphAmount", "bandMode", "bandCrossover", "bandSlope", "bandTopology"};

std::string_view trimSpaces(std::string_view s) noexcept
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

bool parseFloatStrict(std::string_view text, float& out) noexcept
{
    text = trimSpaces(text);
    if (text.empty() || text.size() > 32)
        return false;
    // Locale-independent: reuse the scene parser by round-tripping through a stream is overkill;
    // the grammar here is a plain decimal with optional exponent.
    try
    {
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        double v = 0.0;
        in >> v;
        if (in.fail() || !in.eof() || !std::isfinite(v))
            return false;
        out = static_cast<float>(v);
        return std::isfinite(out);
    }
    catch (...)
    {
        return false;
    }
}

std::string formatFloat(float v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << v;
    return out.str();
}

bool isReservedWindowsName(std::string_view stem)
{
    const auto lower = toLower(stem);
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul")
        return true;
    return lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) && lower[3] >= '1'
           && lower[3] <= '9';
}
} // namespace

bool isAllowedParam(std::string_view id) noexcept
{
    return std::find(allowedParams.begin(), allowedParams.end(), id) != allowedParams.end();
}

std::string toLower(std::string_view text)
{
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

std::string sanitizeText(std::string_view text, std::size_t maxLength)
{
    // Bound the work for hostile input before any copying.
    text = trimSpaces(text.substr(0, maxLength * 4 + 8));
    std::string out;
    out.reserve(std::min(text.size(), maxLength));
    for (const char raw : text)
    {
        if (out.size() >= maxLength)
            break;
        const auto c = static_cast<unsigned char>(raw);
        out.push_back((c < 0x20 || c > 0x7E) ? '_' : raw);
    }
    const auto end = out.find_last_not_of(' ');
    out.erase(end == std::string::npos ? 0 : end + 1);
    return out;
}

std::string sanitizeTag(std::string_view tag)
{
    auto out = toLower(sanitizeText(tag, maxTagLength));
    std::replace(out.begin(), out.end(), ',', '_');
    return out;
}

std::string sanitizeFileStem(std::string_view name)
{
    name = name.substr(0, maxStemLength * 4);
    std::string out;
    for (const char raw : name)
    {
        if (out.size() >= maxStemLength)
            break;
        const auto c = static_cast<unsigned char>(raw);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' '
                        || c == '_' || c == '-' || c == '.' || c == '(' || c == ')';
        out.push_back(ok ? raw : '_');
    }
    // No leading/trailing dots or spaces (Windows strips them; ".." must never survive).
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos)
        return "preset";
    out.erase(0, first);
    const auto last = out.find_last_not_of(" .");
    out.erase(last + 1);
    while (out.find("..") != std::string::npos)
        out.replace(out.find(".."), 2, "_");
    if (out.empty())
        return "preset";
    if (isReservedWindowsName(out.substr(0, out.find('.'))))
        out.push_back('_');
    return out;
}

Preset makePreset(std::string_view name, std::string_view category, std::vector<std::string> tags,
                  const scene::SceneBank& bank, std::string_view author, std::vector<PresetParam> params)
{
    Preset p;
    p.name = sanitizeText(name, maxNameLength);
    p.category = sanitizeText(category, maxCategoryLength);
    p.author = sanitizeText(author, maxAuthorLength);
    for (auto& t : tags)
    {
        auto clean = sanitizeTag(t);
        if (!clean.empty() && p.tags.size() < maxTags
            && std::find(p.tags.begin(), p.tags.end(), clean) == p.tags.end())
            p.tags.push_back(std::move(clean));
    }
    for (auto& param : params)
        if (isAllowedParam(param.id) && std::isfinite(param.value) && p.params.size() < maxParams
            && std::none_of(p.params.begin(), p.params.end(), [&](const PresetParam& q) { return q.id == param.id; }))
            p.params.push_back({param.id, std::clamp(param.value, 0.0f, 1.0f)});
    p.sceneText = scene::SceneSerializer::serialize(bank);
    return p;
}

std::string serializePreset(const Preset& preset)
{
    std::string out;
    out.append(fileMagic).append(" v").append(std::to_string(fileVersion)).append("\n");
    out.append("name=").append(sanitizeText(preset.name, maxNameLength)).append("\n");
    out.append("category=").append(sanitizeText(preset.category, maxCategoryLength)).append("\n");
    out.append("author=").append(sanitizeText(preset.author, maxAuthorLength)).append("\n");
    std::string tags;
    std::size_t tagCount = 0;
    for (const auto& t : preset.tags)
    {
        const auto clean = sanitizeTag(t);
        if (clean.empty() || tagCount >= maxTags)
            continue;
        if (!tags.empty())
            tags.push_back(',');
        tags.append(clean);
        ++tagCount;
    }
    out.append("tags=").append(tags).append("\n");
    std::size_t paramCount = 0;
    for (const auto& p : preset.params)
        if (isAllowedParam(p.id) && std::isfinite(p.value) && paramCount++ < maxParams)
            out.append("param.").append(p.id).append("=").append(formatFloat(std::clamp(p.value, 0.0f, 1.0f))).append("\n");
    out.append("scenes\n");
    out.append(preset.sceneText);
    if (preset.sceneText.empty() || preset.sceneText.back() != '\n')
        out.push_back('\n');
    return out;
}

ParseResult parsePreset(std::string_view text) noexcept
{
    ParseResult result;
    try
    {
        const auto reject = [&result](std::string reason) {
            result.ok = false;
            result.preset = {};
            result.notes.insert(result.notes.begin(), std::move(reason));
            return result;
        };
        if (text.size() > maxFileBytes)
            return reject("file is larger than the preset size limit");
        if (text.starts_with("\xEF\xBB\xBF"))
            text.remove_prefix(3);

        const auto nextLine = [&text]() {
            const auto end = text.find('\n');
            const auto line = text.substr(0, end);
            text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            return line;
        };
        if (text.empty())
            return reject("empty file");
        {
            std::string_view header = nextLine();
            if (!header.empty() && header.back() == '\r')
                header.remove_suffix(1);
            if (!header.starts_with(fileMagic))
                return reject("not a GrooveMorph preset file");
            if (header != std::string(fileMagic) + " v" + std::to_string(fileVersion))
                return reject("unsupported preset version");
        }

        Preset p;
        bool sawName = false, sawCategory = false, sawAuthor = false, sawTags = false, sawScenes = false;
        std::size_t metadataLines = 0;
        while (!text.empty())
        {
            std::string_view line = nextLine();
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line == "scenes")
            {
                sawScenes = true;
                break;
            }
            if (++metadataLines > 256)
                return reject("too many metadata lines");
            const auto eq = line.find('=');
            if (eq == std::string_view::npos)
                continue;
            const auto key = trimSpaces(line.substr(0, eq));
            const auto value = line.substr(eq + 1);
            if (key == "name" && !sawName)
            {
                sawName = true;
                p.name = sanitizeText(value, maxNameLength);
            }
            else if (key == "category" && !sawCategory)
            {
                sawCategory = true;
                p.category = sanitizeText(value, maxCategoryLength);
            }
            else if (key == "author" && !sawAuthor)
            {
                sawAuthor = true;
                p.author = sanitizeText(value, maxAuthorLength);
            }
            else if (key == "tags" && !sawTags)
            {
                sawTags = true;
                std::string_view rest = value.substr(0, maxTags * (maxTagLength + 1) * 4);
                while (!rest.empty() && p.tags.size() < maxTags)
                {
                    const auto comma = rest.find(',');
                    auto tag = sanitizeTag(rest.substr(0, comma));
                    rest.remove_prefix(comma == std::string_view::npos ? rest.size() : comma + 1);
                    if (!tag.empty() && std::find(p.tags.begin(), p.tags.end(), tag) == p.tags.end())
                        p.tags.push_back(std::move(tag));
                }
            }
            else if (key.starts_with("param."))
            {
                const auto id = key.substr(6);
                float v = 0.0f;
                if (!isAllowedParam(id) || p.params.size() >= maxParams
                    || std::any_of(p.params.begin(), p.params.end(), [&](const PresetParam& q) { return q.id == id; }))
                    continue;
                if (!parseFloatStrict(value, v))
                {
                    result.notes.push_back("param." + std::string(id) + " is not a number; ignored");
                    continue;
                }
                const float clamped = std::clamp(v, 0.0f, 1.0f);
                if (clamped != v)
                    result.notes.push_back("param." + std::string(id) + " out of range; clamped");
                p.params.push_back({std::string(id), clamped});
            }
        }
        if (!sawScenes)
            return reject("missing scenes section");
        if (p.name.empty())
            return reject("preset has no name");

        auto loaded = scene::SceneSerializer::deserialize(text);
        if (loaded.report.status == scene::LoadStatus::Defaulted)
            return reject("scene data is unreadable");
        if (loaded.report.status == scene::LoadStatus::Recovered)
            result.notes.push_back("scene data was repaired while loading");
        p.sceneText = scene::SceneSerializer::serialize(loaded.bank);
        result.preset = std::move(p);
        result.ok = true;
        return result;
    }
    catch (...)
    {
        result.ok = false;
        result.preset = {};
        result.notes.assign(1, "internal error while reading preset");
        return result;
    }
}

std::optional<scene::SceneBank> bankOf(const Preset& preset)
{
    auto loaded = scene::SceneSerializer::deserialize(preset.sceneText);
    if (loaded.report.status == scene::LoadStatus::Defaulted)
        return std::nullopt;
    return std::move(loaded.bank);
}

scene::SceneBank applyToBank(const scene::SceneBank& current, const scene::SceneBank& presetBank)
{
    scene::SceneBank next = current;
    for (std::size_t i = 0; i < variationCount; ++i)
        next.setVariation(i, presetBank.variation(i));
    return next;
}

bool sameScenes(const scene::SceneBank& a, const scene::SceneBank& b) noexcept
{
    for (std::size_t i = 0; i < variationCount; ++i)
        if (!(a.variation(i) == b.variation(i)))
            return false;
    return true;
}
} // namespace groovemorph::core::preset
