// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    error::Error,
    fmt, io,
    path::Path,
    sync::{
        Arc, Mutex, MutexGuard, PoisonError,
        atomic::{AtomicBool, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use rodio::{
    Source,
    cpal::{self, traits::StreamTrait},
    source::UniformSourceIterator,
};

use crate::{
    decode::open_media,
    device::{OutputDevice, open_device_stream, open_stereo_stream},
    media::{FileMedia, MediaCancellation, MediaProvider, ReadFailure},
    render::{PcmFrame, RenderQueue, frame_time},
};

const CHANNELS: usize = 2;

/// Engine states reported by [Player::snapshot].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum State {
    Idle,
    Paused,
    Buffering,
    Playing,
    /// The last samples were submitted and their output time has not elapsed.
    Draining,
    Ended,
    SourceFailed,
    OutputLost,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Snapshot {
    pub state: State,
    /// Media position submitted to the device, excluding silence from underruns.
    /// The device and operating system can still buffer these samples.
    pub submitted_position: Duration,
    /// Estimated media position at the device clock, excluding underruns.
    /// Unavailable before the first presentation anchor of this revision or
    /// when the backend does not provide a usable estimate.
    pub audible_position: Option<Duration>,
    /// Last reported callback-to-playback latency for this revision.
    /// It may omit latency downstream of the backend, such as Bluetooth.
    pub output_latency: Option<Duration>,
    /// Changes after a successful load, seek or stop. Invalidate old cues.
    pub timeline_revision: u64,
    pub duration: Option<Duration>,
    pub input_error: Option<io::ErrorKind>,
}

#[derive(Debug)]
pub enum AudioError {
    Input(io::ErrorKind),
    InputRead(io::ErrorKind),
    InputSeek(io::ErrorKind),
    Decode(rodio::decoder::DecoderError),
    Seek(rodio::source::SeekError),
    DecoderThread(io::Error),
    OpenOutput,
    PlayOutput(cpal::PlayStreamError),
    PauseOutput(cpal::PauseStreamError),
    OutputLost,
    NoTrack,
    Ended,
    InvalidVolume,
}

impl fmt::Display for AudioError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Input(kind) => write!(f, "could not open audio input: {kind}"),
            Self::InputRead(kind) => write!(f, "could not read audio input: {kind}"),
            Self::InputSeek(kind) => write!(f, "could not reposition audio input: {kind}"),
            Self::Decode(error) => write!(f, "could not decode audio file: {error}"),
            Self::Seek(error) => write!(f, "could not seek in audio file: {error}"),
            Self::DecoderThread(error) => write!(f, "could not start audio decoder: {error}"),
            Self::OpenOutput => f.write_str("could not open the audio output"),
            Self::PlayOutput(error) => write!(f, "could not start audio output: {error}"),
            Self::PauseOutput(error) => write!(f, "could not pause audio output: {error}"),
            Self::OutputLost => f.write_str("audio output was lost"),
            Self::NoTrack => f.write_str("no audio input is loaded"),
            Self::Ended => f.write_str("seek or load an audio input before playing again"),
            Self::InvalidVolume => f.write_str("volume must be between 0 and 1"),
        }
    }
}

impl Error for AudioError {}

/// Native playback of caller-supplied media. The player owns its output stream
/// and is controlled from one thread. File opening and seeking can block.
pub struct Player {
    stream: cpal::Stream,
    slot: Arc<Mutex<RenderSlot>>,
    output_lost: Arc<AtomicBool>,
    rate: u32,
    track: Option<Track>,
    paused_at: Option<Instant>,
    revision: u64,
}

impl Player {
    pub fn open() -> Result<Self, AudioError> {
        Self::open_output(None)
    }

    pub fn open_output(device: Option<&OutputDevice>) -> Result<Self, AudioError> {
        let slot = Arc::new(Mutex::new(RenderSlot {
            volume: 1.0,
            ..RenderSlot::default()
        }));
        let output_lost = Arc::new(AtomicBool::new(false));
        let render_slot = Arc::clone(&slot);
        let failed = Arc::clone(&output_lost);
        let fill = move |output: &mut [f32], timing: &cpal::OutputCallbackInfo| {
            output.fill(0.0);
            if let Ok(slot) = render_slot.try_lock()
                && slot.running
                && let Some(buffer) = &slot.buffer
            {
                let timestamp = timing.timestamp();
                let latency = timestamp.playback.duration_since(&timestamp.callback);
                let gain = if slot.muted { 0.0 } else { slot.volume };
                buffer.render(output, gain, Instant::now(), latency);
            }
        };
        let error = move |_| failed.store(true, Ordering::Release);
        let (stream, rate) = match device {
            Some(device) => open_device_stream(&device.cpal, fill, error),
            None => open_stereo_stream(fill, error),
        }
        .ok_or(AudioError::OpenOutput)?;

        Ok(Self {
            stream,
            slot,
            output_lost,
            rate,
            track: None,
            paused_at: Some(Instant::now()),
            revision: 0,
        })
    }

