//! The factory programs of the 1.2 release (Params.cpp `factoryPresets`). A program sets channel A:
//! every channel A control it does not name is at its default, and the setup and channel B are left
//! alone (those rows are `PRESET_EXCLUDE`).

use std::sync::OnceLock;

use crate::params::{defaults, index_of, COUNT};

pub struct Program {
    pub name: &'static str,
    values: &'static [(&'static str, f32)],
}

pub static PROGRAMS: [Program; 6] = [
    Program { name: "Init", values: &[] },
    Program {
        name: "Vocal, warm and close",
        values: &[
            ("gain", 0.3), ("lowcut", 0.35), ("platehigh", 0.0), ("threshold", 0.22), ("ratio", 0.35), ("attack", 0.55), ("release", 0.55),
            ("high", 0.62), ("hmlevel", 0.58), ("hmfreq", 0.55), ("lmlevel", 0.46), ("lmfreq", 0.4),
        ],
    },
    Program {
        name: "Bass guitar, DI",
        values: &[
            ("instrument", 1.0), ("gain", 0.25), ("lowcut", 0.1), ("threshold", 0.2), ("ratio", 0.5), ("attack", 0.6), ("release", 0.45),
            ("low", 0.58), ("lmlevel", 0.55), ("lmfreq", 0.5), ("hmlevel", 0.55), ("hmfreq", 0.4), ("hmx10", 0.0),
        ],
    },
    Program {
        name: "Drum bus, glue",
        values: &[("gain", 0.1), ("threshold", 0.18), ("ratio", 0.22), ("attack", 0.7), ("release", 0.35), ("autodetect", 1.0), ("low", 0.56), ("high", 0.58)],
    },
    Program { name: "Tube drive, plate low", values: &[("gain", 0.4), ("gain20", 1.0), ("platehigh", 0.0), ("preout", 0.5), ("comp", 0.0)] },
    Program { name: "Limiter, infinity", values: &[("gain", 0.2), ("threshold", 0.3), ("ratio", 1.0), ("attack", 0.15), ("release", 0.4)] },
];

/// Every parameter's value for program `index`, in table order.
pub fn values(index: usize) -> Option<&'static [f32]> {
    static TABLES: OnceLock<Vec<[f32; COUNT]>> = OnceLock::new();
    let tables = TABLES.get_or_init(|| {
        PROGRAMS
            .iter()
            .map(|p| {
                let mut v = defaults();
                for (id, value) in p.values {
                    v[index_of(id).expect("a program names a parameter that does not exist")] = *value;
                }
                v
            })
            .collect()
    });
    tables.get(index).map(|t| &t[..])
}
