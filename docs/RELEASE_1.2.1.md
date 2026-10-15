# Tube Channel 1.2.1

Adds the **Offline Bounce** dropdown between Redo and System, with all nine
combinations of Eco / Circuit / HQ and 2x / 4x / Derp (8x).

- New instances use Eco/1x for playback and HQ/8x for host-reported offline rendering.
- Playback, recording and realtime exports use the normal playback settings.
- Earlier saved projects retain their bounce behavior until an offline choice is made.
- Offline processing starts with the selected engine on the first audio block, without
  the live mode-change fade; repeated bounces restart consistently.
- Playback history and its automation remain separate. Callback preparation and
  destruction stay outside realtime processing.
- Linear-phase processing uses a fixed, compensated latency budget across profiles.
- The menu is available in both skins and on both pages. Dual Mono channel tabs
  adapt around the dropdown without overlapping it.

Includes universal macOS VST3, AU, CLAP, LV2 and Standalone builds, the refreshed guide
and screenshots, project license and applicable dependency license texts. No AAX
binary is included. Source is MIT licensed; supplied binaries are freeware.

The Eco sound/performance model is unchanged from 1.2.0. Its measured speedups apply
to Eco at 1x; high-drive sound differences remain documented. Automated processor
verification is not a claim of listening acceptance or testing every DAW's offline flag.

See the [User Guide](https://github.com/MistaMin/TubeChannel/blob/main/docs/USER_MANUAL.md).

![Offline Bounce dropdown](https://raw.githubusercontent.com/MistaMin/TubeChannel/v1.2.1/docs/ui-system-digital.png)
![Nine offline combinations (menu illustration)](https://raw.githubusercontent.com/MistaMin/TubeChannel/v1.2.1/docs/offline-bounce-digital.png)
