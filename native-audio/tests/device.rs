// SPDX-License-Identifier: GPL-3.0-or-later

#![cfg(feature = "player")]

use std::{
    f32::consts::TAU,
    fs, thread,
    time::{Duration, Instant},
};

use swiftify_native_audio::{AudioError, Player, State};

/// Explicitly select a test device when running this in a headless environment.
/// CI uses ALSA's null device, so decoding and the device callback still run.
#[test]
#[ignore = "requires an audio output device"]
fn decodes_seeks_and_drains_through_the_audio_device() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("tone.wav");
    let spec = hound::WavSpec {
        channels: 1,
        sample_rate: 16_000,
        bits_per_sample: 16,
        sample_format: hound::SampleFormat::Int,
    };
    let mut wav = hound::WavWriter::create(&path, spec).unwrap();
    for frame in 0..32_000 {
        let angle = TAU * 440.0 * frame as f32 / spec.sample_rate as f32;
        wav.write_sample((angle.sin() * 4_000.0) as i16).unwrap();
    }
    wav.finalize().unwrap();

    let mut player = Player::open().unwrap();
    assert_eq!(player.snapshot().state, State::Idle);
    assert!(matches!(player.play(), Err(AudioError::NoTrack)));
    player.load_file(&path).unwrap();
    let loaded = player.snapshot();
    assert_eq!(loaded.state, State::Paused);
    assert_eq!(loaded.duration, Some(Duration::from_secs(2)));
    assert_eq!(loaded.submitted_position, Duration::ZERO);

    let invalid = directory.path().join("invalid.wav");
    fs::write(&invalid, b"not an audio file").unwrap();
    assert!(matches!(
        player.load_file(invalid),
        Err(AudioError::Decode(_))
    ));
    assert_eq!(player.snapshot(), loaded);

    let seek = Duration::from_millis(500);
    player.seek(seek).unwrap();
    assert_eq!(player.snapshot().state, State::Paused);
    assert_eq!(player.snapshot().submitted_position, seek);

    player.set_volume(0.25).unwrap();
    player.play().unwrap();
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut previous = seek;
    loop {
        let snapshot = player.snapshot();
        assert!(snapshot.submitted_position >= previous);
        assert_ne!(snapshot.state, State::OutputLost);
        if snapshot.state == State::Ended {
            assert!(
                (Duration::from_millis(1_990)..=Duration::from_secs(2))
                    .contains(&snapshot.submitted_position)
            );
            break;
        }
        previous = snapshot.submitted_position;
        assert!(Instant::now() < deadline, "{snapshot:?}");
        thread::sleep(Duration::from_millis(5));
    }
    assert!(matches!(player.play(), Err(AudioError::Ended)));
    player.stop().unwrap();
    assert_eq!(player.snapshot().state, State::Idle);
}
