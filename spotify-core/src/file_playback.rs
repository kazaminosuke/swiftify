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

//! Local PCM preparation. The decoder and DSP live on the producer thread,
//! and every device frame carries its position in the original recording.

use std::{
    collections::VecDeque,
    path::PathBuf,
    sync::{Arc, atomic::{AtomicBool, Ordering}},
    time::Duration,
};

use rodio::source::UniformSourceIterator;
use swiftify_native_audio::{decode::FileSource, render::{PcmFrame, RenderQueue}};

use crate::dsp::{DspProcessor, SharedDsp};
use crate::spectrum::{ANALYSIS_SAMPLE_COUNT, SharedSpectrum};

#[derive(Clone)]
pub(crate) struct LocalPlayback {
    pub(crate) path: PathBuf,
    pub(crate) duration_ms: u64,
    pub(crate) queue: Arc<RenderQueue>,
    pub(crate) base_position_ms: u64,
    pub(crate) replay_gain: f32,
    pub(crate) revision: u64,
}

const DSP_BLOCK: usize = 4_096;

pub(crate) struct LocalTrackSource {
    inner: UniformSourceIterator<FileSource>,
    spectrum: SharedSpectrum,
    sample_rate: u32,
    offset: Duration,
    decoded_frames: u64,
    media_frames: f64,
    block_tempo: f64,
    replay_gain: f32,
    normalise: Arc<AtomicBool>,
    analysis_samples: [f32; ANALYSIS_SAMPLE_COUNT],
    analysis_index: usize,
    analysis_enabled: bool,
    dsp: DspProcessor,
    processed: VecDeque<f32>,
    drained: bool,
}

impl LocalTrackSource {
    pub(crate) fn new(
        decoder: FileSource,
        spectrum: SharedSpectrum,
        sample_rate: u32,
        offset: Duration,
        dsp: SharedDsp,
        replay_gain: f32,
        normalise: Arc<AtomicBool>,
    ) -> Self {
        Self {
            inner: UniformSourceIterator::new(decoder, 2, sample_rate),
            spectrum,
            sample_rate,
            offset,
            decoded_frames: 0,
            media_frames: 0.0,
            block_tempo: 1.0,
            replay_gain,
            normalise,
            analysis_samples: [0.0; ANALYSIS_SAMPLE_COUNT],
            analysis_index: 0,
            analysis_enabled: false,
            dsp: DspProcessor::new(dsp, sample_rate, 2),
            processed: VecDeque::new(),
            drained: false,
        }
    }

    fn fill(&mut self) {
        while self.processed.is_empty() && !self.drained {
            let mut block: Vec<f32> = self.inner.by_ref().take(DSP_BLOCK).collect();
            // UniformSourceIterator normally has complete stereo frames.
            block.truncate(block.len() / 2 * 2);
            if block.is_empty() {
                self.drained = true;
                let outstanding = ((self.decoded_frames as f64 - self.media_frames)
                    / self.block_tempo).round() as usize;
                self.processed.extend(self.dsp.finish(outstanding.min(self.sample_rate as usize / 4)));
                return;
            }
            self.decoded_frames += (block.len() / 2) as u64;
            if self.normalise.load(Ordering::Relaxed) && (self.replay_gain - 1.0).abs() > 1e-4 {
                for sample in &mut block {
                    *sample *= self.replay_gain;
                }
            }
            self.processed.extend(self.dsp.process(block));
            self.block_tempo = f64::from(self.dsp.tempo());
        }
    }

    pub(crate) fn read(&mut self, output: &mut [PcmFrame]) -> usize {
        let mut count = 0;
        for target in output {
            if self.processed.is_empty() {
                self.fill();
            }
            let Some(left) = self.processed.pop_front() else { break };
            let Some(right) = self.processed.pop_front() else { break };
            self.media_frames = (self.media_frames + self.block_tempo).min(self.decoded_frames as f64);
            let time = Duration::from_secs_f64(self.media_frames / f64::from(self.sample_rate));
            *target = PcmFrame {
                stereo: [left, right],
                media_end: self.offset.saturating_add(time),
            };
            count += 1;
            self.analyse(&[left, right]);
        }
        count
    }

    /// hands the analysis what is about to play, a run at a time
    fn analyse(&mut self, mut samples: &[f32]) {
        while !samples.is_empty() {
            if self.analysis_index == 0 {
                self.analysis_enabled = self.spectrum.is_enabled();
            }
            let taken = samples
                .len()
                .min(ANALYSIS_SAMPLE_COUNT - self.analysis_index);
            if self.analysis_enabled {
                self.analysis_samples[self.analysis_index..self.analysis_index + taken]
                    .copy_from_slice(&samples[..taken]);
            }
            self.analysis_index += taken;
            samples = &samples[taken..];
            if self.analysis_index == ANALYSIS_SAMPLE_COUNT {
                if self.analysis_enabled {
                    self.spectrum
                        .queue_interleaved_f32(self.analysis_samples, 1.0);
                }
                self.analysis_index = 0;
            }
        }
    }
}
