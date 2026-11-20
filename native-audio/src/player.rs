// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    collections::VecDeque,
    error::Error,
    fmt,
    fs::{self, File},
    io::{self, BufReader},
    path::{Path, PathBuf},
    sync::{
        Arc, Condvar, Mutex, MutexGuard, PoisonError,
        atomic::{AtomicBool, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use rodio::{
    Decoder, Source,
    cpal::{self, traits::StreamTrait},
    source::UniformSourceIterator,
};

use crate::device::open_stereo_stream;

const CHANNELS: usize = 2;
const DECODE_SAMPLES: usize = 2_048;

type FileDecoder = Decoder<BufReader<File>>;

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
    OutputLost,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Snapshot {
    pub state: State,
    /// Media position submitted to the device, excluding silence from underruns.
    /// The device and operating system can still buffer these samples.
    pub submitted_position: Duration,
    pub duration: Option<Duration>,
}

#[derive(Debug)]
pub enum AudioError {
    File(io::Error),
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
            Self::File(error) => write!(f, "could not open audio file: {error}"),
            Self::Decode(error) => write!(f, "could not decode audio file: {error}"),
            Self::Seek(error) => write!(f, "could not seek in audio file: {error}"),
            Self::DecoderThread(error) => write!(f, "could not start audio decoder: {error}"),
            Self::OpenOutput => f.write_str("could not open the default audio output"),
            Self::PlayOutput(error) => write!(f, "could not start audio output: {error}"),
            Self::PauseOutput(error) => write!(f, "could not pause audio output: {error}"),
            Self::OutputLost => f.write_str("audio output was lost"),
            Self::NoTrack => f.write_str("no audio file is loaded"),
            Self::Ended => f.write_str("seek or load an audio file before playing again"),
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
}

impl Player {
    pub fn open() -> Result<Self, AudioError> {
        let slot = Arc::new(Mutex::new(RenderSlot {
            volume: 1.0,
            ..RenderSlot::default()
        }));
        let output_lost = Arc::new(AtomicBool::new(false));
        let render_slot = Arc::clone(&slot);
        let failed = Arc::clone(&output_lost);
        let (stream, rate) = open_stereo_stream(
            move |output, timing| {
                output.fill(0.0);
                if let Ok(slot) = render_slot.try_lock()
                    && slot.running
                    && let Some(buffer) = &slot.buffer
                {
                    let timestamp = timing.timestamp();
                    let latency = timestamp
                        .playback
                        .duration_since(&timestamp.callback)
                        .unwrap_or_default();
                    let now = Instant::now();
                    let start = now.checked_add(latency).unwrap_or(now);
                    buffer.render(output, slot.volume, start, slot.rate);
                }
            },
            move |_| failed.store(true, Ordering::Release),
        )
        .ok_or(AudioError::OpenOutput)?;
        lock(&slot).rate = rate;

        Ok(Self {
            stream,
            slot,
            output_lost,
            rate,
            track: None,
            paused_at: Some(Instant::now()),
        })
    }

    /// Prepares an audio file and leaves it paused. If opening the new file
    /// fails, the previous track remains active.
    pub fn load_file(&mut self, path: impl AsRef<Path>) -> Result<(), AudioError> {
        self.check_output()?;
        let track = Track::open(path.as_ref(), self.rate, Duration::ZERO)?;
        if self.paused_at.is_none() {
            self.pause()?;
        }
        self.install(track);
        Ok(())
    }

    pub fn play(&mut self) -> Result<(), AudioError> {
        self.check_output()?;
        match self.snapshot().state {
            State::Idle => return Err(AudioError::NoTrack),
            State::OutputLost => return Err(AudioError::OutputLost),
            State::Ended => return Err(AudioError::Ended),
            _ => {}
        }
        if let Some(paused_at) = self.paused_at {
            self.stream.play().map_err(AudioError::PlayOutput)?;
            if let Some(track) = &self.track {
                let mut queue = lock(&track.buffer.queue);
                if let Some(until) = queue.submitted_until
                    && until > paused_at
                {
                    queue.submitted_until = until.checked_add(paused_at.elapsed());
                }
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
        let replacement = Track::open(&track.path, self.rate, position)?;
        self.install(replacement);
        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), AudioError> {
        {
            let mut slot = lock(&self.slot);
            slot.buffer = None;
            slot.running = false;
        }
        self.track = None;
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
                duration: None,
            };
        };
        let queued = lock(&track.buffer.queue);
        let position = track
            .offset
            .saturating_add(frame_time(queued.submitted_frames, self.rate));
        Snapshot {
            state: if output_lost {
                State::OutputLost
            } else {
                queued.state(self.paused_at, Instant::now())
            },
            submitted_position: track
                .duration
                .map_or(position, |duration| position.min(duration)),
            duration: track.duration,
        }
    }

    fn install(&mut self, track: Track) {
        lock(&self.slot).buffer = Some(Arc::clone(&track.buffer));
        // The previous decoder is cancelled and joined on this thread.
        self.track = Some(track);
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
    rate: u32,
    buffer: Option<Arc<Buffer>>,
}

struct Track {
    path: PathBuf,
    duration: Option<Duration>,
    offset: Duration,
    buffer: Arc<Buffer>,
    decoder: Option<JoinHandle<()>>,
}

impl Track {
    fn open(path: &Path, rate: u32, offset: Duration) -> Result<Self, AudioError> {
        let mut decoder = open_decoder(path)?;
        let duration = decoder.total_duration();
        if !offset.is_zero() {
            decoder.try_seek(offset).map_err(AudioError::Seek)?;
        }

        let source = UniformSourceIterator::new(decoder, CHANNELS as u16, rate);
        let capacity = (rate as usize * CHANNELS / 4).max(DECODE_SAMPLES * 2);
        let buffer = Arc::new(Buffer::new(capacity));
        let output = Arc::clone(&buffer);
        let decoder = thread::Builder::new()
            .name("swiftify-native-decoder".to_owned())
            .spawn(move || output.decode(source, capacity))
            .map_err(AudioError::DecoderThread)?;

        Ok(Self {
            path: path.to_owned(),
            duration,
            offset,
            buffer,
            decoder: Some(decoder),
        })
    }
}

impl Drop for Track {
    fn drop(&mut self) {
        lock(&self.buffer.queue).cancelled = true;
        self.buffer.space.notify_one();
        if let Some(decoder) = self.decoder.take() {
            let _ = decoder.join();
        }
    }
}

fn open_decoder(path: &Path) -> Result<FileDecoder, AudioError> {
    let metadata = fs::metadata(path).map_err(AudioError::File)?;
    if !metadata.is_file() {
        return Err(AudioError::File(io::Error::new(
            io::ErrorKind::InvalidInput,
            "expected a regular file",
        )));
    }
    let file = File::open(path).map_err(AudioError::File)?;
    let length = file.metadata().map_err(AudioError::File)?.len();
    let mut builder = Decoder::builder()
        .with_data(BufReader::new(file))
        .with_seekable(true)
        .with_byte_len(length);
    if let Some(extension) = path.extension().and_then(|extension| extension.to_str()) {
        builder = builder.with_hint(extension);
    }
    builder.build().map_err(AudioError::Decode)
}

struct Buffer {
    queue: Mutex<Queue>,
    space: Condvar,
}

struct Queue {
    samples: VecDeque<f32>,
    finished: bool,
    cancelled: bool,
    starved: bool,
    submitted_frames: u64,
    submitted_until: Option<Instant>,
}

impl Queue {
    fn state(&self, paused_at: Option<Instant>, now: Instant) -> State {
        let exhausted = self.finished && self.samples.is_empty();
        if exhausted
            && self
                .submitted_until
                .is_none_or(|end| end <= paused_at.unwrap_or(now))
        {
            State::Ended
        } else if paused_at.is_some() {
            State::Paused
        } else if exhausted {
            State::Draining
        } else if self.starved {
            State::Buffering
        } else {
            State::Playing
        }
    }
}

impl Buffer {
    fn new(capacity: usize) -> Self {
        Self {
            queue: Mutex::new(Queue {
                samples: VecDeque::with_capacity(capacity),
                finished: false,
                cancelled: false,
                starved: true,
                submitted_frames: 0,
                submitted_until: None,
            }),
            space: Condvar::new(),
        }
    }

    fn decode(&self, mut source: impl Iterator<Item = f32>, capacity: usize) {
        let mut decoded = [0.0; DECODE_SAMPLES];
        loop {
            let queue = self
                .space
                .wait_while(lock(&self.queue), |q| {
                    !q.cancelled && q.samples.len() + DECODE_SAMPLES > capacity
                })
                .unwrap_or_else(PoisonError::into_inner);
            if queue.cancelled {
                return;
            }
            drop(queue);

            let mut samples = 0;
            for frame in decoded.chunks_exact_mut(CHANNELS) {
                let Some(left) = source.next() else { break };
                let Some(right) = source.next() else { break };
                frame.copy_from_slice(&[left, right]);
                samples += CHANNELS;
            }

            let mut queue = lock(&self.queue);
            if queue.cancelled {
                return;
            }
            queue.samples.extend(decoded[..samples].iter().copied());
            if samples < DECODE_SAMPLES {
                queue.finished = true;
                return;
            }
        }
    }

    /// Called with a zeroed stereo output slice. A busy queue leaves silence,
    /// and never makes the device callback wait for the decoder.
    fn render(&self, output: &mut [f32], volume: f32, start: Instant, rate: u32) {
        let Ok(mut queue) = self.queue.try_lock() else {
            return;
        };
        let count = output.len().min(queue.samples.len()) / CHANNELS * CHANNELS;
        for (out, sample) in output.iter_mut().zip(queue.samples.drain(..count)) {
            *out = sample * volume;
        }
        if count != 0 {
            let frames = (count / CHANNELS) as u64;
            queue.submitted_frames = queue.submitted_frames.saturating_add(frames);
            queue.submitted_until = start.checked_add(frame_time(frames, rate));
        }
        queue.starved = count < output.len() && !queue.finished;
        drop(queue);
        if count != 0 {
            self.space.notify_one();
        }
    }
}

fn frame_time(frames: u64, rate: u32) -> Duration {
    let rate = u64::from(rate);
    Duration::from_secs(frames / rate)
        + Duration::from_nanos((frames % rate) * 1_000_000_000 / rate)
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(PoisonError::into_inner)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn silence_does_not_advance_the_submitted_clock() {
        let buffer = Buffer::new(DECODE_SAMPLES * 2);
        lock(&buffer.queue).samples.extend([0.5, -0.25, 1.0, -1.0]);
        let mut output = [0.0; 8];

        buffer.render(&mut output, 0.5, Instant::now(), 48_000);
        assert_eq!(output, [0.25, -0.125, 0.5, -0.5, 0.0, 0.0, 0.0, 0.0]);
        let queue = lock(&buffer.queue);
        assert!(queue.starved);
        assert_eq!(queue.submitted_frames, 2);
    }

    #[test]
    fn audio_callback_does_not_wait_for_a_busy_queue() {
        let buffer = Buffer::new(DECODE_SAMPLES * 2);
        let mut queue = lock(&buffer.queue);
        queue.samples.extend([1.0, 1.0]);
        let mut output = [0.0; 2];

        buffer.render(&mut output, 1.0, Instant::now(), 48_000);
        assert_eq!(output, [0.0; 2]);
        assert_eq!(queue.submitted_frames, 0);
        assert_eq!(queue.samples.len(), 2);
    }

    #[test]
    fn end_waits_for_the_last_submitted_frame() {
        let buffer = Buffer::new(DECODE_SAMPLES * 2);
        {
            let mut queue = lock(&buffer.queue);
            queue.samples.extend([1.0, -1.0]);
            queue.finished = true;
        }
        let start = Instant::now() + Duration::from_millis(40);
        buffer.render(&mut [0.0; 2], 1.0, start, 48_000);

        let queue = lock(&buffer.queue);
        let end = start + frame_time(1, 48_000);
        assert_eq!(queue.state(None, start), State::Draining);
        assert_eq!(queue.state(Some(start), end), State::Paused);
        assert_eq!(queue.state(None, end), State::Ended);
    }

    #[test]
    fn converts_submitted_frames_without_millisecond_rounding() {
        assert_eq!(
            frame_time(48_001, 48_000),
            Duration::from_nanos(1_000_020_833)
        );
    }
}
