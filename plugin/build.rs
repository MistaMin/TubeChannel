//! Compiles the C++ chain (../Source, header only, no JUCE) and its C shim (cpp/bridge.cpp) for the
//! Cargo target, with the netlists of ../Netlists embedded as the CMake build embeds them. `cc` passes
//! the target's architecture and MACOSX_DEPLOYMENT_TARGET, so a universal build is one Cargo build
//! per architecture joined by `lipo`.

use std::{env, fs, path::Path};

const NETS: [&str; 12] = ["mic", "inst", "tube", "preout", "comp", "det", "eq1", "eq2", "eq3", "eq4", "out", "psu"];

fn main() {
    let manifest = env::var("CARGO_MANIFEST_DIR").unwrap();
    let out = env::var("OUT_DIR").unwrap();
    let root = Path::new(&manifest).join("..");

    // Netlists/EmbeddedNetlists.h.in with each @TC_<NET>@ replaced by the file, as CMake's configure_file does.
    let mut header = fs::read_to_string(root.join("Netlists/EmbeddedNetlists.h.in")).expect("Netlists/EmbeddedNetlists.h.in");
    for net in NETS {
        let path = root.join(format!("Netlists/{net}.net"));
        let text = fs::read_to_string(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()));
        assert!(!text.contains(")TCNET\""), "{net}.net closes the raw string");
        header = header.replace(&format!("@TC_{}@", net.to_uppercase()), &text);
        println!("cargo:rerun-if-changed={}", path.display());
    }
    fs::write(Path::new(&out).join("EmbeddedNetlists.h"), header).unwrap();

    let mut build = cc::Build::new();
    build.cpp(true).std("c++20").include(&out).include("cpp").include(root.join("Source")).file("cpp/bridge.cpp");
    build.flag_if_supported("-fvisibility=hidden");
    if env::var_os("MACOSX_DEPLOYMENT_TARGET").is_none() && cfg!(target_os = "macos") {
        build.flag("-mmacosx-version-min=11.0"); // what rustc links with by default
    }
    // The model is numerical code: optimise it in debug builds too, or tests take minutes.
    if env::var("OPT_LEVEL").as_deref() == Ok("0") {
        build.opt_level(2);
    }
    // No build path in the shipped binary.
    let source = root.join("Source").canonicalize().map(|p| p.display().to_string()).unwrap_or_default();
    for (from, to) in [(out.as_str(), "build"), (source.as_str(), "Source"), (manifest.as_str(), "plugin")] {
        if !from.is_empty() {
            build.flag_if_supported(format!("-ffile-prefix-map={from}={to}"));
        }
    }
    // MSVC has no prefix map: cut the repository folder off the file names its asserts keep.
    if let Some(repo) = Path::new(&manifest).parent() {
        build.flag_if_supported(format!("/d1trimfile:{}\\", repo.display()));
        build.flag_if_supported(format!("/d1trimfile:{manifest}\\..\\"));
    }
    // Linux: the C++ runtime goes into the plug-in, so it does not depend on the host system's libstdc++.
    let linux = env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("linux");
    if linux {
        build.cpp_link_stdlib(None);
    }
    build.compile("tubechannel_engine");
    if linux {
        println!("cargo:rustc-link-lib=static=stdc++");
    }
    println!("cargo:rerun-if-changed=cpp");
    println!("cargo:rerun-if-changed=../Netlists/EmbeddedNetlists.h.in");
    for dir in ["Channel", "Circuit", "DSP", "Hybrid", "Plugin/Chain.h"] {
        println!("cargo:rerun-if-changed=../Source/{dir}");
    }
}
