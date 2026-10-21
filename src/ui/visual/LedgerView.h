#pragma once

#include "ui/visual/VisualStyle.h"

#include "core/viz/Ledger.h"

namespace groovemorph::ui::visual
{
// The numbers behind the pictures, as text. Also the accessibility value and the copy-as-text source.
class LedgerView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LedgerView();
    void setLedger(groovemorph::core::viz::Ledger ledger);
    [[nodiscard]] juce::String asText() const { return juce::String(groovemorph::core::viz::ledgerAsText(ledger)); }
    void paint(juce::Graphics&) override;

private:
    groovemorph::core::viz::TextThrottle descriptionThrottle;
    groovemorph::core::viz::Ledger ledger;
};
} // namespace groovemorph::ui::visual
