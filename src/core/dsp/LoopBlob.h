#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace groovemorph::core
{
// Compact, versioned, self-checking serialization of a loop's audio for plugin/project state (F09).
// Audio is quantised to 16-bit PCM (clamped to +-1: lossy relative to float, at most 1/32767 per sample)
// and coded losslessly from there with fixed predictors + Rice codes. Format v1, little endian:
//   "GMLB" | u16 version | u8 storedChannels (1|2) | u8 flags (bit0: duplicate channel 0 to stereo)
//   | u32 sampleRate | u32 samples | f64 loopBeats | f64 originBeats | u32 payloadBytes | payload | u32 CRC-32
// The decoder treats the blob as hostile input: every field is range checked, the payload must hold
// exactly the declared samples, and nothing is allocated before those checks pass.
namespace LoopBlob
{
inline constexpr std::uint16_t version = 1;
inline constexpr double maxSeconds = 20.0;
inline constexpr std::size_t maxEncodedBytes = 10u * 1024u * 1024u;
// Plugin state envelope limit (was 1 MiB before loop persistence): base64 of the largest blob (4/3 * 10 MiB
// = 13.4 MiB) plus 1 MiB of headroom for parameters, scenes, gestures and MIDI links, rounded to 16 MiB.
inline constexpr std::size_t maxStateBytes = 16u * 1024u * 1024u;
[[nodiscard]] constexpr bool stateSizeAcceptable(std::size_t bytes) noexcept { return bytes > 0 && bytes <= maxStateBytes; }
inline constexpr double minSampleRate = 8000.0;
inline constexpr double maxSampleRate = 384000.0;
inline constexpr double maxLoopBeats = 4096.0;

struct Audio
{
    double sampleRate = 0.0;
    double loopBeats = 0.0;
    double originBeats = 0.0;
    std::vector<float> left;
    std::vector<float> right; // always filled (equals left for mono / dual mono)
    [[nodiscard]] std::int64_t numSamples() const noexcept { return static_cast<std::int64_t>(left.size()); }
};

enum class Status
{
    Ok,
    Invalid,   // bad arguments or non-finite metadata
    TooLong,   // longer than maxSeconds
    TooLarge,  // encoded size above maxEncodedBytes
    Corrupt    // decode: not a valid blob
};

// channels: 1 or 2 pointers (a second channel identical to the first is stored once).
Status encode(const float* const* channels, int numChannels, std::int64_t numSamples, double sampleRate,
              double loopBeats, double originBeats, std::vector<std::uint8_t>& out);
Status decode(const std::uint8_t* data, std::size_t size, Audio& out);
} // namespace LoopBlob
} // namespace groovemorph::core
