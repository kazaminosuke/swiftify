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

use rodio::cpal::{
    self,
    traits::{DeviceTrait, HostTrait, StreamTrait},
};

/// a stream on the default device, stopped until it is played, that plays the
/// interleaved stereo `fill` writes. a device not of two channels or not of
/// floats gets the same stereo, mixed down or padded, in its own format.
pub fn open_stereo_stream<F, E>(fill: F, error: E) -> Option<(cpal::Stream, u32)>
where
    F: FnMut(&mut [f32], &cpal::OutputCallbackInfo) + Clone + Send + 'static,
    E: FnMut(cpal::StreamError) + Clone + Send + 'static,
{
    let device = cpal::default_host().default_output_device()?;
    open_device_stream(&device, fill, error)
}

pub(crate) fn open_device_stream<F, E>(
    device: &cpal::Device,
    fill: F,
    error: E,
) -> Option<(cpal::Stream, u32)>
where
    F: FnMut(&mut [f32], &cpal::OutputCallbackInfo) + Clone + Send + 'static,
    E: FnMut(cpal::StreamError) + Clone + Send + 'static,
{
    let supported = device.default_output_config().ok()?;
    let config: cpal::StreamConfig = supported.config();
    let rate = config.sample_rate.0;
    // a thousand frames a callback rather than the device's usual 512: the
    // system's work for each callback is most of what the stream costs, and
    // at about 21 ms it is still no longer than an analysis hop, so the
    // visualizers are fed as evenly as before. a device that will not take
    // that is opened as it comes
    let larger = match *supported.buffer_size() {
        cpal::SupportedBufferSize::Range { min, max } => Some(cpal::StreamConfig {
            buffer_size: cpal::BufferSize::Fixed(1_024_u32.clamp(min, max)),
            ..config.clone()
        }),
        cpal::SupportedBufferSize::Unknown => None,
    };
    let stream = larger
        .and_then(|larger| {
            build_stereo_stream(
                device,
                supported.sample_format(),
                &larger,
                fill.clone(),
                error.clone(),
            )
        })
        .or_else(|| build_stereo_stream(device, supported.sample_format(), &config, fill, error))?;
    stream.pause().ok()?;
    Some((stream, rate))
}

/// An output discovered on the current host. Keep the handle for selection;
/// display names can be repeated or change after device reconfiguration.
#[cfg(feature = "player")]
pub struct OutputDevice {
    pub(crate) cpal: cpal::Device,
    name: String,
}

#[cfg(feature = "player")]
impl OutputDevice {
    pub fn name(&self) -> &str {
        &self.name
    }
}

#[cfg(feature = "player")]
pub fn output_devices() -> Result<Vec<OutputDevice>, cpal::DevicesError> {
    Ok(cpal::default_host()
        .output_devices()?
        .filter_map(|cpal| {
            let name = cpal.name().ok()?;
            Some(OutputDevice { cpal, name })
        })
        .collect())
}

fn build_stereo_stream<F, E>(
    device: &cpal::Device,
    format: cpal::SampleFormat,
    config: &cpal::StreamConfig,
    fill: F,
    error: E,
) -> Option<cpal::Stream>
where
    F: FnMut(&mut [f32], &cpal::OutputCallbackInfo) + Send + 'static,
    E: FnMut(cpal::StreamError) + Send + 'static,
{
    match (format, config.channels) {
        (cpal::SampleFormat::F32, 2) => {
            let mut fill = fill;
            device
                .build_output_stream::<f32, _, _>(
                    config,
                    move |data, timing| fill(data, timing),
                    error,
                    None,
                )
                .ok()
        }
        (cpal::SampleFormat::F32, _) => mapped_stream::<f32, F, E>(device, config, fill, error),
        (cpal::SampleFormat::I16, _) => mapped_stream::<i16, F, E>(device, config, fill, error),
        (cpal::SampleFormat::I32, _) => mapped_stream::<i32, F, E>(device, config, fill, error),
        (cpal::SampleFormat::U16, _) => mapped_stream::<u16, F, E>(device, config, fill, error),
        (cpal::SampleFormat::F64, _) => mapped_stream::<f64, F, E>(device, config, fill, error),
        _ => None,
    }
}

fn mapped_stream<T, F, E>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
    mut fill: F,
    error: E,
) -> Option<cpal::Stream>
where
    T: cpal::SizedSample + cpal::FromSample<f32>,
    F: FnMut(&mut [f32], &cpal::OutputCallbackInfo) + Send + 'static,
    E: FnMut(cpal::StreamError) + Send + 'static,
{
    let channels = usize::from(config.channels);
    const SCRATCH_FRAMES: usize = 2_048;
    let mut stereo = vec![0.0; SCRATCH_FRAMES * 2].into_boxed_slice();
    device
        .build_output_stream::<T, _, _>(
            config,
            move |data: &mut [T], timing| {
                for chunk in data.chunks_mut(SCRATCH_FRAMES * channels) {
                    let frames = chunk.len() / channels;
                    let scratch = &mut stereo[..frames * 2];
                    fill(scratch, timing);
                    for (out, pair) in chunk
                        .chunks_exact_mut(channels)
                        .zip(scratch.chunks_exact(2))
                    {
                        let left = pair[0];
                        let right = pair[1];
                        if channels == 1 {
                            out[0] = T::from_sample((left + right) * 0.5);
                        } else {
                            out[0] = T::from_sample(left);
                            out[1] = T::from_sample(right);
                            out[2..].fill(T::from_sample(0.0));
                        }
                    }
                }
            },
            error,
            None,
        )
        .ok()
}
