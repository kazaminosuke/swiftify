// SPDX-License-Identifier: GPL-3.0-or-later

//! The bounded PCM handoff between a decode/DSP worker and a device callback.

use std::{
    collections::VecDeque,
    io,
    sync::{Condvar, Mutex, MutexGuard, PoisonError},
    time::{Duration, Instant},
};

const CLOCK_SPANS: usize = 64;
const WORK_FRAMES: usize = 512;

#[derive(Clone, Copy, Default)]
pub struct PcmFrame {
    pub stereo: [f32; 2],
    /// Position in the recording immediately after this frame.
    pub media_end: Duration,
}

pub struct RenderQueue {
    rate: u32,
    capacity: usize,
    state: Mutex<QueueState>,
    wake: Condvar,
}

struct QueueState {
    frames: VecDeque<PcmFrame>,
    finished: bool,
    cancelled: bool,
    starved: bool,
    input_error: Option<io::ErrorKind>,
    clock: PresentationClock,
}

#[derive(Clone, Copy)]
pub struct RenderPosition {
    pub submitted: Duration,
    pub audible: Option<Duration>,
    pub latency: Option<Duration>,
    pub exhausted: bool,
    pub drained: bool,
    pub starved: bool,
    pub input_error: Option<io::ErrorKind>,
}

impl RenderQueue {
    pub fn new(rate: u32, capacity_frames: usize, from: Duration) -> Self {
        assert!(rate > 0);
        let capacity = capacity_frames.max(2 * WORK_FRAMES);
        Self {
            rate,
            capacity,
            state: Mutex::new(QueueState {
                frames: VecDeque::with_capacity(capacity),
                finished: false,
                cancelled: false,
                starved: true,
                input_error: None,
                clock: PresentationClock::new(from),
            }),
            wake: Condvar::new(),
        }
    }

    /// Runs on a worker. The source fills a block, or returns a shorter block
    /// at EOF. It may block or allocate without tying up the audio callback.
    /// Each queue has one producer. Frames carry monotonic media positions.
    pub fn produce(
        &self,
        mut read: impl FnMut(&mut [PcmFrame]) -> usize,
        terminal_error: impl FnOnce() -> Option<io::ErrorKind>,
    ) {
        let mut decoded = [PcmFrame::default(); WORK_FRAMES];
        loop {
            let mut state = lock(&self.state);
            while !state.cancelled && state.frames.len() + WORK_FRAMES > self.capacity {
                // The device callback does not wake or wait on an OS primitive.
                (state, _) = self
                    .wake
                    .wait_timeout(state, Duration::from_millis(3))
                    .unwrap_or_else(PoisonError::into_inner);
            }
            if state.cancelled {
                return;
            }
            drop(state);

            let count = read(&mut decoded);
            let mut state = lock(&self.state);
            if state.cancelled {
                return;
            }
            state.frames.extend(decoded[..count].iter().copied());
            if count < decoded.len() {
                state.finished = true;
                state.input_error = terminal_error();
                return;
            }
        }
    }

    pub fn cancel(&self) {
        lock(&self.state).cancelled = true;
        self.wake.notify_one();
    }

    pub fn resume(&self, paused_at: Instant, now: Instant) {
        lock(&self.state).clock.resume(paused_at, now);
    }

    pub fn position(&self, paused_at: Option<Instant>) -> RenderPosition {
        let state = lock(&self.state);
        let at = paused_at.unwrap_or_else(Instant::now);
        let exhausted = state.finished && state.frames.is_empty();
        RenderPosition {
            submitted: state.clock.submitted,
            audible: state.clock.audible(at),
            latency: state.clock.latency,
            exhausted,
            drained: exhausted && state.clock.spans.back().is_none_or(|span| span.end <= at),
            starved: state.starved,
            input_error: state.input_error,
        }
    }

    /// Copies into an already silent stereo buffer. Contention produces silence
    /// without advancing media time. This path neither allocates nor waits.
    pub fn render(&self, output: &mut [f32], gain: f32, at: Instant, latency: Option<Duration>) {
        let Ok(mut state) = self.state.try_lock() else {
            return;
        };
        let (output, _) = output.as_chunks_mut::<2>();
        let count = output.len().min(state.frames.len());
        let mut media_end = None;
        for (device, frame) in output.iter_mut().zip(state.frames.drain(..count)) {
            let [left, right] = frame.stereo;
            *device = if gain == 0.0 {
                [0.0; 2]
            } else {
                [left * gain, right * gain]
            };
            media_end = Some(frame.media_end);
        }
        if let Some(media_end) = media_end {
            state
                .clock
                .submit(count as u64, media_end, at, latency, self.rate);
        }
        state.starved = count < output.len() && !state.finished;
    }
}

struct PresentationClock {
    submitted: Duration,
    latency: Option<Duration>,
    spans: VecDeque<Span>,
}

struct Span {
    from: Duration,
    to: Duration,
    start: Instant,
    end: Instant,
    estimated: bool,
}

