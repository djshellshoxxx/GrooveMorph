# Third-party notices

## GrooveMorph's own licence: not declared

**No licence has been declared for GrooveMorph itself.** The project owner has not chosen one, and this
file does not choose one. Until the owner does, no licence is granted to anyone to use, copy, modify or
redistribute GrooveMorph beyond whatever the owner has told you directly. This beta is shared with testers
on that understanding.

## JUCE (the main licensing constraint)

GrooveMorph is built on the JUCE framework, version 8.0.7 (pinned in `cmake/GrooveMorphPins.cmake`;
https://github.com/juce-framework/JUCE). Per the `LICENSE.md` of that release, the JUCE modules are
dual-licensed:

- the GNU Affero General Public License v3 (AGPLv3), or
- the commercial JUCE licence (the JUCE 8 End User Licence Agreement, https://juce.com/legal/juce-8-licence/,
  with tiers set by JUCE).

Consequence for the project owner: **before any distribution beyond testers, the owner must choose a
GrooveMorph licence that is compatible with the JUCE terms the project relies on** (either a licence
compatible with AGPLv3, with corresponding source offered to recipients, or a commercial JUCE licence whose
tier and revenue conditions the owner qualifies for). The tiers and conditions change over time; read the
current JUCE terms, do not rely on this summary. This file records the constraint and does not resolve it.

JUCE bundles further third-party code inside its modules, each under its own licence. The list below is
from the JUCE 8.0.7 `LICENSE.md`; which of these are compiled into a given GrooveMorph binary depends on
the JUCE modules and platform used and has not been audited here:

- AudioUnitSDK (Apache 2.0), Oboe (Apache 2.0), FLAC (BSD), GLEW including Mesa and Khronos parts
  (BSD/MIT), Ogg Vorbis (BSD), jpeglib (Independent JPEG Group License), CHOC with QuickJS (ISC/MIT),
  LV2 (ISC), pslextensions (public domain), Box2D (zlib), pnglib (zlib), zlib (zlib),
  HarfBuzz ("Old MIT"), SheenBidi (Apache).
- VST3 SDK: "Proprietary Steinberg VST3 License / GPLv3" (dual). "VST" is a trademark of Steinberg Media
  Technologies GmbH. The VST3 SDK licence terms (including any requirement to hold a VST3 licence agreement
  for closed-source distribution) must be checked by the owner against the SDK version bundled in
  `modules/juce_audio_processors/format_types/VST3_SDK/`.
- AAX SDK (Proprietary Avid AAX License / GPLv3) ships inside JUCE but GrooveMorph does not build AAX.

Full texts: see the files named in the JUCE `LICENSE.md` inside the JUCE source tree.

## CLAP format support

The CLAP build uses these components, all under the MIT licence:

| Component | Source | Copyright line (from its LICENSE) |
|---|---|---|
| clap-juce-extensions (JUCE-to-CLAP adapter), pinned commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` | https://github.com/free-audio/clap-juce-extensions | Copyright 2019-2020, Paul Walker |
| CLAP (plugin API headers) | https://github.com/free-audio/clap | Copyright (c) 2021 Alexandre BIQUE |
| clap-helpers | https://github.com/free-audio/clap-helpers | Copyright (c) 2021 Alexandre BIQUE |

MIT licence text (applies to each of the three components above, with that component's copyright line):

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
> following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial
> portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
> LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
> EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
> IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
> USE OR OTHER DEALINGS IN THE SOFTWARE.

The adapter's MIT licence does not change the JUCE terms above: a CLAP build still links JUCE and is
subject to the same choice.

## Tools used only to build and check (not shipped in the zips)

- clap-validator 0.4.1 (https://github.com/free-audio/clap-validator), downloaded by CI to check the CLAP
  build. It is not redistributed.
- CMake, the platform compilers, and the GitHub Actions runner images.

## Operating-system components

The Standalone and plugins call system libraries (Windows runtime, macOS frameworks, Linux ALSA/X11/
FreeType and glibc). These are dynamically linked and are not redistributed here.
