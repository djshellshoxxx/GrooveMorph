#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// User-defined MIDI CC links ("MIDI learn"). Portable; no JUCE.
//
// A link maps one CC number to one control target:
//   "p:<parameterId>"      a host parameter (intensity, dry/wet, tempo, morph, ...)
//   "s:<effect>:<field>"   a scene setting of the sounding scene; effect 0..4, field depth | slice | enabled
// One target has at most one CC; one CC may drive several targets.
namespace groovemorph::core
{
struct MidiLink
{
    int cc = 0;
    std::string target;
};

class MidiLinkTable final
{
public:
    static constexpr std::size_t maxLinks = 64;

    enum class LinkResult : std::uint8_t { Ok, Invalid, TableFull };
    // Replaces any existing link of the same target. Invalid: the CC is out of 0..127 or the target is not
    // well formed. TableFull: a new target would exceed maxLinks (relinking a present target still works).
    LinkResult linkChecked(int cc, std::string_view target);
    // Same as linkChecked(), true only for Ok.
    bool link(int cc, std::string_view target);
    void unlink(std::string_view target);
    void clear() noexcept { entries.clear(); }
    [[nodiscard]] int ccFor(std::string_view target) const noexcept; // -1 when not linked
    [[nodiscard]] const std::vector<MidiLink>& links() const noexcept { return entries; }

    // "20=p:masterIntensity;21=s:0:depth". Invalid entries are skipped on load, duplicates keep the last.
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static MidiLinkTable deserialize(std::string_view text);

    [[nodiscard]] static bool validTarget(std::string_view target) noexcept;

private:
    std::vector<MidiLink> entries;
};

// Audio thread -> message thread hand-off for CC values: atomic stores only, no allocation, no locks.
class CcInbox final
{
public:
    CcInbox() noexcept;
    // Audio thread.
    void push(int cc, int value) noexcept;

    struct Change
    {
        int cc = 0;
        int value = 0;  // 0..127, the newest value
        int travel = 0; // sum of |step| of every event since the last collect(): how much the control moved
    };
    // Message thread. Writes the CCs that moved since the last collect() (up to capacity) and returns how
    // many. A CC counts as moved when its newest value differs OR it travelled in between, so a pulse that
    // returns to its old value (0 -> 127 -> 0 between two polls) is still reported. Unmoved CCs are not
    // repeated.
    std::size_t collect(Change* out, std::size_t capacity) noexcept;
    // Message thread. Forget everything pending, so only movements after this call are reported.
    void discardPending() noexcept;

private:
    std::array<std::atomic<std::int16_t>, 128> latest;
    std::array<std::atomic<std::uint32_t>, 128> travelTotal; // wraps; only differences are used
    std::array<std::int16_t, 128> seen{};                    // message thread only
    std::array<std::uint32_t, 128> seenTravel{};             // message thread only
};

// MIDI learn: the CC to link among those that moved in one collect() batch. The one that travelled most
// wins (a fader sweep beats a stray button press); ties go to the lowest CC. -1 when the batch is empty or
// nothing travelled.
[[nodiscard]] int pickLearnCc(const CcInbox::Change* changes, std::size_t count) noexcept;
} // namespace groovemorph::core