    /// Switches the output on the control thread. `None` resolves the current
    /// default again, including after an output disconnect. Existing media is
    /// reopened near the last presentation position, or the submitted position
    /// if the device no longer has a usable clock. A completed track becomes idle.
    pub fn switch_output(&mut self, device: Option<&OutputDevice>) -> Result<(), AudioError> {
        let mut replacement = Self::open_output(device)?;
        replacement.revision = self.revision;
        replacement.set_volume(self.volume())?;
        replacement.set_muted(self.is_muted());
        let was_running = self.paused_at.is_none();
        let restore_old = was_running && !self.output_lost.load(Ordering::Acquire);
        if restore_old {
            self.pause()?;
        }
        let snapshot = self.snapshot();
        let replace = (|| {
            if let Some(track) = &self.track
                && !matches!(snapshot.state, State::Ended | State::SourceFailed)
            {
                let position = snapshot
                    .audible_position
                    .unwrap_or(snapshot.submitted_position);
                let track = Track::open(Arc::clone(&track.provider), replacement.rate, position)?;
                replacement.install(track);
                if was_running {
                    replacement.play()?;
                }
            } else if self.track.is_some() {
                replacement.revision = self.revision.wrapping_add(1);
            }
            Ok(())
        })();
        if let Err(error) = replace {
            if restore_old && self.play().is_err() {
                self.output_lost.store(true, Ordering::Release);
            }
            return Err(error);
        }
        *self = replacement;
        Ok(())
    }

    /// Prepares an audio file and leaves it paused. If opening the new file
    /// fails, the previous track remains active.
    pub fn load_file(&mut self, path: impl AsRef<Path>) -> Result<(), AudioError> {
        self.load_source(Arc::new(FileMedia(path.as_ref().to_owned())))
    }

    /// Prepares a source on the control thread and leaves it paused. A failed
    /// open or decode keeps the existing track and its generation intact.
    pub fn load_source(&mut self, provider: Arc<dyn MediaProvider>) -> Result<(), AudioError> {
        self.check_output()?;
        let track = Track::open(provider, self.rate, Duration::ZERO)?;
        if self.paused_at.is_none() {
            self.pause()?;
        }
        self.install(track);
        Ok(())
    }

    pub fn play(&mut self) -> Result<(), AudioError> {
        self.check_output()?;
        let snapshot = self.snapshot();
        match snapshot.state {
            State::Idle => return Err(AudioError::NoTrack),
            State::OutputLost => return Err(AudioError::OutputLost),
            State::Ended => return Err(AudioError::Ended),
            State::SourceFailed => {
                return Err(AudioError::InputRead(
                    snapshot.input_error.unwrap_or(io::ErrorKind::Other),
                ));
            }
            _ => {}
        }
        if let Some(paused_at) = self.paused_at {
            self.stream.play().map_err(AudioError::PlayOutput)?;
            if let Some(track) = &self.track {
                track.buffer.resume(paused_at, Instant::now());
            }
            lock(&self.slot).running = true;
            self.paused_at = None;
        }
        Ok(())
    }

    pub fn pause(&mut self) -> Result<(), AudioError> {
        self.check_output()?;
        if self.paused_at.is_none() {
            self.stream.pause().map_err(AudioError::PauseOutput)?;
            lock(&self.slot).running = false;
            self.paused_at = Some(Instant::now());
        }
        Ok(())
    }

    /// Seeks on a fresh decoder before replacing the active source. This
    /// preserves the playing/paused state and leaves the track intact on error.
    pub fn seek(&mut self, position: Duration) -> Result<(), AudioError> {
        self.check_output()?;
        let track = self.track.as_ref().ok_or(AudioError::NoTrack)?;
        let position = track
            .duration
            .map_or(position, |duration| position.min(duration));
        let replacement = Track::open(Arc::clone(&track.provider), self.rate, position)?;
        self.install(replacement);
        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), AudioError> {
        {
            let mut slot = lock(&self.slot);
            slot.buffer = None;
            slot.running = false;
        }
        if self.track.take().is_some() {
            self.revision = self.revision.wrapping_add(1);
        }
        self.paused_at = Some(Instant::now());
        self.stream.pause().map_err(AudioError::PauseOutput)
    }

