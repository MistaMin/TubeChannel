//! The editor without a window: every parameter has a control on some page.

use super::*;
use crate::params::B;

fn pages() -> Vec<(Page, bool)> {
    let mut out = Vec::new();
    for page in [Page::Analogue, Page::Digital, Page::AnalogueSystem, Page::DigitalSystem] {
        for channel_b in [false, true] {
            out.push((page, channel_b));
        }
    }
    out
}

#[test]
fn every_parameter_has_a_control_in_each_skin() {
    for digital in [false, true] {
        let mut shown = vec![false; COUNT];
        for (page, channel_b) in pages() {
            if page.digital() != digital {
                continue;
            }
            for w in panel::widgets(page, channel_b, true, Overlay::None) {
                for p in w.params() {
                    shown[p] = true;
                }
            }
        }
        // The offline bounce choice and its switch are set from the toolbar's list.
        shown[idx::OFFLINE_BOUNCE] = true;
        shown[idx::OFFLINE_BOUNCE_ENABLED] = true;
        let missing: Vec<_> = PARAMS.iter().zip(&shown).filter(|(_, s)| !**s).map(|(p, _)| p.id).collect();
        assert!(missing.is_empty(), "{} skin has no control for {missing:?}", if digital { "digital" } else { "analogue" });
    }
}

#[test]
fn channel_b_tabs_address_channel_b() {
    let a = panel::widgets(Page::Analogue, false, true, Overlay::None);
    let b = panel::widgets(Page::Analogue, true, true, Overlay::None);
    let gain = |ws: &[Widget]| ws.iter().find_map(|w| match w.kind {
        Kind::Knob { param, title: "GAIN", .. } => Some(param),
        _ => None,
    });
    assert_eq!(gain(&a), Some(panel::GAIN));
    assert_eq!(gain(&b), Some(panel::GAIN + B));
}

#[test]
fn widgets_stay_inside_the_editor() {
    for (page, channel_b) in pages() {
        for w in panel::widgets(page, channel_b, true, Overlay::Bounce) {
            let (x, y, ww, h) = w.rect;
            assert!(x >= 0.0 && y >= 0.0 && x + ww <= W + 0.5 && y + h <= H + 0.5, "{page:?} {:?} at {:?}", w.kind, w.rect);
        }
    }
}
