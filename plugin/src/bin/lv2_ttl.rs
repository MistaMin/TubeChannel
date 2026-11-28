//! Writes the LV2 description files into a bundle folder:
//! `lv2_ttl <folder> <library stem>` gives `manifest.ttl` and `<stem>.ttl`.
use tube_channel::TubeChannel;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (Some(dir), Some(stem)) = (args.get(1), args.get(2)) else {
        eprintln!("usage: lv2_ttl <folder> <library stem>");
        std::process::exit(2);
    };
    if let Err(e) = openplugin::targets::lv2::write_bundle_for_editor(&TubeChannel::default(), std::path::Path::new(dir), stem, Some(tube_channel::make_editor)) {
        eprintln!("{dir}: {e}");
        std::process::exit(1);
    }
    // The meters and status lights reach the editor only through instance-access, and REAPER passes
    // it only when the editor requires it (seen 2026-11-27: optional left the meters dark there).
    let path = std::path::Path::new(dir).join(format!("{stem}.ttl"));
    let ttl = std::fs::read_to_string(&path).expect("the description just written");
    let optional = "lv2:optionalFeature <http://lv2plug.in/ns/ext/instance-access> , ";
    assert!(ttl.contains(optional), "{}: the editor's instance-access line is not where it was", path.display());
    let required = "lv2:requiredFeature <http://lv2plug.in/ns/ext/instance-access> ;\n    lv2:optionalFeature ";
    std::fs::write(&path, ttl.replace(optional, required)).expect("write the description");
}
