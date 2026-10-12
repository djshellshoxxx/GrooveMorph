#include "core/variation/VariationPlanner.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace groovemorph::core;
using namespace groovemorph::core::variation;
int main() {
    Request request;
    for (std::size_t i=0;i<operationCount;++i) {
        request.source.operations[i].enabled = (i%2)==0;
        request.source.operations[i].depth = 0.5f;
        request.source.operations[i].sliceBeats = 0.25f;
    }
    request.seed = 12345;
    request.style = Style::Experimental;
    request.candidateIndex = 7;
    auto a = generate(request), b = generate(request);
    assert(a.mutatedOperations == b.mutatedOperations);
    for (std::size_t i=0;i<operationCount;++i) {
        assert(a.settings.operations[i].enabled == b.settings.operations[i].enabled);
        assert(a.settings.operations[i].depth == b.settings.operations[i].depth);
        assert(a.settings.operations[i].sliceBeats == b.settings.operations[i].sliceBeats);
        assert(std::isfinite(a.settings.operations[i].depth));
        assert(a.settings.operations[i].depth >= 0.0f && a.settings.operations[i].depth <= 1.0f);
        assert(a.settings.operations[i].sliceBeats >= 0.0625f && a.settings.operations[i].sliceBeats <= 4.0f);
    }
    request.protectedOperations = (1u << operationCount) - 1u;
    auto protectedResult = generate(request);
    assert(protectedResult.mutatedOperations == 0);
    for(std::size_t i=0;i<operationCount;++i) {
        assert(protectedResult.settings.operations[i].depth == request.source.operations[i].depth);
        assert(protectedResult.settings.operations[i].sliceBeats == request.source.operations[i].sliceBeats);
        assert(protectedResult.settings.operations[i].enabled == request.source.operations[i].enabled);
    }
    std::cout << "VariationPlannerTests passed\n";
}
