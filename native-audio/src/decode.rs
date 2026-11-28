// SPDX-License-Identifier: GPL-3.0-or-later

use std::{
    io::{self, BufReader, Read, Seek},
    path::Path,
    time::Duration,
};

use rodio::{Decoder, Source, decoder::DecoderError, source::SeekError};
use symphonia::core::{
    codecs::audio::{
        AudioDecoder, AudioDecoderOptions,
        well_known::{CODEC_ID_OPUS, CODEC_ID_VORBIS},
    },
    errors::Error,
    formats::{FormatOptions, FormatReader, SeekMode, SeekTo, TrackType, probe::Hint},
    io::MediaSourceStream,
    units::{Time, TimeBase},
};
use symphonia_opus::OpusDecoder;

use crate::media::{MediaInput, ReadFailure, open_regular_file};

pub type FileSource = Box<dyn Source + Send>;

/// Opens a regular file for both Qt's existing DSP path and the native engine.
pub fn open_file(path: &Path) -> Result<FileSource, DecoderError> {
    open_media(open_regular_file(path).map_err(file_error)?)
}

pub fn open_media(mut input: MediaInput) -> Result<FileSource, DecoderError> {
    // The first Ogg packet begins after the complete lacing table. Read only
    // enough of the stream to recognize an Opus identification packet.
    const OGG_HEADER: usize = 27;
    let mut header = [0; OGG_HEADER + 255 + 8];
    let mut read = 0;
    while read < header.len() {
        let count = match input.read(&mut header[read..]) {
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            result => result.map_err(file_error)?,
        };
        if count == 0 {
            break;
        }
        read += count;
    }
    input.rewind().map_err(file_error)?;
    let header = &header[..read];
    let ogg_opus = header.starts_with(b"OggS")
        && header.get(26).is_some_and(|segments| {
            let start = OGG_HEADER + usize::from(*segments);
            header.get(start..start + 8) == Some(b"OpusHead")
        });
    if header.starts_with(&[0x1a, 0x45, 0xdf, 0xa3]) || ogg_opus {
        let failure = input.failure.clone();
        let stream = MediaSourceStream::new(Box::new(input), Default::default());
        return DemuxedSource::open(stream, failure)
            .map(|source| Box::new(source) as FileSource)
            .map_err(demux_error);
    }

    let length = input.byte_len;
    let extension = input.extension.take();
    let mut builder = Decoder::builder()
        .with_data(BufReader::new(input))
        .with_seekable(true);
    if let Some(length) = length {
        builder = builder.with_byte_len(length);
    }
    if let Some(extension) = extension {
        builder = builder.with_hint(&extension);
    }
    Ok(Box::new(builder.build()?))
}

fn file_error(error: io::Error) -> DecoderError {
    DecoderError::IoError(error.kind().to_string())
}

fn demux_error(error: Error) -> DecoderError {
    match error {
        Error::IoError(error) => file_error(error),
        Error::DecodeError(message) => DecoderError::DecodeError(message),
        Error::LimitError(message) => DecoderError::LimitError(message),
        Error::ResetRequired => DecoderError::ResetRequired,
        Error::SeekError(_) => DecoderError::DecodeError("could not position the audio stream"),
        Error::Unsupported(_) => DecoderError::UnrecognizedFormat,
        _ => DecoderError::DecodeError("could not decode the audio stream"),
    }
}

struct DemuxedSource {
    format: Box<dyn FormatReader>,
    decoder: Box<dyn AudioDecoder>,
    seek_preroll: Duration,
    track: u32,
    rate: u32,
    channels: u16,
    time_base: TimeBase,
    duration: Option<Duration>,
    pcm: Vec<f32>,
    cursor: usize,
    target_ns: i128,
    ended: bool,
    failure: ReadFailure,
}

impl DemuxedSource {
    fn open(stream: MediaSourceStream<'static>, failure: ReadFailure) -> Result<Self, Error> {
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
        let options = AudioDecoderOptions::default();
        let (decoder, seek_preroll): (Box<dyn AudioDecoder>, _) = match params.codec {
            CODEC_ID_OPUS => (
                Box::new(OpusDecoder::try_new(params, &options)?),
                Duration::from_millis(80),
            ),
            CODEC_ID_VORBIS => (
                symphonia::default::get_codecs().make_audio_decoder(params, &options)?,
                // Vorbis needs a preceding packet to initialize its overlap.
                Duration::from_millis(100),
            ),
            _ => return Err(Error::Unsupported("unsupported audio codec in EBML")),
        };
        let time_base = track
            .time_base
            .ok_or(Error::Unsupported("no audio time base"))?;
        let decoded = decoder.codec_params();
        let rate = decoded
            .sample_rate
            .ok_or(Error::Unsupported("no decoded sample rate"))?;
        let channels = decoded
            .channels
            .as_ref()
            .ok_or(Error::Unsupported("no decoded channels"))?
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
            seek_preroll,
            rate,
            channels,
            time_base,
            duration,
            pcm: Vec::new(),
            cursor: 0,
            target_ns: 0,
            ended: false,
            failure,
        };
        source.fill()?;
        if source.ended {
            return Err(Error::DecodeError("empty audio track"));
        }
        Ok(source)
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
                .ok_or(Error::DecodeError("audio packet timestamp out of range"))?
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

impl Iterator for DemuxedSource {
    type Item = f32;

    fn next(&mut self) -> Option<Self::Item> {
        if let Err(error) = self.fill() {
            let kind = match error {
                Error::IoError(error) => error.kind(),
                _ => io::ErrorKind::InvalidData,
            };
            self.failure.record(kind);
            self.ended = true;
            return None;
        }
        let sample = *self.pcm.get(self.cursor)?;
        self.cursor += 1;
        Some(sample)
    }
}

impl Source for DemuxedSource {
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
        let preroll = target.saturating_sub(self.seek_preroll);
        let time = Time::try_from_nanos_u128(preroll.as_nanos())
            .ok_or_else(|| seek_error("audio seek time out of range"))?;
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
