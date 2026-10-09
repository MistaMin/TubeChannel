# Tube Channel 1.2.0

Tube Channel 1.2.0 adds the reduced Eco engine as the default for new instances.
The System page orders the engine buttons **Eco · Circuit · HQ**. Circuit is the
renamed Default mode. The original circuit implementation remains available.

Separate small buttons select **2x**, **4x**, or **8x** (the Derp symbol) with any
engine, at any session rate. Click the selected factor to return to 1x. Eco starts
at 1x; PSU simulation remains off for new instances. Older sessions retain their
original engine, automation range and effective adaptive oversampling. The first
engine click freezes the restored factor as an independent setting.

The local stereo workload matrix measured **26–35x faster at 48 kHz** and **14–19x
at 96 kHz** than v1.1.4 Default, exceeding the 10x minimum in every measured case.
The 20x stretch target was reached at 48 kHz. These measurements use Eco at 1x;
oversampling increases CPU cost. Sixteen serial JUCE processor instances met all
measured callback deadlines in the harness; this is not actual DAW/device playback.

Eco is a measured approximation: heavy-drive harmonics, aliasing, tube memory,
supply dynamics and component variation can differ from Circuit/HQ. Real program
material listening and real DAW stress testing remain unverified. Circuit/HQ are
available when the original response is needed.

The universal macOS installer supports Apple silicon and Intel Macs and offers
VST3, Audio Unit, CLAP, LV2 and Standalone. It includes the updated user guide,
screenshots, project license and applicable third-party license texts. No AAX binary
is included. Source is MIT licensed; provided binaries are freeware. Builds use
JUCE 9.0.3 under the existing commercial JUCE licensing arrangement.

See the [README](https://github.com/MistaMin/TubeChannel#readme),
[User Guide](https://github.com/MistaMin/TubeChannel/blob/main/docs/USER_MANUAL.md),
and [measurement report](https://github.com/MistaMin/TubeChannel/blob/main/docs/measurements/eco-1.2.0/README.md).

![Digital interface](https://raw.githubusercontent.com/MistaMin/TubeChannel/v1.2.0/docs/ui-digital.png)
![System page: independent engine and oversampling controls](https://raw.githubusercontent.com/MistaMin/TubeChannel/v1.2.0/docs/ui-system-digital.png)
