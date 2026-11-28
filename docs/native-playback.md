# Native playback

Qt's production playback path passes from `PlaybackController` through the
C ABI into the Rust core in `spotify-core/`. The local renderer is compiled
with Spotify disabled. `native-audio/` provides CPAL output, shared decoding
and PCM accounting. It also provides a standalone `Player` that is useful for
testing decoders and the media-source contract.

## Desktop render path

The local producer opens and decodes the source, prepares stereo PCM for the
device rate, applies ReplayGain and the DSP chain, and puts frames into a
bounded render queue. Frames carry positions in the original recording so
the media clock tracks tempo changes. The real-time callback consumes
available frames and can supply silence for an underrun without advancing
the submitted media position. At the end of a source, the producer flushes
DSP and the clock accounts for the final queued frames.

The Rust clock exposes a revision, submitted position, estimated audible
position, optional device latency and drain status. Qt consumes the
presentation estimate when available and the submitted position otherwise.
The estimate depends on CPAL and the output backend. Measure actual output
latency on the target hardware. The clock feeds the transport, MPRIS and
[lyrics timing](synchronized-lyrics.md).

Playback and seek prepare replacement sources before switching. Asynchronous
requests use generations to prevent superseded operations from applying.
The [audio output guide](audio-output-devices.md) describes recovery.

## Shared input

Local inputs use the shared decoder with Ogg and WebM Opus, WebM Vorbis and
the codecs enabled through Rodio and Symphonia. Container and track metadata
matter for duration and seeking. Use source-frame positions to account for
tempo, encoder delay, padding and buffered DSP when comparing timing.
[Media provider](media-provider.md)

The standalone `Player` exercises seekable sources and typed I/O failure
through the same shared decoding and rendering primitives. The Qt desktop
path uses its own playback and DSP controls in the Rust core. The end-to-end
YouTube Music path must exercise the desktop controls when validating
playback, seek, pause, recovery and synchronization.
[Backend investigation](youtube-music-backend.md)

## Development checks

From the repository root, run the Rust suites for the audio crate and the
Spotify-disabled desktop core.

```sh
cargo test --locked --manifest-path native-audio/Cargo.toml
cargo test --locked --manifest-path spotify-core/Cargo.toml --no-default-features --features capi
```

The standalone example opens and plays a local audio file through CPAL.

```sh
cargo run --locked --manifest-path native-audio/Cargo.toml --example play-file -- /path/to/audio.wav
```

Local files and virtual ALSA fixtures exercise the decoder, output and
clock. The [CachyOS trial guide](cachyos-trial.md) lists checks that need
the desktop session and physical audio hardware.
