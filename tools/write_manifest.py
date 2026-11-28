#!/usr/bin/env python3
"""Writes opengrid-manifest.json (schema 2) for a Tube Channel release.

  tools/write_manifest.py --version 2.0.0 --released 2026-11-28 --out DIR/opengrid-manifest.json \
      [--mac PKG] [--windows EXE] [--linux TAR_GZ]

Size and SHA-256 come from each final file (after signing, notarization and stapling).
The install paths are the ones plugin/scripts/make_installer.sh, installer/windows.iss and the
Linux archive step of .github/workflows/build.yml use.
"""
import argparse
import hashlib
import json
from pathlib import Path

NAME = "Tube Channel"
LV2 = "TubeChannel"
HIGHLIGHTS = [
    "Rebuilt on OpenPluginSDK: same sound, same parameters, 1.x projects and presets open unchanged",
    "Autogain matches the output loudness to the input; Delta plays only what the processing changes",
    "Engine and oversampling changes are built in the background and faded in",
    "macOS universal (VST3, AU, CLAP, LV2, AAX), Windows and Linux (VST3, CLAP, LV2)",
]


def entry(path: Path, os_name: str, arch: list, min_os: str, installs: list) -> dict:
    data = path.read_bytes()
    build_class = "modern"  # macOS 11 or later, Windows 10 or later, Linux
    if os_name == "macos":
        assert float(min_os) >= 11.0, "a modern macOS build needs 11.0 or later"
    return {
        "os": os_name,
        "arch": arch,
        "min_os": min_os,
        "build_class": build_class,
        "name": path.name,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "installs": [{"format": f, "path": p} for f, p in installs],
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", required=True)
    ap.add_argument("--released", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--mac", type=Path)
    ap.add_argument("--windows", type=Path)
    ap.add_argument("--linux", type=Path)
    a = ap.parse_args()
    archives = []
    if a.mac:
        archives.append(entry(a.mac, "macos", ["universal"], "11.0", [
            ("vst3", f"Library/Audio/Plug-Ins/VST3/{NAME}.vst3"),
            ("au", f"Library/Audio/Plug-Ins/Components/{NAME}.component"),
            ("clap", f"Library/Audio/Plug-Ins/CLAP/{NAME}.clap"),
            ("lv2", f"Library/Audio/Plug-Ins/LV2/{LV2}.lv2"),
            ("aax", f"Library/Application Support/Avid/Audio/Plug-Ins/{NAME}.aaxplugin"),
        ]))
    if a.windows:
        archives.append(entry(a.windows, "windows", ["x64"], "10.0", [
            ("vst3", f"Common Files/VST3/{NAME}.vst3"),
            ("clap", f"Common Files/CLAP/{NAME}.clap"),
            ("lv2", f"Common Files/LV2/{LV2}.lv2"),
        ]))
    if a.linux:
        # Linux: the minimum is the glibc of the build machine (Ubuntu 22.04).
        archives.append(entry(a.linux, "linux", ["x86_64"], "2.35", [
            ("vst3", f"usr/lib/vst3/{NAME}.vst3"),
            ("clap", f"usr/lib/clap/{NAME}.clap"),
            ("lv2", f"usr/lib/lv2/{LV2}.lv2"),
        ]))
    if not archives:
        raise SystemExit("no archive given")
    manifest = {
        "schema": 2,
        "plugin": NAME,
        "version": a.version,
        "released": a.released,
        "highlights": HIGHLIGHTS,
        "archives": archives,
    }
    a.out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {a.out} with {len(archives)} archive(s)")


if __name__ == "__main__":
    main()
