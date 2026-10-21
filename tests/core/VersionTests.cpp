// N06-A2: the compiled-in version string comes from the VERSION file and is well formed.
#include <cctype>
#include <cstdio>
#include <string>

#include "groovemorph/Version.h"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}
}  // namespace

int main() {
    const std::string full = GROOVEMORPH_VERSION_STRING;
    const std::string numeric = GROOVEMORPH_VERSION_NUMERIC;
    const std::string pre = GROOVEMORPH_VERSION_PRERELEASE;

    // numeric part is MAJOR.MINOR.PATCH
    const auto d1 = numeric.find('.');
    const auto d2 = d1 == std::string::npos ? std::string::npos : numeric.find('.', d1 + 1);
    check(d1 != std::string::npos && d2 != std::string::npos, "numeric has three fields");
    if (d1 != std::string::npos && d2 != std::string::npos) {
        check(allDigits(numeric.substr(0, d1)), "major is digits");
        check(allDigits(numeric.substr(d1 + 1, d2 - d1 - 1)), "minor is digits");
        check(allDigits(numeric.substr(d2 + 1)), "patch is digits");
    }

    if (pre.empty()) {
        check(full == numeric, "final release: full == numeric");
    } else {
        check(full == numeric + "-" + pre, "prerelease: full == numeric-prerelease");
        check(pre.rfind("beta.", 0) == 0 && allDigits(pre.substr(5)), "prerelease is beta.N");
    }
    check(numeric == GROOVEMORPH_CMAKE_PROJECT_VERSION, "numeric equals CMake project() version");

    if (failures == 0) std::printf("VersionTests passed (%s)\n", full.c_str());
    return failures == 0 ? 0 : 1;
}
