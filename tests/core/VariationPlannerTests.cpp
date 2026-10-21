#include "core/variation/VariationPlanner.h"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::variation;

namespace {
int failures = 0;

void expect(std::string_view name, bool condition) {
    if (!condition) {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Compare fields rather than struct padding, including exact NaN payloads.
bool same(const OperationSettings& a, const OperationSettings& b) {
    return a.enabled == b.enabled
        && std::bit_cast<std::uint32_t>(a.depth) == std::bit_cast<std::uint32_t>(b.depth)
        && std::bit_cast<std::uint32_t>(a.sliceBeats) == std::bit_cast<std::uint32_t>(b.sliceBeats);
}

bool bounded(const OperationSettings& op) {
    return std::isfinite(op.depth) && op.depth >= 0.0f && op.depth <= 1.0f
        && std::isfinite(op.sliceBeats) && op.sliceBeats >= 0.0625f && op.sliceBeats <= 4.0f;
}
} // namespace

int main() {
    Request req{};
    for (auto& op : req.source.operations)
        op = {true, 0.5f, 0.25f};
    req.seed = 12345;
    req.style = Style::Experimental;
    req.candidateIndex = 7;
    const auto one = generate(req);
    const auto two = generate(req);
    expect("deterministic mutation mask", one.mutatedOperations == two.mutatedOperations);
    for (std::size_t i = 0; i < operationCount; ++i) {
        expect("deterministic settings", same(one.settings.operations[i], two.settings.operations[i]));
        expect("valid input stays bounded", bounded(one.settings.operations[i]));
    }

    // Protection must neither modify its operation nor perturb another operation's draws.
    for (std::size_t protectedIndex = 0; protectedIndex < operationCount; ++protectedIndex) {
        auto masked = req;
        masked.protectedOperations = 1u << protectedIndex;
        const auto result = generate(masked);
        expect("protected fields preserved", same(result.settings.operations[protectedIndex],
                                                  req.source.operations[protectedIndex]));
        expect("protected operation excluded from mutation mask",
               (result.mutatedOperations & masked.protectedOperations) == 0);
        for (std::size_t j = 0; j < operationCount; ++j)
            if (j != protectedIndex)
                expect("protection keeps independent random draws",
                       same(result.settings.operations[j], one.settings.operations[j]));
    }
    auto allProtected = req;
    allProtected.protectedOperations = (1u << operationCount) - 1u;
    expect("all protected mutation mask is empty", generate(allProtected).mutatedOperations == 0u);

    // A planner is not a blanket sanitizer: protected and unselected operations
    // retain all original fields, even invalid floats. Only selected operations
    // are normalized to the planner's finite bounds.
    constexpr float invalid[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -100.0f, 100.0f
    };
    std::uint32_t selectedCoverage = 0;
    std::uint32_t skippedCoverage = 0;
    for (Style style : {Style::Conservative, Style::Creative, Style::Experimental}) {
        for (float value : invalid) {
            for (std::uint32_t candidate = 0; candidate < 64; ++candidate) {
                Request input = req;
                input.style = style;
                input.candidateIndex = candidate;
                for (auto& op : input.source.operations) {
                    op.depth = value;
                    op.sliceBeats = value;
                }
                const auto result = generate(input);
                expect("mutation mask has only existing operations",
                       (result.mutatedOperations >> operationCount) == 0u);
                for (std::size_t i = 0; i < operationCount; ++i) {
                    const std::uint32_t bit = 1u << i;
                    if ((result.mutatedOperations & bit) != 0) {
                        selectedCoverage |= bit;
                        expect("selected invalid input becomes finite and bounded",
                               bounded(result.settings.operations[i]));
                    } else {
                        skippedCoverage |= bit;
                        expect("unselected invalid fields preserved exactly",
                               same(result.settings.operations[i], input.source.operations[i]));
                    }
                }
                input.protectedOperations = (1u << operationCount) - 1u;
                const auto kept = generate(input);
                expect("invalid protected input is never marked mutated", kept.mutatedOperations == 0);
                for (std::size_t i = 0; i < operationCount; ++i)
                    expect("invalid protected fields preserved exactly",
                           same(kept.settings.operations[i], input.source.operations[i]));
            }
        }
    }
    expect("invalid fixtures selected every operation", selectedCoverage == (1u << operationCount) - 1u);
    expect("invalid fixtures skipped every operation", skippedCoverage == (1u << operationCount) - 1u);

    // N01: the planner reaches FilterSweep and BitCrush, and the draws for the five original operations are
    // unchanged by their existence (operations consume draws in index order).
    {
        std::uint32_t reached = 0;
        bool originalsStable = true;
        for (std::uint64_t seed = 1; seed <= 200; ++seed) {
            Request r{};
            for (auto& op : r.source.operations)
                op = {true, 0.5f, 0.25f};
            r.seed = seed;
            r.style = Style::Creative;
            const auto full = generate(r);
            reached |= full.mutatedOperations;
            Request guarded = r;
            guarded.protectedOperations = (1u << static_cast<std::size_t>(OperationType::FilterSweep))
                                          | (1u << static_cast<std::size_t>(OperationType::BitCrush));
            const auto without = generate(guarded);
            for (std::size_t i = 0; i < static_cast<std::size_t>(OperationType::FilterSweep); ++i)
                originalsStable = originalsStable && same(full.settings.operations[i], without.settings.operations[i]);
            expect("protected new ops are untouched", same(without.settings.operations[5], r.source.operations[5])
                                                          && same(without.settings.operations[6], r.source.operations[6]));
            expect("new ops stay bounded", bounded(full.settings.operations[5]) && bounded(full.settings.operations[6]));
        }
        expect("planner mutates FilterSweep", (reached & (1u << static_cast<std::size_t>(OperationType::FilterSweep))) != 0);
        expect("planner mutates BitCrush", (reached & (1u << static_cast<std::size_t>(OperationType::BitCrush))) != 0);
        expect("protecting the new ops leaves the original draws alone", originalsStable);
    }

    if (failures == 0)
        std::cout << "VariationPlannerTests passed\n";
    return failures == 0 ? 0 : 1;
}