impl PresentationClock {
    fn new(from: Duration) -> Self {
        Self {
            submitted: from,
            latency: None,
            spans: VecDeque::with_capacity(CLOCK_SPANS),
        }
    }

    fn submit(
        &mut self,
        frames: u64,
        media_end: Duration,
        at: Instant,
        latency: Option<Duration>,
        rate: u32,
    ) {
        let estimate = latency.and_then(|delay| at.checked_add(delay));
        let proposed = estimate.unwrap_or(at);
        // Backend estimates can jitter. Adjacent buffers play in queue order.
        let start = self
            .spans
            .back()
            .map_or(proposed, |span| span.end.max(proposed));
        let end = start.checked_add(frame_time(frames, rate)).unwrap_or(start);
        if self.spans.len() == CLOCK_SPANS {
            self.spans.pop_front();
        }
        let to = self.submitted.max(media_end);
        self.spans.push_back(Span {
            from: self.submitted,
            to,
            start,
            end,
            estimated: estimate.is_some(),
        });
        self.submitted = to;
        self.latency = latency.filter(|_| estimate.is_some());
    }

    fn audible(&self, at: Instant) -> Option<Duration> {
        let span = self.spans.iter().rev().find(|span| span.start <= at)?;
        if !span.estimated {
            return None;
        }
        if at >= span.end {
            return Some(span.to);
        }
        let elapsed = at.duration_since(span.start).as_secs_f64();
        let total = span.end.duration_since(span.start).as_secs_f64();
        Some(span.from + (span.to - span.from).mul_f64(elapsed / total))
    }

    fn resume(&mut self, paused_at: Instant, resumed_at: Instant) {
        let paused_for = resumed_at.saturating_duration_since(paused_at);
        for span in self
            .spans
            .iter_mut()
            .rev()
            .take_while(|span| span.end > paused_at)
        {
            if let (Some(start), Some(end)) = (
                span.start.checked_add(paused_for),
                span.end.checked_add(paused_for),
            ) {
                span.start = start;
                span.end = end;
            }
        }
    }
}

pub fn frame_time(frames: u64, rate: u32) -> Duration {
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
    fn callback_uses_committed_frames_and_leaves_underrun_as_silence() {
        let buffer = RenderQueue::new(1_000, 512, Duration::from_millis(500));
        buffer.produce(
            |pcm| {
                pcm[0] = PcmFrame {
                    stereo: [0.5, -0.5],
                    media_end: Duration::from_millis(502),
                };
                1
            },
            || None,
        );

        let mut output = [0.0; 4];
        let now = Instant::now();
        {
            let _busy = lock(&buffer.state);
            buffer.render(&mut output, 1.0, now, Some(Duration::ZERO));
            assert_eq!(output, [0.0; 4]);
        }
        buffer.render(&mut output, 1.0, now, Some(Duration::from_millis(10)));
        assert_eq!(output, [0.5, -0.5, 0.0, 0.0]);
        let queued = buffer.position(Some(now));
        assert_eq!(queued.submitted, Duration::from_millis(502));
        assert_eq!(queued.audible, None);
        assert!(queued.exhausted && !queued.drained);
        let played = buffer.position(Some(now + Duration::from_millis(11)));
        assert_eq!(played.audible, Some(Duration::from_millis(502)));
        assert!(played.drained);
    }

    #[test]
    fn clock_maps_speed_changes_and_pauses_without_advancing_on_silence() {
        let now = Instant::now();
        let mut clock = PresentationClock::new(Duration::from_millis(300));
        clock.submit(
            20,
            Duration::from_millis(340),
            now,
            Some(Duration::from_millis(10)),
            1_000,
        );
        let paused = now + Duration::from_millis(15);
        let resumed = now + Duration::from_millis(60);
        assert_eq!(clock.audible(paused), Some(Duration::from_millis(310)));
        clock.resume(paused, resumed);
        assert_eq!(clock.audible(resumed), Some(Duration::from_millis(310)));
        clock.submit(
            20,
            Duration::from_millis(350),
            now + Duration::from_millis(90),
            Some(Duration::ZERO),
            1_000,
        );
        assert_eq!(
            clock.audible(now + Duration::from_millis(80)),
            Some(Duration::from_millis(340))
        );
        assert_eq!(
            clock.audible(now + Duration::from_millis(100)),
            Some(Duration::from_millis(345))
        );
    }

    #[test]
    fn muting_consumes_frames_and_masks_nonfinite_pcm() {
        let buffer = RenderQueue::new(48_000, 512, Duration::ZERO);
        buffer.produce(
            |out| {
                out[0] = PcmFrame {
                    stereo: [0.5, f32::NAN],
                    media_end: frame_time(1, 48_000),
                };
                1
            },
            || Some(io::ErrorKind::TimedOut),
        );
        let mut output = [1.0; 2];
        buffer.render(&mut output, 0.0, Instant::now(), None);
        assert_eq!(output, [0.0; 2]);
        let snapshot = buffer.position(None);
        assert_eq!(snapshot.submitted, Duration::from_nanos(20_833));
        assert_eq!(snapshot.input_error, Some(io::ErrorKind::TimedOut));
    }
}
