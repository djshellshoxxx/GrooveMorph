// VF-06: contrast (WCAG) and colour-vision-deficiency separation of the Visual window palette.
// Colour is never the only cue (every effect also has a glyph/texture), so effect pairs that are close under a
// simulated deficiency are reported, not hidden.
#include "core/viz/Palette.h"

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

using namespace groovemorph::core::viz;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

struct Rgb { double r, g, b; }; // linear 0..1

double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
Rgb unpack(std::uint32_t argb) {
    return {toLinear(((argb >> 16) & 0xFF) / 255.0), toLinear(((argb >> 8) & 0xFF) / 255.0), toLinear((argb & 0xFF) / 255.0)};
}
double luminance(const Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }
double contrast(std::uint32_t a, std::uint32_t b) {
    const double la = luminance(unpack(a)), lb = luminance(unpack(b));
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

using Matrix = std::array<std::array<double, 3>, 3>;
// Machado, Oliveira, Fernandes 2009, severity 1.0 (full dichromacy), applied in linear RGB.
constexpr Matrix protan{{{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}}};
constexpr Matrix deutan{{{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}}};
constexpr Matrix tritan{{{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}};

Rgb simulate(const Matrix& m, const Rgb& c) {
    auto clamp01 = [](double v) { return std::min(1.0, std::max(0.0, v)); };
    return {clamp01(m[0][0] * c.r + m[0][1] * c.g + m[0][2] * c.b), clamp01(m[1][0] * c.r + m[1][1] * c.g + m[1][2] * c.b),
            clamp01(m[2][0] * c.r + m[2][1] * c.g + m[2][2] * c.b)};
}

struct Lab { double l, a, b; };
Lab toLab(const Rgb& c) {
    const double x = 0.4124 * c.r + 0.3576 * c.g + 0.1805 * c.b;
    const double y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    const double z = 0.0193 * c.r + 0.1192 * c.g + 0.9505 * c.b;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double fx = f(x / 0.95047), fy = f(y), fz = f(z / 1.08883);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}
double deltaE(const Lab& p, const Lab& q) {
    return std::sqrt((p.l - q.l) * (p.l - q.l) + (p.a - q.a) * (p.a - q.a) + (p.b - q.b) * (p.b - q.b));
}

// UI theme values introduced by the GUI redesign. These replicate src/ui/theme/Theme.h (which needs JUCE and so
// cannot be included in a core test); keep both in sync. The shared colours come from core/viz/Palette.h.
namespace theme {
constexpr std::uint32_t surfaceRaised = 0xff252a35;
constexpr std::uint32_t surfaceHover = 0xff2f3542;
constexpr std::uint32_t textOnAccent = 0xff10131a;
constexpr std::uint32_t accent = 0xff4fd1c5;
constexpr std::uint32_t sounding = 0xff2e9e6a;
constexpr std::uint32_t queued = 0xffe6a700;
constexpr std::uint32_t danger = 0xffe5604f;
constexpr std::uint32_t focusRing = 0xff8fd8ff;
} // namespace theme

constexpr const char* effectNames[] = {"live", "repeat", "reverse", "gate", "half-time", "tape-stop", "filter-sweep", "bit-crush"};
} // namespace

int main() {
    // Text contrast (WCAG AA 4.5:1) on the panel and slate backgrounds.
    expect("ink on panel >= 4.5", contrast(palette::ink, palette::panel) >= 4.5);
    expect("ink on slate >= 4.5", contrast(palette::ink, palette::slate) >= 4.5);
    expect("dim text on panel >= 4.5", contrast(palette::dim, palette::panel) >= 4.5);
    expect("dim text on slate >= 4.5", contrast(palette::dim, palette::slate) >= 4.5);
    expect("warning text on slate >= 4.5", contrast(palette::warning, palette::slate) >= 4.5);
    std::cout << "contrast ink/panel " << contrast(palette::ink, palette::panel) << ", dim/panel " << contrast(palette::dim, palette::panel) << "\n";

    // UI theme (docs/specs/UI-REDESIGN.md): text on every surface, accent states, and non-text colours.
    expect("text on bg >= 4.5", contrast(palette::ink, palette::slate) >= 4.5);
    expect("text on surface >= 4.5", contrast(palette::ink, palette::panel) >= 4.5);
    expect("text on surfaceRaised >= 4.5", contrast(palette::ink, theme::surfaceRaised) >= 4.5);
    expect("text on surfaceHover >= 4.5", contrast(palette::ink, theme::surfaceHover) >= 4.5);
    expect("textDim on surface >= 4.5", contrast(palette::dim, palette::panel) >= 4.5);
    expect("textDim on surfaceRaised >= 4.5", contrast(palette::dim, theme::surfaceRaised) >= 4.5);
    expect("accent on surface >= 3", contrast(theme::accent, palette::panel) >= 3.0);
    expect("accent on bg >= 3", contrast(theme::accent, palette::slate) >= 3.0);
    expect("textOnAccent on accent >= 4.5", contrast(theme::textOnAccent, theme::accent) >= 4.5);
    expect("textOnAccent on sounding >= 4.5", contrast(theme::textOnAccent, theme::sounding) >= 4.5);
    expect("textOnAccent on queued >= 4.5", contrast(theme::textOnAccent, theme::queued) >= 4.5);
    expect("sounding on surface >= 3", contrast(theme::sounding, palette::panel) >= 3.0);
    expect("queued on surface >= 3", contrast(theme::queued, palette::panel) >= 3.0);
    expect("danger on surface >= 3", contrast(theme::danger, palette::panel) >= 3.0);
    expect("focus ring on surface >= 3", contrast(theme::focusRing, palette::panel) >= 3.0);
    expect("focus ring on surfaceRaised >= 3", contrast(theme::focusRing, theme::surfaceRaised) >= 3.0);
    std::cout << "contrast dim/raised " << contrast(palette::dim, theme::surfaceRaised) << ", onAccent/sounding "
              << contrast(theme::textOnAccent, theme::sounding) << ", onAccent/queued " << contrast(theme::textOnAccent, theme::queued) << "\n";

    // Graphics: every effect colour against the panel (WCAG non-text 3:1).
    for (std::size_t i = 0; i < palette::effects.size(); ++i) {
        const double c = contrast(palette::effects[i], palette::panel);
        std::cout << effectNames[i] << " on panel " << c << ":1\n";
        expect((std::string(effectNames[i]) + " >= 3:1 on panel").c_str(), c >= 3.0);
    }

    // Pairwise separation (CIE76 delta E): normal vision and three dichromacies.
    struct Vision { const char* name; const Matrix* m; double minimum; };
    const Vision visions[] = {{"normal", nullptr, 20.0}, {"protanopia", &protan, 8.0}, {"deuteranopia", &deutan, 8.0}, {"tritanopia", &tritan, 8.0}};
    for (const auto& v : visions) {
        double worst = 1.0e9;
        std::string pair;
        for (std::size_t i = 0; i < palette::effects.size(); ++i)
            for (std::size_t j = i + 1; j < palette::effects.size(); ++j) {
                Rgb a = unpack(palette::effects[i]), b = unpack(palette::effects[j]);
                if (v.m) { a = simulate(*v.m, a); b = simulate(*v.m, b); }
                const double d = deltaE(toLab(a), toLab(b));
                if (d < worst) { worst = d; pair = std::string(effectNames[i]) + " / " + effectNames[j]; }
            }
        std::cout << v.name << ": closest effect pair " << pair << " dE=" << worst << "\n";
        expect((std::string(v.name) + " effect separation").c_str(), worst >= v.minimum);
    }

    if (failures == 0) std::cout << "PaletteTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