    pub fn set_volume(&mut self, volume: f32) -> Result<(), AudioError> {
        if !(0.0..=1.0).contains(&volume) {
            return Err(AudioError::InvalidVolume);
        }
        lock(&self.slot).volume = volume;
        Ok(())
    }

    pub fn volume(&self) -> f32 {
        lock(&self.slot).volume
    }

    pub fn set_muted(&mut self, muted: bool) {
        lock(&self.slot).muted = muted;
    }

    pub fn is_muted(&self) -> bool {
        lock(&self.slot).muted
    }

    pub fn snapshot(&self) -> Snapshot {
        let output_lost = self.output_lost.load(Ordering::Acquire);
        let Some(track) = &self.track else {
            return Snapshot {
                state: if output_lost {
                    State::OutputLost
                } else {
                    State::Idle
                },
                submitted_position: Duration::ZERO,
                audible_position: None,
                output_latency: None,
                timeline_revision: self.revision,
                duration: None,
                input_error: None,
            };
        };
        let queued = track.buffer.position(self.paused_at);
        let clamp = |position: Duration| {
            track
                .duration
                .map_or(position, |duration| position.min(duration))
        };
        let state = if output_lost {
            State::OutputLost
        } else if queued.drained {
            if queued.input_error.is_some() {
                State::SourceFailed
            } else {
                State::Ended
            }
        } else if self.paused_at.is_some() {
            State::Paused
        } else if queued.exhausted {
            State::Draining
        } else if queued.starved {
            State::Buffering
        } else {
            State::Playing
        };
        Snapshot {
            state,
            submitted_position: clamp(queued.submitted),
            audible_position: (!output_lost)
                .then_some(queued.audible)
                .flatten()
                .map(clamp),
            output_latency: (!output_lost).then_some(queued.latency).flatten(),
            timeline_revision: self.revision,
            duration: track.duration,
            input_error: queued.input_error,
        }
    }

    fn install(&mut self, track: Track) {
        lock(&self.slot).buffer = Some(Arc::clone(&track.buffer));
        // The previous decoder is cancelled and joined on this thread.
        self.track = Some(track);
        self.revision = self.revision.wrapping_add(1);
    }

    fn check_output(&self) -> Result<(), AudioError> {
        if self.output_lost.load(Ordering::Acquire) {
            Err(AudioError::OutputLost)
        } else {
            Ok(())
        }
    }
}

#[derive(Default)]
struct RenderSlot {
    running: bool,
    volume: f32,
    muted: bool,
    buffer: Option<Arc<RenderQueue>>,
}

struct Track {
    provider: Arc<dyn MediaProvider>,
    cancellation: MediaCancellation,
    duration: Option<Duration>,
    buffer: Arc<RenderQueue>,
    decoder: Option<JoinHandle<()>>,
}

impl Track {
    fn open(
        provider: Arc<dyn MediaProvider>,
        rate: u32,
        offset: Duration,
    ) -> Result<Self, AudioError> {
        let cancellation = MediaCancellation::default();
        let input = provider
            .open_cancellable(cancellation.clone())
            .map_err(|error| AudioError::Input(error.kind()))?
            .with_cancellation(cancellation.clone());
        let failure = input.failure.clone();
        let seek_failure = input.seek_failure.clone();
        let mut decoder = open_media(input).map_err(|error| {
            input_failure(&failure, &seek_failure).unwrap_or(AudioError::Decode(error))
        })?;
        if let Some(kind) = failure.kind() {
            return Err(AudioError::InputRead(kind));
        }
        let duration = decoder.total_duration();
        if !offset.is_zero() {
            seek_failure.clear();
            decoder.try_seek(offset).map_err(|error| {
                input_failure(&failure, &seek_failure).unwrap_or(AudioError::Seek(error))
            })?;
            if let Some(kind) = failure.kind() {
                return Err(AudioError::InputRead(kind));
            }
        }
        let mut source = UniformSourceIterator::new(decoder, CHANNELS as u16, rate);
        let buffer = Arc::new(RenderQueue::new(rate, rate as usize / 4, offset));
        let output = Arc::clone(&buffer);
        let decoder = thread::Builder::new()
            .name("swiftify-native-decoder".to_owned())
            .spawn(move || {
                let mut frames = 0;
                output.produce(
                    |target| {
                        let mut count = 0;
                        for frame in target {
                            let Some(left) = source.next() else { break };
                            let Some(right) = source.next() else { break };
                            frames += 1;
                            *frame = PcmFrame {
                                stereo: [left, right],
                                media_end: offset.saturating_add(frame_time(frames, rate)),
                            };
                            count += 1;
                        }
                        count
                    },
                    || failure.kind(),
                );
            })
            .map_err(AudioError::DecoderThread)?;

        Ok(Self {
            provider,
            cancellation,
            duration,
            buffer,
            decoder: Some(decoder),
        })
    }
}

