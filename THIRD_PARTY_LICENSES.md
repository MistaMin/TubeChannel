# Third-party licenses

Tube Channel's own source code is MIT licensed (see LICENSE). The macOS installer includes the applicable license
texts under `/Library/Application Support/Tube Channel/Licenses`. The repository keeps the project license and
third-party overview here; upstream license texts are copied from the dependency revisions used for the build.

The macOS release package includes Tube Channel's AAX binary. Like the other official Tube Channel plugin
binaries, AAX is distributed as freeware; the source code remains MIT licensed. Building AAX from source requires
the separately licensed SDK and PACE signing access. The applicable AAX SDK license text is included with the macOS
installer when supplied with the SDK.

| Component | Used for | License |
|---|---|---|
| OpenPluginSDK (`openplugin` and its crates) | plugin framework and the CLAP, VST3, Audio Unit, AAX and LV2 format layers | MIT or Apache-2.0 |
| GoodLookin DRI (`goodlookindri`, `goodlookindri-shell`) | the editor: knobs, panels, meters, text | MIT |
| Bundled fonts of GoodLookin DRI | lettering where the system font is not used | SIL Open Font License 1.1 |
| fontdue, ttf-parser | font rasterising | MIT, Apache-2.0 or Zlib (fontdue); MIT or Apache-2.0 (ttf-parser) |
| rustfft, num-complex, num-integer, num-traits, primal-check, strength_reduce, transpose | the spectrum display | MIT or Apache-2.0 |
| hashbrown, ahash, allocator-api2, cfg-if, once_cell, zerocopy, log | support libraries | MIT or Apache-2.0 |
| CLAP | CLAP plugin format | MIT |
| VST3 SDK | VST3 plugin format | See the included SDK license texts. |
| LV2 | LV2 plugin format | ISC |
| Audio Unit SDK | Audio Unit plugin format | Apache-2.0 |
| AAX SDK | AAX plug-in format when the licensed SDK is available | Avid AAX SDK License Agreement; the applicable SDK license text accompanies AAX packages. |
| PACE Eden | AAX binary signing | PACE signing tools are used under the developer's PACE account; the tools are not included in Tube Channel packages. |

Versions 1.2.x were built with JUCE 9.0.3 under a JUCE Developer license and with GoodLookinUI (MIT). The 1.2.x project is still in the repository; building it needs a JUCE license that covers your use and distribution. Version 2.0 does not use JUCE.

Eco calibration tables are generated offline from the author's circuit models. NumPy is used by the development fitting tool and is not bundled in the plugin or installer.

The AAX SDK is not part of this repository. AAX builds are optional and require a separately licensed SDK and PACE signing access.

The circuit netlists in `Netlists/` are the author's own models. Semiconductor and tube parameters are generic
published device data.

VST is a registered trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of Apple Inc. AAX and Pro Tools are trademarks of Avid Technology, Inc.
