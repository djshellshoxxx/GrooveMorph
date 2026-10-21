#pragma once

#include <array>
#include <cstdint>

// Portable colour constants for the Visual window (0xAARRGGBB). The JUCE layer wraps these in juce::Colour;
// keeping them here lets tests check contrast and colour-vision-deficiency separation without a GUI.
// Effect colours are Okabe-Ito; every effect is also distinguished by a glyph/texture, never colour alone.
namespace groovemorph::core::viz::palette
{
inline constexpr std::uint32_t slate = 0xff15171d;
inline constexpr std::uint32_t panel = 0xff1c1f27;
inline constexpr std::uint32_t line = 0xff2c303b;
inline constexpr std::uint32_t ink = 0xffece6d6;
inline constexpr std::uint32_t dim = 0xff8b90a0;
inline constexpr std::uint32_t hatch = 0xff4a4f5e;
inline constexpr std::uint32_t warning = 0xffe69f00;

inline constexpr std::uint32_t live = 0xffb9b4a6;
inline constexpr std::uint32_t repeat = 0xff56b4e9;
inline constexpr std::uint32_t reverse = 0xffe69f00;
inline constexpr std::uint32_t gate = 0xffcc79a7;
inline constexpr std::uint32_t halfTime = 0xff009e73;
inline constexpr std::uint32_t tapeStop = 0xfff0e442;

// Okabe-Ito blue and vermillion (the two remaining palette entries), verified by PaletteTests.
inline constexpr std::uint32_t filterSweep = 0xff0072b2;
inline constexpr std::uint32_t bitCrush = 0xffd55e00;

inline constexpr std::array<std::uint32_t, 8> effects = {live, repeat, reverse, gate, halfTime, tapeStop, filterSweep, bitCrush};
} // namespace groovemorph::core::viz::palette
