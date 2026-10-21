#include "core/variation/GeneratorHistory.h"

#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace groovemorph::core::variation
{
std::string serializeHistory(const std::vector<HistoryEntry>& entries)
{
    std::ostringstream output;
    output << historyHeader << '\n';
    output << std::setprecision(9);
    std::size_t written = 0;
    for (const auto& entry : entries)
    {
        if (written++ >= maxHistoryEntries)
            break;
        scene::SceneBank sourceBank;
        sourceBank.setVariationSettings(0, entry.source);
        const auto sourceText = scene::SceneSerializer::serialize(sourceBank);
        output << "entry " << entry.scene << ' ' << entry.settings.seed << ' '
               << static_cast<int>(entry.settings.style) << ' ' << entry.settings.distance << ' '
               << entry.settings.candidate << ' ' << entry.settings.protectedOperations << ' '
               << (entry.settings.generateEventPatterns ? 1 : 0) << ' ' << entry.settings.phraseTicks << '\n'
               << sourceText << "end-entry\n";
    }
    return output.str();
}

std::vector<HistoryEntry> parseHistory(std::string_view text)
{
    std::vector<HistoryEntry> entries;
    if (text.empty() || text.size() > maxHistoryFileBytes)
        return entries;
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line) || line != historyHeader)
        return entries;
    while (entries.size() < maxHistoryEntries && std::getline(input, line))
    {
        if (line.empty())
            continue;
        HistoryEntry entry;
        int styleIndex = 0;
        int generateEventsValue = 0;
        std::istringstream header(line);
        std::string marker;
        if (!(header >> marker >> entry.scene >> entry.settings.seed >> styleIndex >> entry.settings.distance
                     >> entry.settings.candidate >> entry.settings.protectedOperations >> generateEventsValue
                     >> entry.settings.phraseTicks)
            || marker != "entry" || entry.scene >= variationCount || styleIndex < 0 || styleIndex > 2
            || (generateEventsValue != 0 && generateEventsValue != 1)
            || entry.settings.phraseTicks <= 0 || !std::isfinite(entry.settings.distance))
            break;
        entry.settings.style = static_cast<Style>(styleIndex);
        entry.settings.generateEventPatterns = generateEventsValue != 0;
        std::string sourceText, sourceLine;
        bool ended = false;
        while (std::getline(input, sourceLine))
        {
            if (sourceLine == "end-entry")
            {
                ended = true;
                break;
            }
            sourceText += sourceLine;
            sourceText.push_back('\n');
        }
        if (!ended)
            break;
        const auto sourceBank = scene::SceneSerializer::deserialize(sourceText);
        if (sourceBank.report.status == scene::LoadStatus::Defaulted)
            continue;
        entry.source = sourceBank.bank.variation(0).settings;
        entries.push_back(std::move(entry));
    }
    return entries;
}
} // namespace groovemorph::core::variation