impl Drop for Track {
    fn drop(&mut self) {
        self.cancellation.cancel();
        self.buffer.cancel();
        if let Some(decoder) = self.decoder.take() {
            let _ = decoder.join();
        }
    }
}

fn input_failure(read: &ReadFailure, seek: &ReadFailure) -> Option<AudioError> {
    read.kind()
        .map(AudioError::InputRead)
        .or_else(|| seek.kind().map(AudioError::InputSeek))
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(PoisonError::into_inner)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dropping_a_track_cancels_a_blocked_provider_read() {
        use crate::media::MediaInput;
        use std::{
            io::{Cursor, Read, Seek, SeekFrom},
            sync::mpsc::{self, SyncSender},
        };

        struct Provider {
            audio: Vec<u8>,
            started: SyncSender<()>,
            observed: Arc<AtomicBool>,
        }

        struct Reader {
            inner: Cursor<Vec<u8>>,
            owner: thread::ThreadId,
            started: SyncSender<()>,
            cancellation: MediaCancellation,
            observed: Arc<AtomicBool>,
        }

        impl Read for Reader {
            fn read(&mut self, output: &mut [u8]) -> io::Result<usize> {
                if thread::current().id() == self.owner {
                    return self.inner.read(output);
                }
                self.started.try_send(()).ok();
                let deadline = Instant::now() + Duration::from_secs(3);
                while !self.cancellation.is_cancelled() && Instant::now() < deadline {
                    thread::sleep(Duration::from_millis(1));
                }
                self.observed
                    .store(self.cancellation.is_cancelled(), Ordering::Release);
                Err(io::ErrorKind::ConnectionAborted.into())
            }
        }

        impl Seek for Reader {
            fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
                self.inner.seek(position)
            }
        }

        impl MediaProvider for Provider {
            fn open(&self) -> io::Result<MediaInput> {
                unreachable!("the decoder must pass the cancellation token")
            }

            fn open_cancellable(&self, cancellation: MediaCancellation) -> io::Result<MediaInput> {
                let reader = Reader {
                    inner: Cursor::new(self.audio.clone()),
                    owner: thread::current().id(),
                    started: self.started.clone(),
                    cancellation,
                    observed: Arc::clone(&self.observed),
                };
                Ok(MediaInput::new(reader, Some(self.audio.len() as u64), None))
            }
        }

        let spec = hound::WavSpec {
            channels: 1,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: hound::SampleFormat::Int,
        };
        let mut audio = Cursor::new(Vec::new());
        let mut wav = hound::WavWriter::new(&mut audio, spec).unwrap();
        for _ in 0..48_000 {
            wav.write_sample(0i16).unwrap();
        }
        wav.finalize().unwrap();
        let observed = Arc::new(AtomicBool::new(false));
        let (started, waiting) = mpsc::sync_channel(1);
        let provider = Arc::new(Provider {
            audio: audio.into_inner(),
            started,
            observed: Arc::clone(&observed),
        });
        let track = Track::open(provider, 48_000, Duration::ZERO).unwrap();
        waiting.recv_timeout(Duration::from_secs(2)).unwrap();
        drop(track);
        assert!(observed.load(Ordering::Acquire));
    }

    #[test]
    fn distinguishes_input_seek_from_input_read_failure() {
        let read = ReadFailure::default();
        let seek = ReadFailure::default();
        assert!(input_failure(&read, &seek).is_none());
        seek.record(io::ErrorKind::PermissionDenied);
        assert!(matches!(
            input_failure(&read, &seek),
            Some(AudioError::InputSeek(io::ErrorKind::PermissionDenied))
        ));
        read.record(io::ErrorKind::TimedOut);
        assert!(matches!(
            input_failure(&read, &seek),
            Some(AudioError::InputRead(io::ErrorKind::TimedOut))
        ));
    }
}
