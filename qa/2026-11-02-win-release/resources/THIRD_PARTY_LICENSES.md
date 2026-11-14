# Third-party licenses

Tube Channel's own source code is MIT licensed (see LICENSE). The macOS installer includes the applicable license
texts under `/Library/Application Support/Tube Channel/Licenses`. The repository keeps the project license and
third-party overview here; upstream license texts are copied from the dependency revisions used for the build.

The macOS 1.2.3 release package includes Tube Channel's AAX binary. Like the other official Tube Channel plugin
binaries, AAX is distributed as freeware; the source code remains MIT licensed. Building AAX from source requires
the separately licensed SDK and PACE signing access. The applicable AAX SDK license text is included with the macOS
installer when supplied with the SDK.

| Component | Used for | License |
|---|---|---|
| JUCE 9.0.3 | plugin framework, user interface, audio plugin formats | AGPL-3.0 or a commercial JUCE license. The released binaries are built under a JUCE Developer license. Anyone building from source needs a JUCE license that covers their use and distribution. |
| GoodLookinUI (`third_party/GoodLookinUI`) | knobs, panels, meters | MIT |
| CLAP and clap-juce-extensions | CLAP plugin format | MIT |
| VST3 SDK (bundled with JUCE) | VST3 plugin format | See the included SDK license texts. |
| LV2 SDK (bundled with JUCE) | LV2 plugin format | See the included SDK license texts. |
| Audio Unit SDK (bundled with JUCE) | Audio Unit plugin format | See the included SDK license text. |
| AAX SDK | AAX plug-in format when the licensed SDK is available | Avid AAX SDK License Agreement; the applicable SDK license text accompanies AAX packages. |
| PACE Eden | AAX binary signing | PACE signing tools are used under the developer's PACE account; the tools are not included in Tube Channel packages. |

Eco calibration tables are generated offline from the author's circuit models. NumPy is used by the development fitting tool and is not bundled in the plugin or installer.

The AAX SDK is not part of this repository. AAX builds are optional and require a separately licensed SDK and PACE signing access.

The circuit netlists in `Netlists/` are the author's own models. Semiconductor and tube parameters are generic
published device data.
