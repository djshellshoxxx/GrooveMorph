#include "core/dsp/PrintNaming.h"

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

using namespace groovemorph::core::printnaming;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}
void expectEq(std::string_view name, const std::string& got, const std::string& want)
{
    if (got != want)
    {
        std::cerr << name << ": got '" << got << "' want '" << want << "'\n";
        ++failures;
    }
}
} // namespace

int main()
{
    // N04-N01 sanitiser
    expectEq("plain", sanitizeComponent("Scene A"), "Scene-A");
    expectEq("separators dropped", sanitizeComponent("a/b\\c:d*e?f\"g<h>i|j"), "abcdefghij");
    expectEq("dots dropped", sanitizeComponent("..\\..\\evil.wav"), "evilwav");
    expectEq("underscore", sanitizeComponent("my_scene_1"), "my-scene-1");
    expectEq("unicode dropped", sanitizeComponent("Gr\xC3\xB6" "ove"), "Grove");
    expectEq("control dropped", sanitizeComponent(std::string("a\x01\x1f") + "b\tc"), "abc");
    expectEq("runs collapse", sanitizeComponent("a   --  b"), "a-b");
    expectEq("trim", sanitizeComponent("  -hello- "), "hello");
    expectEq("empty", sanitizeComponent(""), "Scene");
    expectEq("only junk", sanitizeComponent("///***"), "Scene");
    expectEq("reserved CON", sanitizeComponent("con"), "con-");
    expectEq("reserved COM1", sanitizeComponent("COM1"), "COM1-");
    expectEq("reserved lpt9", sanitizeComponent("lpt9"), "lpt9-");
    expectEq("not reserved COM10", sanitizeComponent("COM10"), "COM10");
    expectEq("long", sanitizeComponent(std::string(100, 'a')), std::string(32, 'a'));
    expectEq("long ends trimmed", sanitizeComponent(std::string(31, 'a') + " b"), std::string(31, 'a'));
    expectEq("custom max", sanitizeComponent("abcdef", 3), "abc");

    // N04-N02 names
    expectEq("bpm whole", formatBpm(120.0), "120");
    expectEq("bpm tenth", formatBpm(123.46), "123.5");
    expectEq("bpm rounding up to whole", formatBpm(119.96), "120");
    expectEq("bpm 90.05", formatBpm(90.04), "90");
    expectEq("bpm invalid", formatBpm(-1.0), "0");
    expectEq("bpm nan", formatBpm(std::nan("")), "0");
    expectEq("bpm zero", formatBpm(0.0), "0");

    Timestamp ts;
    ts.year = 2026; ts.month = 10; ts.day = 5; ts.hour = 7; ts.minute = 3; ts.second = 9;
    expectEq("timestamp", formatTimestamp(ts), "20261005-070309");
    expectEq("name", buildPrintFileName("Scene A", 120.0, 4, ts), "GrooveMorph_Scene-A_120bpm_4bars_20261005-070309.wav");
    expectEq("name junk scene", buildPrintFileName("../x", 87.5, 16, ts), "GrooveMorph_x_87.5bpm_16bars_20261005-070309.wav");
    expectEq("collision", withCollisionSuffix("GrooveMorph_A_120bpm_1bars_x.wav", 2), "GrooveMorph_A_120bpm_1bars_x-2.wav");
    expectEq("collision n1", withCollisionSuffix("a.wav", 1), "a.wav");
    expectEq("collision no ext", withCollisionSuffix("abc", 3), "abc-3");

    expect("isPrint yes", isPrintFileName("GrooveMorph_A_120bpm_4bars_20261005-070309.wav"));
    expect("isPrint no prefix", !isPrintFileName("Other_A_120bpm_4bars_20261005-070309.wav"));
    expect("isPrint no ext", !isPrintFileName("GrooveMorph_A_120bpm_4bars_20261005-070309.mp3"));
    expect("isPrint short", !isPrintFileName("GrooveMorph_.wav"));

    const auto parsed = parsePrintFileName(buildPrintFileName("Scene A", 123.5, 8, ts));
    expect("parse valid", parsed.valid);
    expect("parse scene", parsed.scene == "Scene-A");
    expect("parse bpm", parsed.bpm == 123.5);
    expect("parse bars", parsed.bars == 8);
    const auto suffixed = parsePrintFileName(withCollisionSuffix(buildPrintFileName("X", 100.0, 2, ts), 2));
    expect("parse collision suffix", suffixed.valid && suffixed.bars == 2 && suffixed.bpm == 100.0);
    expect("parse underscore scene", parsePrintFileName("GrooveMorph_a_b_100bpm_2bars_20261005-070309.wav").scene == "a_b");
    expect("parse garbage", !parsePrintFileName("GrooveMorph_garbage.wav").valid);
    expect("parse bad bars", !parsePrintFileName("GrooveMorph_A_100bpm_17bars_20261005-070309.wav").valid);
    expect("parse bad bpm", !parsePrintFileName("GrooveMorph_A_1x0bpm_2bars_20261005-070309.wav").valid);
    expect("parse not print", !parsePrintFileName("song.wav").valid);
    expect("list size", kMaxListed == 20);

    if (failures == 0)
        std::cout << "PrintNamingTests passed\n";
    return failures == 0 ? 0 : 1;
}
