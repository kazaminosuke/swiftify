// SPDX-License-Identifier: GPL-3.0-or-later

#![cfg(feature = "file-codecs")]

use std::{path::Path, time::Duration};
use swiftify_native_audio::decode::open_file;

#[test]
fn decodes_and_seeks_in_ogg_opus_webm_opus_and_webm_vorbis() {
    for file in ["two-tones.opus", "two-tones.webm", "two-tones-vorbis.webm"] {
        let path = Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("tests/audio")
            .join(file);
        let mut source = open_file(&path).unwrap_or_else(|error| panic!("{file}: {error}"));
        assert_eq!(source.channels(), 1, "{file}");
        assert_eq!(source.sample_rate(), 48_000, "{file}");
        let duration = source.total_duration().expect(file).as_millis();
        assert!(
            (1180..=1230).contains(&duration),
            "{file}: duration was {duration} ms"
        );
        let before = source.by_ref().take(4800).collect::<Vec<_>>();
        assert_tone(&before, 440.0, file);

        source.try_seek(Duration::from_millis(800)).unwrap();
        let after = source.by_ref().take(4800).collect::<Vec<_>>();
        assert_tone(&after, 880.0, file);
        let remaining = source.count();
        assert!(
            (13_900..=15_500).contains(&remaining),
            "{file}: {remaining} frames remaining"
        );
    }
}

#[test]
fn webm_uses_the_selected_track_instead_of_the_segment_length() {
    let file = "short-default-track.webm";
    let path = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/audio")
        .join(file);
    let mut source = open_file(&path).expect(file);
    assert_eq!(source.total_duration(), None);

    let beginning = source.by_ref().take(4800).collect::<Vec<_>>();
    assert_tone(&beginning, 440.0, file);
    let frames = beginning.len() + source.count();
    // Symphonia 0.6.1 does not expose Matroska DiscardPadding. The default
    // stream can therefore contain up to one extra 20 ms Opus packet here.
    assert!(
        (26_640..=27_600).contains(&frames),
        "{file}: {frames} decoded frames"
    );
}

fn assert_tone(samples: &[f32], expected: f32, file: &str) {
    assert_eq!(samples.len(), 4800, "{file}");
    assert!(samples.iter().any(|sample| sample.abs() > 0.03), "{file}");
    let energy_at = |frequency: f32| {
        let mut sine = 0.0;
        let mut cosine = 0.0;
        for (frame, sample) in samples.iter().enumerate() {
            let phase = std::f32::consts::TAU * frequency * frame as f32 / 48_000.0;
            sine += sample * phase.sin();
            cosine += sample * phase.cos();
        }
        sine.powi(2) + cosine.powi(2)
    };
    let other = if expected == 440.0 { 880.0 } else { 440.0 };
    assert!(
        energy_at(expected) > 20.0 * energy_at(other),
        "{file}: expected {expected} Hz, target energy {}, other energy {}",
        energy_at(expected),
        energy_at(other)
    );
}
