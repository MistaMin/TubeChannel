"""Write the installed manifest from a configured release build and exact sources."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = (root / (sys.argv[1] if len(sys.argv) > 1 else "build/universal")).resolve()
cache = {}
for line in (build / "CMakeCache.txt").read_text().splitlines():
    if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value

def revision(path):
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()

juce = Path(cache.get("FETCHCONTENT_SOURCE_DIR_JUCE") or build / "_deps/juce-src")
clap = Path(cache.get("FETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS") or build / "_deps/clap-juce-extensions-src")
files = [root / "CMakeLists.txt", root / "VERSION"]
files += sorted((root / "Source").rglob("*.h")) + sorted((root / "Source").rglob("*.cpp"))
files += sorted((root / "Netlists").glob("*.net"))
manifest = {
    "version": (root / "VERSION").read_text().strip(),
    "architectures": cache.get("CMAKE_OSX_ARCHITECTURES", "").split(";"),
    "minimum_macos": cache.get("CMAKE_OSX_DEPLOYMENT_TARGET"),
    "configuration": cache.get("CMAKE_BUILD_TYPE"),
    "formats": ["VST3", "AU", "CLAP", "LV2", "Standalone"],
    "dependencies": {
        "JUCE": revision(juce),
        "GoodLookinUI": revision(root / "third_party/GoodLookinUI"),
        "clap-juce-extensions": revision(clap),
        "CLAP": revision(clap / "clap-libs/clap"),
        "clap-helpers": revision(clap / "clap-libs/clap-helpers"),
    },
    "compiler": subprocess.check_output(["clang", "--version"], text=True).splitlines()[0],
    "source_sha256": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    "licenses": "Included at /Library/Application Support/Tube Channel/Licenses",
}
if (build / "TubeChannel_artefacts/Release/AAX/Tube Channel.aaxplugin").exists():
    manifest["formats"].append("AAX")
    sdk_name = Path(cache.get("TUBECHANNEL_AAX_SDK", "")).name
    manifest["dependencies"]["AAX SDK"] = sdk_name.split("aax-sdk-", 1)[-1].replace("-", ".")
    manifest["licenses"] += "; AAX SDK license included in the installer"
(build / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
print("Wrote", build / "release-manifest.json")
