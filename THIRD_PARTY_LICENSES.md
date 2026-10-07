# Third-party licenses

Tube Channel's own source code is MIT licensed (see LICENSE). It is built with the following components, each under
its own license. The license texts ship with those projects.

| Component | Used for | License |
|---|---|---|
| [JUCE](https://juce.com) 8 | plugin framework, user interface, audio plugin formats | AGPL-3.0 or the JUCE commercial license. The released binaries are built with a JUCE Developer license. Anyone building Tube Channel from source needs their own JUCE license that fits how they distribute the result. |
| GoodLookinUI (`third_party/GoodLookinUI`) | knobs, panels, meters | MIT (its LICENSE file is in that folder) |
| [CLAP](https://github.com/free-audio/clap) and [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | CLAP plugin format | MIT |
| Steinberg VST 3 SDK (bundled with JUCE) | VST3 plugin format | see the license in the JUCE modules (`juce_audio_processors/format_types/VST3_SDK`) |
| LV2 (bundled with JUCE) | LV2 plugin format | ISC |

The AAX SDK is not part of this repository and not part of the released binaries.

The circuit netlists in `Netlists/` are the author's own models. Semiconductor and tube parameters are generic
published device data.
