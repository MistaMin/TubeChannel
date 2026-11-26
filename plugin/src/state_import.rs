//! Reading the state the JUCE release saved, so its sessions and presets open in the port
//! (`openplugin_core::foreign_state`).
//!
//! The format (`openplugin-port capture`, tests/fixtures): the magic `VC2!`, a little-endian `u32`
//! length, then an XML document and a zero byte. The root is `TubeChannelState` with one `PARAM`
//! element per parameter: `id` is the parameter id, which the port kept, and `value` the plain value.
//! The root's `skin`, `vuSrc` and `sysPage` attributes are the editor's view.

use openplugin::core::foreign_state::import::{import, ImportedState, ParamMap, StateFormat};
use openplugin::core::foreign_state::{xml, ErrorKind, ForeignError, Result};

use crate::params::{idx, PARAMS};
use crate::{View, COUNT};

const MAGIC: &[u8; 4] = b"VC2!";
const ROOT: &str = "TubeChannelState";

pub static FORMATS: [StateFormat; 1] = [StateFormat { name: "Tube Channel 1.x (JUCE)", detect: original_detect, decode: original_decode }];

fn invalid(what: &'static str) -> ForeignError {
    ForeignError { kind: ErrorKind::Invalid, offset: 0, what }
}

fn original_detect(data: &[u8]) -> Option<u32> {
    data.starts_with(MAGIC).then_some(1)
}

fn document(data: &[u8]) -> Option<&[u8]> {
    let length = data.get(4..8).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as usize)?;
    let body = data.get(8..)?.get(..length)?;
    Some(body.strip_suffix(&[0]).unwrap_or(body))
}

fn original_decode(data: &[u8], version: u32) -> Result<ImportedState> {
    let root = xml::parse(document(data).ok_or(invalid("shorter than its length"))?)?;
    if root.name != ROOT {
        return Err(invalid("not a Tube Channel state"));
    }
    let mut state = ImportedState::new(FORMATS[0].name, version);
    for param in root.children_named("PARAM") {
        let id = param.attr("id").ok_or(invalid("a PARAM without an id"))?;
        let value = param.attr("value").and_then(|v| v.parse::<f64>().ok()).filter(|v| v.is_finite()).ok_or(invalid("a PARAM without a number"))?;
        state.push_plain(id.into(), value);
    }
    Ok(state)
}

/// `None` when `data` is not the release's format (the port's own is tried next); `Some(Err(_))`
/// when it is but is damaged (refuse it); otherwise every parameter's plain value and the skin.
/// A parameter the state does not name gets its default, except for the ones later releases added,
/// which get the value that keeps an older session sounding as it did
/// (`TubeChannelProcessor::setStateInformation`).
pub fn import_original(data: &[u8]) -> Option<std::result::Result<([f32; COUNT], View), String>> {
    let state = match import(&FORMATS, data)? {
        Ok(state) => state,
        Err(e) => return Some(Err(e.to_string())),
    };
    let named = |id: &str| document(data).and_then(|d| xml::parse(d).ok()).is_some_and(|root| root.children_named("PARAM").any(|p| p.attr("id") == Some(id)));
    let mut values: [f32; COUNT] = std::array::from_fn(|i| PARAMS[i].default);
    // Released states have no Eco flag (the original engine), no split oversampling (legacy
    // adaptive factors) and no bounce choice (bounce off). Autogain and Delta did not exist.
    if !named("ecomode") {
        values[idx::ECO] = 0.0;
    }
    if !named("oversampling") {
        values[idx::OVERSAMPLING] = 0.0;
    }
    if !named("offlinebounce") {
        values[idx::OFFLINE_BOUNCE] = 8.0;
        if !named("offlinebounceenabled") {
            values[idx::OFFLINE_BOUNCE_ENABLED] = 0.0;
        }
    }
    let mut map = ParamMap::new();
    for info in PARAMS.iter() {
        map.map(info.id, info.id);
    }
    for (index, value) in map.apply(&PARAMS, &state).values {
        values[index] = value;
    }
    Some(Ok((values, view(data))))
}

/// The root's `skin`, `vuSrc` and `sysPage` properties.
fn view(data: &[u8]) -> View {
    let root = document(data).and_then(|d| xml::parse(d).ok());
    let number = |name: &str| root.as_ref().and_then(|r| r.attr(name)).and_then(|v| v.parse::<f32>().ok()).filter(|v| v.is_finite());
    let d = View::default();
    View {
        skin: number("skin").map_or(d.skin, |v| u8::from(v >= 0.5)),
        vu_source: number("vuSrc").map_or(d.vu_source, |v| v.clamp(0.0, 3.0).round() as u8),
        system_page: number("sysPage").is_some_and(|v| v >= 0.5),
    }
}
