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
    assert_eq!(loaded.audible_position, None);
    assert_eq!(loaded.output_latency, None);

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
    assert_ne!(
        player.snapshot().timeline_revision,
        loaded.timeline_revision
    );
    assert_eq!(player.snapshot().audible_position, None);

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
    let stopped = player.snapshot();
    assert_eq!(stopped.state, State::Idle);
    assert_eq!(stopped.audible_position, None);
    assert_ne!(stopped.timeline_revision, loaded.timeline_revision);
}

#[test]
#[ignore = "requires an audio output device"]
fn media_provider_reopens_without_losing_the_track_on_error() {
    use std::io::Cursor;
    use std::sync::{
        Arc,
        atomic::{AtomicBool, AtomicUsize, Ordering},
    };
    use swiftify_native_audio::media::{MediaInput, MediaProvider};

    struct Provider {
        deny: AtomicBool,
        opened: AtomicUsize,
    }
    impl MediaProvider for Provider {
        fn open(&self) -> std::io::Result<MediaInput> {
            if self.deny.load(Ordering::Relaxed) {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::PermissionDenied,
                    "provider-private details",
                ));
            }
            self.opened.fetch_add(1, Ordering::Relaxed);
            let audio = include_bytes!("audio/two-tones.webm");
            Ok(MediaInput::new(
                Cursor::new(audio.as_slice()),
                Some(audio.len() as u64),
                None,
            ))
        }
    }

    let provider = Arc::new(Provider {
        deny: AtomicBool::new(false),
        opened: AtomicUsize::new(0),
    });
    let mut player = Player::open().unwrap();
    player.load_source(provider.clone()).unwrap();
    let loaded = player.snapshot();
    provider.deny.store(true, Ordering::Relaxed);
    assert!(matches!(
        player.seek(Duration::from_millis(400)),
        Err(AudioError::Input(std::io::ErrorKind::PermissionDenied))
    ));
    assert_eq!(player.snapshot(), loaded);
    provider.deny.store(false, Ordering::Relaxed);
    player.seek(Duration::from_millis(800)).unwrap();
    assert_eq!(provider.opened.load(Ordering::Relaxed), 2);
    assert_ne!(
        player.snapshot().timeline_revision,
        loaded.timeline_revision
    );
    player.play().unwrap();
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        let snapshot = player.snapshot();
        if snapshot.state == State::Ended {
            assert!(snapshot.submitted_position >= Duration::from_millis(1150));
            break;
        }
        assert_ne!(snapshot.state, State::SourceFailed);
        assert!(Instant::now() < deadline, "{snapshot:?}");
        thread::sleep(Duration::from_millis(5));
    }
    player.stop().unwrap();
    assert_eq!(player.snapshot().state, State::Idle);
    assert_eq!(Arc::strong_count(&provider), 1);
}

#[test]
#[ignore = "requires an audio output device"]
fn output_reopens_at_the_paused_position_with_gain_preserved() {
    let mut player = Player::open().unwrap();
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/audio/two-tones.opus");
    player.load_file(path).unwrap();
    let position = Duration::from_millis(800);
    player.seek(position).unwrap();
    player.set_volume(0.35).unwrap();
    player.set_muted(true);
    let before = player.snapshot();
    player.switch_output(None).unwrap();
    let after = player.snapshot();
    assert_eq!(after.state, State::Paused);
    assert_eq!(after.submitted_position, position);
    assert_ne!(after.timeline_revision, before.timeline_revision);
    assert_eq!(after.audible_position, None);
    assert_eq!(player.volume(), 0.35);
    assert!(player.is_muted());
    player.play().unwrap();
    player.stop().unwrap();
}
