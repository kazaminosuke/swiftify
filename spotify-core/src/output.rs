/*
 * Swiftify, a spotify player for desktop
 * Copyright (C) 2026  shred0s
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//! Desktop CPAL outputs for Spotify streams and buffered local/live sources.

use std::{
    sync::{
        Arc, Mutex, MutexGuard, PoisonError,
        atomic::{AtomicBool, AtomicU32, Ordering},
        mpsc,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use rodio::{
    Source,
    cpal::{self, traits::StreamTrait},
};
use swiftify_native_audio::render::{PcmFrame, RenderPosition, RenderQueue, frame_time};

pub(crate) fn open_stereo_stream<F>(mut fill: F) -> Option<(rodio::cpal::Stream, u32)>
where
    F: FnMut(&mut [f32]) + Clone + Send + 'static,
{
    swiftify_native_audio::device::open_stereo_stream(move |output, _| fill(output), |_| {})
}

/// a source the device reads a block at a time
pub(crate) trait BlockSource: Iterator<Item = f32> + Send {
    /// interleaved samples, as many as fit; fewer only at its end
    fn read(&mut self, out: &mut [f32]) -> usize {
        let mut count = 0;
        while count < out.len() {
            let Some(sample) = self.next() else {
                break;
            };
            out[count] = sample;
            count += 1;
        }
        count
    }
}

impl BlockSource for std::vec::IntoIter<f32> {}

/// Owns the device on its control thread. Local decoding, buffering and DSP
/// run on a source worker, with the same PCM handoff as the native player.
pub(crate) struct LocalOutput {
    shared: Arc<LocalShared>,
    commands: mpsc::Sender<bool>,
    rate: u32,
}

struct LocalShared {
    slot: Mutex<Slot>,
    volume: AtomicU32,
    lost: AtomicBool,
}

struct Slot {
    source: Option<OutputSource>,
    paused_at: Option<Instant>,
    reported_end: bool,
}

pub(crate) struct OutputSource {
    pub(crate) queue: Arc<RenderQueue>,
    worker: Option<JoinHandle<()>>,
}

impl Drop for OutputSource {
    fn drop(&mut self) {
        self.queue.cancel();
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

impl LocalShared {
    fn slot(&self) -> MutexGuard<'_, Slot> {
        self.slot.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn fill(&self, out: &mut [f32], timing: &cpal::OutputCallbackInfo) {
        out.fill(0.0);
        if self.lost.load(Ordering::Acquire) {
            return;
        }
        if let Ok(slot) = self.slot.try_lock()
            && slot.paused_at.is_none()
            && let Some(source) = &slot.source
        {
            let volume = f32::from_bits(self.volume.load(Ordering::Relaxed));
            let timestamp = timing.timestamp();
            let latency = timestamp.playback.duration_since(&timestamp.callback);
            source.queue.render(out, volume, Instant::now(), latency);
        }
    }
}

impl LocalOutput {
    pub(crate) fn open() -> Option<Self> {
        let shared = Arc::new(LocalShared {
            volume: AtomicU32::new(1.0_f32.to_bits()),
            lost: AtomicBool::new(false),
            slot: Mutex::new(Slot {
                source: None,
                paused_at: Some(Instant::now()),
                reported_end: false,
            }),
        });
        let (commands, received) = mpsc::channel::<bool>();
        let (opened, opening) = mpsc::sync_channel(1);
        let filled = Arc::clone(&shared);
        let failed = Arc::clone(&shared);
        let controlled = Arc::clone(&shared);
        thread::Builder::new()
            .name("swiftify-local-output".to_owned())
            .spawn(move || {
                let Some((stream, rate)) = swiftify_native_audio::device::open_stereo_stream(
                    move |out, timing| filled.fill(out, timing),
                    move |_| failed.lost.store(true, Ordering::Release),
                ) else {
                    let _ = opened.send(None);
                    return;
                };
                let _ = opened.send(Some(rate));
                while let Ok(play) = received.recv() {
                    let stopped = if play {
                        stream.play().is_err()
                    } else {
                        stream.pause().is_err()
                    };
                    if stopped {
                        controlled.lost.store(true, Ordering::Release);
                    }
                }
            })
            .ok()?;
        let rate = opening.recv().ok()??;
        Some(Self {
            shared,
            commands,
            rate,
        })
    }

    fn command(&self, slot: &mut Slot, play: bool) {
        let now = Instant::now();
        if play {
            if let Some(paused) = slot.paused_at.take()
                && let Some(source) = &slot.source
            {
                source.queue.resume(paused, now);
            }
        } else {
            slot.paused_at.get_or_insert(now);
        }
        if self.commands.send(play).is_err() {
            self.shared.lost.store(true, Ordering::Release);
        }
    }

    pub(crate) fn prepare(
        &self,
        offset: Duration,
        capacity_frames: usize,
        read: impl FnMut(&mut [PcmFrame]) -> usize + Send + 'static,
    ) -> Result<OutputSource, String> {
        let queue = Arc::new(RenderQueue::new(self.rate, capacity_frames, offset));
        let producer = Arc::clone(&queue);
        let worker = thread::Builder::new()
            .name("swiftify-local-pcm".to_owned())
            .spawn(move || producer.produce(read, || None))
            .map_err(|err| format!("could not start audio worker: {err}"))?;
        Ok(OutputSource {
            queue,
            worker: Some(worker),
        })
    }

    pub(crate) fn install(&self, source: OutputSource) -> Option<OutputSource> {
        let mut slot = self.shared.slot();
        slot.reported_end = false;
        if let Some(paused) = &mut slot.paused_at {
            *paused = Instant::now();
        }
        slot.source.replace(source)
    }

    pub(crate) fn append<S>(&self, source: S) -> Result<(), String>
    where
        S: Source<Item = f32> + BlockSource + 'static,
    {
        let channels = usize::from(source.channels()).max(1);
        let rate = self.rate;
        let from = source.sample_rate().max(1);
        let mut source = LocalStream::new(Box::new(source), channels, from, rate);
        let mut frame_index = 0;
        let mut output_samples = [0.0; 1_024];
        let prepared = self.prepare(Duration::ZERO, 1_024, move |out| {
            let sample_count = source.fill(&mut output_samples[..out.len() * 2], 1.0);
            let frames = &output_samples[..sample_count].as_chunks::<2>().0;
            for (target, samples) in out.iter_mut().zip(frames.iter()) {
                frame_index += 1;
                *target = PcmFrame {
                    stereo: *samples,
                    media_end: frame_time(frame_index, rate),
                };
            }
            frames.len()
        })?;
        drop(self.install(prepared));
        Ok(())
    }

    pub(crate) fn stop(&self) {
        let previous = {
            let mut slot = self.shared.slot();
            self.command(&mut slot, false);
            slot.source.take()
        };
        drop(previous);
    }

    pub(crate) fn play(&self) {
        self.command(&mut self.shared.slot(), true);
    }

    pub(crate) fn pause(&self) {
        self.command(&mut self.shared.slot(), false);
    }

    pub(crate) fn rate(&self) -> u32 {
        self.rate
    }

    pub(crate) fn is_lost(&self) -> bool {
        self.shared.lost.load(Ordering::Acquire)
    }

    pub(crate) fn set_volume(&self, volume: f32) {
        self.shared
            .volume
            .store(volume.to_bits(), Ordering::Relaxed);
    }

    pub(crate) fn empty(&self) -> bool {
        let slot = self.shared.slot();
        slot.source
            .as_ref()
            .is_none_or(|source| source.queue.position(slot.paused_at).drained)
    }

    pub(crate) fn position(&self, queue: &Arc<RenderQueue>) -> Option<RenderPosition> {
        let slot = self.shared.slot();
        let source = slot.source.as_ref()?;
        Arc::ptr_eq(queue, &source.queue).then(|| queue.position(slot.paused_at))
    }

    pub(crate) fn take_drained(&self, queue: &Arc<RenderQueue>) -> bool {
        let mut slot = self.shared.slot();
        if self.is_lost()
            || slot.reported_end
            || slot
                .source
                .as_ref()
                .is_none_or(|source| !Arc::ptr_eq(queue, &source.queue))
            || !queue.position(slot.paused_at).drained
        {
            return false;
        }
        slot.reported_end = true;
        self.command(&mut slot, false);
        true
    }
}

/// a source brought to the device: its channels to two, its rate to the
/// device's, by linear interpolation
struct LocalStream {
    source: Box<dyn BlockSource>,
    /// what was read from the source and not yet played, `buffered` of it
    /// from `taken`
    buffer: Box<[f32]>,
    taken: usize,
    buffered: usize,
    drained: bool,
    channels: usize,
    /// source frames a device frame steps over
    step: f64,
    /// how far from `previous` toward `upcoming` the next frame falls
    position: f64,
    previous: [f32; 2],
    upcoming: Option<[f32; 2]>,
    primed: bool,
    ended: bool,
}

impl LocalStream {
    /// read from the source at a time
    const BLOCK: usize = 4_096;

    fn new(source: Box<dyn BlockSource>, channels: usize, from: u32, to: u32) -> Self {
        Self {
            source,
            buffer: vec![0.0; Self::BLOCK].into_boxed_slice(),
            taken: 0,
            buffered: 0,
            drained: false,
            channels,
            step: f64::from(from) / f64::from(to),
            position: 0.0,
            previous: [0.0; 2],
            upcoming: None,
            primed: false,
            ended: false,
        }
    }

    #[cfg(test)]
    fn ended(&self) -> bool {
        self.ended
    }

    fn next_sample(&mut self) -> Option<f32> {
        if self.taken == self.buffered {
            if self.drained {
                return None;
            }
            self.buffered = self.source.read(&mut self.buffer);
            self.taken = 0;
            self.drained = self.buffered < self.buffer.len();
            if self.buffered == 0 {
                return None;
            }
        }
        let sample = self.buffer[self.taken];
        self.taken += 1;
        Some(sample)
    }

    /// a frame of the source, as stereo
    fn read(&mut self) -> Option<[f32; 2]> {
        let left = self.next_sample()?;
        if self.channels == 1 {
            return Some([left, left]);
        }
        let right = self.next_sample()?;
        for _ in 2..self.channels {
            self.next_sample();
        }
        Some([left, right])
    }

    fn next_frame(&mut self) -> Option<[f32; 2]> {
        if self.ended {
            return None;
        }
        if self.step == 1.0 {
            let frame = self.read();
            self.ended = frame.is_none();
            return frame;
        }
        if !self.primed {
            self.primed = true;
            let Some(first) = self.read() else {
                self.ended = true;
                return None;
            };
            self.previous = first;
            self.upcoming = self.read();
        }
        let frame = match self.upcoming {
            Some(upcoming) => {
                let fraction = self.position as f32;
                [
                    self.previous[0] + (upcoming[0] - self.previous[0]) * fraction,
                    self.previous[1] + (upcoming[1] - self.previous[1]) * fraction,
                ]
            }
            // the source's last frame, once
            None if self.position == 0.0 => self.previous,
            None => {
                self.ended = true;
                return None;
            }
        };
        self.position += self.step;
        while self.position >= 1.0 {
            let Some(upcoming) = self.upcoming else {
                self.ended = true;
                break;
            };
            self.previous = upcoming;
            self.upcoming = self.read();
            self.position -= 1.0;
        }
        Some(frame)
    }

    /// stereo at the device's rate, as it comes: copied a run at a time
    fn copy(&mut self, out: &mut [f32], volume: f32) -> usize {
        let mut written = 0;
        while written < out.len() {
            if self.taken == self.buffered && self.next_sample().map(|_| self.taken -= 1).is_none()
            {
                self.ended = true;
                break;
            }
            let run = (out.len() - written).min(self.buffered - self.taken);
            for (to, from) in out[written..written + run]
                .iter_mut()
                .zip(&self.buffer[self.taken..self.taken + run])
            {
                *to = from * volume;
            }
            self.taken += run;
            written += run;
        }
        written
    }

    /// interleaved stereo, as much as the source has left; how many samples
    fn fill(&mut self, out: &mut [f32], volume: f32) -> usize {
        if self.step == 1.0 && self.channels == 2 && !self.ended {
            return self.copy(out, volume);
        }
        let mut written = 0;
        while written + 2 <= out.len() {
            let Some([left, right]) = self.next_frame() else {
                break;
            };
            out[written] = left * volume;
            out[written + 1] = right * volume;
            written += 2;
        }
        written
    }
}

#[cfg(test)]
mod tests {
    use super::LocalStream;

    fn stream(samples: Vec<f32>, channels: usize, from: u32, to: u32) -> LocalStream {
        LocalStream::new(Box::new(samples.into_iter()), channels, from, to)
    }

    fn drain(stream: &mut LocalStream) -> Vec<f32> {
        let mut out = Vec::new();
        let mut block = [0.0_f32; 64];
        loop {
            let written = stream.fill(&mut block, 1.0);
            out.extend_from_slice(&block[..written]);
            if written < block.len() {
                return out;
            }
        }
    }

    #[test]
    fn a_source_at_the_devices_rate_plays_as_it_is() {
        let samples: Vec<f32> = (0..200).map(|index| index as f32).collect();
        let mut played = stream(samples.clone(), 2, 48_000, 48_000);
        assert_eq!(drain(&mut played), samples);
        assert!(played.ended());
    }

    #[test]
    fn mono_plays_on_both_sides() {
        let mut played = stream(vec![0.25, 0.5], 1, 44_100, 44_100);
        assert_eq!(drain(&mut played), vec![0.25, 0.25, 0.5, 0.5]);
    }

    #[test]
    fn a_ramp_stays_a_ramp_at_another_rate() {
        // a ramp in frames, 44.1 khz to 48: every frame played lies on it,
        // and they run from its first frame to its last, as many as the
        // ratio says
        let frames = 4_410;
        let samples: Vec<f32> = (0..frames)
            .flat_map(|index| [index as f32, -(index as f32)])
            .collect();
        let played = drain(&mut stream(samples, 2, 44_100, 48_000));
        let count = played.len() / 2;
        assert!((count as i64 - 4_800).abs() <= 1, "{count} frames");
        let step = 44_100.0 / 48_000.0;
        for (index, frame) in played.as_chunks::<2>().0.iter().enumerate() {
            let expected = index as f32 * step;
            assert!(
                (frame[0] - expected).abs() < 1e-2,
                "frame {index}: {} for {expected}",
                frame[0]
            );
            assert!((frame[1] + expected).abs() < 1e-2);
        }
    }

    #[test]
    fn surround_keeps_its_front_pair() {
        let mut played = stream(vec![1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 6, 48_000, 48_000);
        assert_eq!(drain(&mut played), vec![1.0, 2.0]);
    }
}
