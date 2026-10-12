#include "core/variation/VariationPlanner.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace groovemorph::core;
using namespace groovemorph::core::variation;

int main() {
    Request req{};
    for (std::size_t i=0; i<operationCount; ++i) {
        req.source.operations[i].enabled = true;
        req.source.operations[i].depth = 0.5f;
        req.source.operations[i].sliceBeats = 0.25f;
    }
    req.seed = 12345;
    req.style = Style::Experimental;
    req.candidateIndex = 7;
    const auto one = generate(req);
    const auto two = generate(req);
    assert(one.mutatedOperations == two.mutatedOperations);
    for (std::size_t i=0; i<operationCount; ++i) {
        const auto& a=one.settings.operations[i];
        const auto& b=two.settings.operations[i];
        assert(a.enabled == b.enabled && a.depth == b.depth && a.sliceBeats == b.sliceBeats);
        assert(std::isfinite(a.depth) && a.depth>=0.0f && a.depth<=1.0f);
        assert(std::isfinite(a.sliceBeats) && a.sliceBeats>=0.0625f && a.sliceBeats<=4.0f);
    }

    // Protecting one operation cannot change random draws of another.
    for (std::size_t protectedIndex=0; protectedIndex<operationCount; ++protectedIndex) {
        auto masked=req;
        masked.protectedOperations=1u<<protectedIndex;
        const auto result=generate(masked);
        const auto& original=req.source.operations[protectedIndex];
        const auto& kept=result.settings.operations[protectedIndex];
        assert(kept.enabled == original.enabled);
        assert(kept.depth == original.depth);
        assert(kept.sliceBeats == original.sliceBeats);
        for (std::size_t j=0; j<operationCount; ++j) {
            if (j==protectedIndex) continue;
            assert(result.settings.operations[j].enabled==one.settings.operations[j].enabled);
            assert(result.settings.operations[j].depth==one.settings.operations[j].depth);
            assert(result.settings.operations[j].sliceBeats==one.settings.operations[j].sliceBeats);
        }
    }
    auto allProtected=req;
    allProtected.protectedOperations=(1u<<operationCount)-1u;
    assert(generate(allProtected).mutatedOperations==0u);
    std::cout<<"VariationPlannerTests passed\n";
}
