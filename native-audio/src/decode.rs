// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    fs::{self, File},
    io::{self, BufReader, Read, Seek},
    path::Path,
    time::Duration,
};

use rodio::{Decoder, Source, decoder::DecoderError, source::SeekError};
use symphonia::core::{
    codecs::audio::{AudioDecoder, AudioDecoderOptions, well_known::CODEC_ID_OPUS},
    errors::Error,
    formats::{FormatOptions, FormatReader, SeekMode, SeekTo, TrackType, probe::Hint},
    io::MediaSourceStream,
    units::{Time, TimeBase},
};
use symphonia_opus::OpusDecoder;

pub type FileSource = Box<dyn Source + Send>;

/// Opens a regular file for both Qt's existing DSP path and the native engine.
/// Read the container signature so an Opus stream also works with an `.ogg`
/// name or without a file extension.
pub fn open_file(path: &Path) -> Result<FileSource, DecoderError> {
    if !fs::metadata(path).map_err(file_error)?.is_file() {
        return Err(DecoderError::IoError("expected a regular file".to_owned()));
    }
    let mut file = File::open(path).map_err(file_error)?;
    let metadata = file.metadata().map_err(file_error)?;
    let mut signature = [0; 4];
    if file.read_exact(&mut signature).is_ok() {
        file.rewind().map_err(file_error)?;
        if signature == [0x1a, 0x45, 0xdf, 0xa3] || &signature == b"OggS" {
            let stream = MediaSourceStream::new(
                Box::new(file.try_clone().map_err(file_error)?),
                Default::default(),
            );
            match OpusSource::open(stream) {
                Ok(Some(source)) => return Ok(Box::new(source)),
                Err(error) if signature != *b"OggS" => {
                    return Err(DecoderError::IoError(error.to_string()));
                }
                _ => file.rewind().map_err(file_error)?,
            }
        }
    } else {
        file.rewind().map_err(file_error)?;
    }

    let mut builder = Decoder::builder()
        .with_data(BufReader::new(file))
        .with_seekable(true)
        .with_byte_len(metadata.len());
    if let Some(extension) = path.extension().and_then(|extension| extension.to_str()) {
        builder = builder.with_hint(extension);
    }
    Ok(Box::new(builder.build()?))
}

fn file_error(error: io::Error) -> DecoderError {
    DecoderError::IoError(error.to_string())
}

struct OpusSource {
    format: Box<dyn FormatReader>,
    decoder: OpusDecoder,
    track: u32,
    rate: u32,
    channels: u16,
    time_base: TimeBase,
    duration: Option<Duration>,
    pcm: Vec<f32>,
    cursor: usize,
    target_ns: i128,
    ended: bool,
}

impl OpusSource {
    fn open(stream: MediaSourceStream<'static>) -> Result<Option<Self>, Error> {
        let format = symphonia::default::get_probe().probe(
            &Hint::new(),
            stream,
            FormatOptions::default(),
            Default::default(),
        )?;
        let track = format
            .default_track(TrackType::Audio)
            .ok_or(Error::Unsupported("no audio track"))?;
        let params = track
            .codec_params
            .as_ref()
            .and_then(|params| params.audio())
            .ok_or(Error::Unsupported("audio parameters unavailable"))?;
        if params.codec != CODEC_ID_OPUS {
            return Ok(None);
        }
        let time_base = track
            .time_base
            .ok_or(Error::Unsupported("no Opus time base"))?;
        let decoder = OpusDecoder::try_new(params, &AudioDecoderOptions::default())?;
        let decoded = decoder.codec_params();
        let rate = decoded
            .sample_rate
            .ok_or(Error::Unsupported("no Opus sample rate"))?;
        let channels = decoded
            .channels
            .as_ref()
            .ok_or(Error::Unsupported("no Opus channels"))?
            .count() as u16;
        let duration = track
            .duration
            .and_then(|d| time_base.calc_duration(d))
            .or_else(|| {
                // WebM puts the playback duration on the Segment, not on
                // its track. Use it when there is only one media track.
                if format.tracks().len() != 1 {
                    return None;
                }
                let media = format.media_info();
                media.time_base?.calc_duration(media.duration?)
            })
            .and_then(|time| Duration::try_from_secs_f64(time.as_secs_f64()).ok());
        let mut source = Self {
            track: track.id,
            format,
            decoder,
            rate,
            channels,
            time_base,
            duration,
            pcm: Vec::new(),
            cursor: 0,
            target_ns: 0,
            ended: false,
        };
        source.fill()?;
        if source.ended {
            return Err(Error::DecodeError("empty Opus track"));
        }
        Ok(Some(source))
    }

    fn frames(&self, nanos: i128) -> usize {
        // Container ticks need not be frames. Round away jitter from tick quantization.
        ((nanos.max(0) * i128::from(self.rate) + 500_000_000) / 1_000_000_000)
            .min(usize::MAX as i128) as usize
    }

    fn fill(&mut self) -> Result<(), Error> {
        while !self.ended && self.cursor >= self.pcm.len() {
            let Some(packet) = self.format.next_packet()? else {
                self.ended = true;
                break;
            };
            if packet.track_id != self.track {
                continue;
            }
            let timestamp = self
                .time_base
                .calc_time(packet.pts)
                .ok_or(Error::DecodeError("Opus packet timestamp out of range"))?
                .as_nanos();
            let skip = self.frames(self.target_ns - timestamp).max(
                self.frames(
                    self.time_base
                        .calc_duration_saturating(packet.trim_start)
                        .as_nanos(),
                ),
            );
            let padding = self.frames(
                self.time_base
                    .calc_duration_saturating(packet.trim_end)
                    .as_nanos(),
            );
            let audio = self.decoder.decode(&packet)?;
            let frames = audio.frames().saturating_sub(padding);
            if skip >= frames {
                continue;
            }
            self.pcm.clear();
            audio.copy_to_vec_interleaved(&mut self.pcm);
            self.pcm.truncate(frames * usize::from(self.channels));
            self.cursor = skip * usize::from(self.channels);
        }
        Ok(())
    }
}

impl Iterator for OpusSource {
    type Item = f32;

    fn next(&mut self) -> Option<Self::Item> {
        if self.fill().is_err() {
            self.ended = true;
            return None;
        }
        let sample = *self.pcm.get(self.cursor)?;
        self.cursor += 1;
        Some(sample)
    }
}

impl Source for OpusSource {
    fn current_span_len(&self) -> Option<usize> {
        None
    }
    fn channels(&self) -> u16 {
        self.channels
    }
    fn sample_rate(&self) -> u32 {
        self.rate
    }
    fn total_duration(&self) -> Option<Duration> {
        self.duration
    }

    fn try_seek(&mut self, position: Duration) -> Result<(), SeekError> {
        let target = self
            .duration
            .map_or(position, |duration| position.min(duration));
        let preroll = target.saturating_sub(Duration::from_millis(80));
        let time = Time::try_from_nanos_u128(preroll.as_nanos())
            .ok_or_else(|| seek_error("Opus seek time out of range"))?;
        self.format
            .seek(
                SeekMode::Accurate,
                SeekTo::Time {
                    time,
                    track_id: Some(self.track),
                },
            )
            .map_err(seek_error)?;
        self.decoder.reset();
        self.target_ns = target.as_nanos() as i128;
        self.pcm.clear();
        self.cursor = 0;
        self.ended = false;
        self.fill().map_err(seek_error)
    }
}

fn seek_error(error: impl ToString) -> SeekError {
    SeekError::Other(Box::new(io::Error::other(error.to_string())))
}
