//! Moves a scene drawn in editor units into window points (from Blue Channel's editor).

use openplugin::dri::goodlookindri::scene::{Cmd, Paint, Point, Rect, Scene, Shape};


/// Put a scene drawn in panel units into the window: `k` points per unit, the panel's top
/// left corner at (`dx`, `dy`) points.
pub fn place(scene: &mut Scene, k: f32, dx: f32, dy: f32) {
    if k == 1.0 && dx == 0.0 && dy == 0.0 {
        return;
    }
    let p = |p: &mut Point| *p = Point::new(p.x * k + dx, p.y * k + dy);
    let r = |r: &mut Rect| *r = Rect::new(r.x * k + dx, r.y * k + dy, r.w * k, r.h * k);
    let shape = |s: &mut Shape| match s {
        Shape::Ellipse(rect) => r(rect),
        Shape::Round(rect, radius) => {
            r(rect);
            *radius *= k;
        }
    };
    for cmd in &mut scene.cmds {
        match cmd {
            Cmd::FillEllipse { rect, .. } | Cmd::FillRect { rect, .. } => r(rect),
            Cmd::StrokeEllipse { rect, width, .. } => {
                r(rect);
                *width *= k;
            }
            Cmd::FillRoundRect { rect, radius, .. } | Cmd::ClipRound { rect, radius } => {
                r(rect);
                *radius *= k;
            }
            Cmd::StrokeRoundRect { rect, radius, width, .. } => {
                r(rect);
                *radius *= k;
                *width *= k;
            }
            Cmd::Line { a, b, width, .. } => {
                p(a);
                p(b);
                *width *= k;
            }
            Cmd::FillPoly { pts, .. } => pts.iter_mut().for_each(p),
            Cmd::StrokePoly { pts, width, .. } => {
                pts.iter_mut().for_each(p);
                *width *= k;
            }
            Cmd::StrokeArc { center, radius, width, .. } => {
                p(center);
                *radius *= k;
                *width *= k;
            }
            Cmd::Text { at, size, .. } => {
                p(at);
                *size *= k;
            }
            Cmd::Fill { shape: s, paint, feather, .. } => {
                shape(s);
                *feather *= k;
                match paint {
                    Paint::Radial { center, rx, ry, focus, .. } => {
                        p(center);
                        p(focus);
                        *rx *= k;
                        *ry *= k;
                    }
                    Paint::Conic { center, .. } => p(center),
                    Paint::Solid(_) | Paint::Linear { .. } => {}
                }
            }
            Cmd::Shadow { shape: s, sigma, .. } => {
                shape(s);
                *sigma *= k;
            }
            Cmd::InnerShadow { shape: s, offset, sigma, .. } => {
                shape(s);
                // A distance, not a place.
                *offset = Point::new(offset.x * k, offset.y * k);
                *sigma *= k;
            }
            Cmd::Image { dest, .. } => r(dest),
            _ => {}
        }
    }
}
