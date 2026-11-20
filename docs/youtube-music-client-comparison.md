# Native YouTube Music clients compared

The sources below were checked out at the listed revisions on 2026-11-20.
This is a static source comparison. No provider request, browser login, or
third-party executable was run during the comparison. Swiftify's native
audio test results are in [Native playback](native-playback.md).

| Project | Reviewed revision | Project source license |
| --- | --- | --- |
| Refryt | `039d83e5955b3e3386de7a8cb60d019a344e877f` | [GPL-3.0-or-later in Cargo.toml](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/Cargo.toml#L8) and [GPL v3 text](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/LICENSE) |
| Limusic | `5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b` | [GPL-3.0-or-later in Cargo.toml](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/Cargo.toml#L15) and [GPL v3 text](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/LICENSE) |
| Raaga | `cc79085d5d9e3b8ec03283e89950185e584e56f1` | [GPL-3.0-or-later in Cargo.toml](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/Cargo.toml#L14) and [GPL v3 text](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/LICENSE) |

## Refryt

- **Engine and audio processing.** The Rust player lazily initializes libmpv,
  loads a resolved URL for audio-only playback and exposes stop, pause, seek
  and volume. The load path applies per-track loudness normalization as a gain
  correction with bounds. It handles libmpv open failures and redacts query
  strings and home paths from libmpv logs.
  [Player state and controls](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/player.rs#L156-L241)
  [Logging and gain](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/player.rs#L243-L346)
- **Stream and authentication.** The backend builds a resolver chain with an
  anonymous InnerTube resolver and a yt-dlp fallback. The optional in-process
  resolver is disabled by default because the maintainers' validity check
  rejected its output. The backend caches short-lived resolved stream
  descriptors and offers a local proxy with range reads and a refresh path.
  Signed-in sessions are optional and use the project's Wry sign-in window
  or import from a user's existing browser. The app stores its session as
  JSON and sets Unix file permissions to `0600`.
  [Resolver order](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt-resolve/src/chain.rs#L105-L135)
  [Backend assembly](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/backend.rs#L1013-L1037)
  [Proxy refresh](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt-resolve/src/proxy.rs#L424-L505)
  [Native resolver feature](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt-resolve/Cargo.toml#L22-L40)
  [Sign-in window](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/webview_login.rs#L132-L151)
  [Browser session option](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/README.md#L196-L198)
  [Credential storage](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/auth.rs#L62-L101)
- **Queue, time and errors.** The core queue holds current index, resume point,
  shuffle order and history. Async play results are accepted by generation.
  Recovery tracks attempts per item and uses backoff. Lyrics pick a line
  from libmpv's polled `time-pos`, which is a media position sample.
  [Queue](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/queue.rs#L163-L185)
  [Retry state](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/app.rs#L194)
  [Generation handling](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/app.rs#L6230-L6250)
  [Lyrics](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt/src/ui/lyrics.rs#L39-L46)

## Limusic

- **Engine and audio processing.** Tauri presents a WebView UI. The Rust
  playback crate uses libmpv's playlist for gapless lookahead and a second
  libmpv deck for crossfade transitions. Its controls combine gain, speed,
  pitch and convolution reverb. There is also an optional local
  vocal-separation path with dedicated model and runtime notices.
  [Engine](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/crates/player/src/lib.rs#L389-L478)
  [Lookahead and crossfade](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/crates/player/src/lib.rs#L574-L647)
  [Effects](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/crates/player/src/lib.rs#L813-L892)
  [Notices](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/THIRD-PARTY-NOTICES.md)
- **Stream and authentication.** The orchestrator consults multiple InnerTube
  client paths and RustyPipe, tracks stream expiry and tests whether candidate
  streams return usable data. Its source includes player-script URL
  transformations and an attestation component. A loopback proxy forwards
  byte ranges to upstream media. A Tauri Google sign-in window collects
  browser session cookies. Account records and cookie updates are stored in
  SQLite. The native audio path is separate from those WebView dependencies.
  [Resolver](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/orchestrator.rs#L215-L237)
  [Range validation](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/orchestrator.rs#L794)
  [Proxy](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/audioproxy.rs#L305-L376)
  [Sign-in window](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/session.rs#L103-L140)
  [Session lifecycle](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/docs/SIGN-IN-SESSIONS.md)
- **Queue, time and errors.** Queue state keeps item index, source, repeat,
  pre-shuffle order and the primed next entry. The player distinguishes EOF
  from a deliberate stop or replacement and suppresses completion from
  inactive decks. The resolver distinguishes global failures such as missing
  sign-in or network from an item that failed to resolve. Its on-demand
  lyrics clock prefers libmpv's `audio-pts` over `time-pos` and pairs the
  sample with an observation time, speed and pause state.
  [Queue](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/state.rs#L337-L386)
  [Player events](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/crates/player/src/lib.rs#L1296-L1329)
  [Resolver errors](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/orchestrator.rs#L71-L104)
  [Audio position](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/crates/player/src/lib.rs#L895-L905)
  [Clock snapshot](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/state.rs#L3735-L3753)

## Raaga

- **Engine and audio processing.** The Tauri UI invokes a Rust libmpv player.
  The player preloads on the inactive deck and uses crossfade and gapless
  lookahead. It uses volume normalization and Rubber Band pitch shifting.
  If the optional pitch filter cannot be set, it restores the previous
  usable effects chain.
  [Engine](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/crates/player/src/lib.rs#L272-L325)
  [Queue and crossfade](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/crates/player/src/lib.rs#L361-L434)
  [Audio filters](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/crates/player/src/lib.rs#L583-L628)
- **Stream and authentication.** The orchestrator resolves through InnerTube
  client candidates and RustyPipe, checks media fetchability, and registers
  some streams with a loopback range proxy. As in Limusic, the source uses
  player-script transformations and an attestation component. The session
  uses a Tauri Google sign-in WebView and stored per-account cookies in
  SQLite. GitHub lists both repositories as independent repositories. Their
  checked-out resolver, proxy and session implementations are structurally
  similar, so the similarity is not counted as independent runtime evidence.
  [Resolver](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/orchestrator.rs#L182-L214)
  [Range validation](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/orchestrator.rs#L611)
  [Proxy](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/audioproxy.rs#L189-L257)
  [Sign-in window](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/session.rs#L103-L140)
  [Session lifecycle](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/docs/SIGN-IN-SESSIONS.md)
- **Queue, time and errors.** The queue remembers `played_from` to distinguish
  tracks passed during playback from tracks before the starting index. Player
  events separate EOF and failures. A generation check rejects stale preload
  completion. Word-level lyrics interpolate between libmpv `time-pos`
  updates, rebase on resume and freeze while paused. In this revision that
  interpolation adds elapsed time at 1x and does not use an audio-device
  latency anchor, so it is not a timing reference for altered tempo.
  [Queue and history](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/state.rs#L275-L308)
  [Preload and terminal events](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/crates/player/src/lib.rs#L775-L820)
  [Resolver errors](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/orchestrator.rs#L64-L95)
  [Lyrics interpolation](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/ui/src/lib/components/LyricsView.svelte#L122-L152)

## What Swiftify takes from the comparison

1. Use a provider adapter to resolve playable inputs and handle expiry and
   retry. Pass content identity, format, range/seek support, and a cancellable
   source to the renderer. Transient connectivity and expiry can be retried
   within a budget. Access denial, missing login and item-level unavailability
   should have separate outcomes. Only ordinary EOF advances the queue.
2. Give queue entries stable identity, keep a revision for async source and
   preload decisions, and scope errors and terminal events to that revision.
   Keep EQ and track gain independent of chosen volume. Validate enabled DSP
   before switching a live chain, and account for its latency and tempo.
3. Distinguish the decoder position, samples submitted to the device, and
   estimated media time at the speakers. Follow revision, pause and seek
   boundaries. Rust now exposes submitted and estimated audible positions,
   device latency and a revision for the file playback engine. The Qt bridge
   should add consistently sampled monotonic observation times and playback
   rate. Hardware and Bluetooth latency need measurement before shipping
   synchronized lyrics.
4. Keep the Rodio/Symphonia and CPAL engine for Swiftify's PCM and DSP path.
   The reviewed clients handle AAC and Opus choices. On generated samples
   the current engine sent AAC/MP4 to Linux ALSA but could not open Opus/WebM.
   Compare a WebM demuxer and Opus decoder with a libmpv backend using
   permitted test inputs. Check seeking, gap handling, output timestamps,
   DSP access and Windows/Linux packaging before choosing a pipeline.
   The provider adapter owns media permission, availability and authentication.
   [Refryt codec selection](https://github.com/gridoe/refryt/blob/039d83e5955b3e3386de7a8cb60d019a344e877f/crates/refryt-resolve/src/lib.rs#L38-L64)
   [Limusic format ranking](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/src-tauri/src/orchestrator.rs#L985-L1004)
   [Raaga format ranking](https://github.com/Mercyy00/Raaga/blob/cc79085d5d9e3b8ec03283e89950185e584e56f1/src-tauri/src/orchestrator.rs#L770-L789)

## Independent native playback probe

On Debian 13, a standalone probe used libmpv `0.40.0-3+deb13u1` through its
C API. The test generated two seconds of the same tone as AAC/MP4 and
Opus/WebM. It disabled video and the network resolver, sought to 0.5 seconds
while paused, resumed, read `audio-pts`, and waited for EOF. libmpv's PCM
output contained 1.505 and 1.500 seconds of non-zero audio respectively.
This verifies native demuxing, decoding and seek for the two local samples.
It leaves audio-device timing, PCM access from Swiftify, Windows distribution
and network streaming to subsequent tests.
[Debian libmpv package](https://packages.debian.org/trixie/libmpv2)

## Technical feasibility and service permission

The three codebases demonstrate how native decoding and output can sit
behind a separate stream resolver. Their guest client behavior, short-lived
URLs, URL transformations and playback attestation are provider-specific.
The code shows validation failures, forbidden responses, session rotation
and unavailable items. Those are source-based observations, not a live
availability measurement.

A browser cookie grants a session with the service. How it was obtained,
how it is stored, what can be requested with it, and where it is sent need
separate review. The YouTube API policies restrict API clients' use of
undocumented services, storage of audiovisual content, audio/video separation
and background players. YouTube's general Terms apply to access and use of
service content. Permissions for user-owned or licensed recordings also
depend on the delivery route. An OSS license grants rights for application
code. Media access and distribution require the relevant service and content
permissions.
[YouTube Terms of Service](https://www.youtube.com/t/terms)
[YouTube API Developer Policies](https://developers.google.com/youtube/terms/developer-policies)

For Swiftify, signed URL expiry, transient network errors and secure storage
are engineering concerns. Client masquerading, extraction of session
credentials from a browser, playback-attestation workarounds, and choosing
another client to defeat an access rejection need independent authorization
and were left out of this change. The review informed the native engine and
the adapter boundary. It did not add a YouTube Music stream adapter.

## Reuse and credits

The three projects' GPL-3.0-or-later application source is compatible in
principle with Swiftify's GPL-3.0-or-later project code, subject to inspecting
the specific file and its provenance. Copying covered code would require
preserving copyright and license notices, marking modifications, and meeting
GPL source and distribution obligations for the combined work. A binary
distribution that adds libmpv also needs a review of the selected mpv build,
FFmpeg and filter libraries, the Rust bindings, and the licenses of any
assets or models. The published Cargo manifests label Refryt's `libmpv2`
6.0.0, Limusic and Raaga's `libmpv2` 4.1.0, and their `libmpv2-sys` 4.0.1
as `LGPL-2.1`. Limusic's third-party notices illustrate the separate
model and runtime review required for optional features.
[mpv copyright and build licensing](https://github.com/mpv-player/mpv/blob/master/Copyright)
[Refryt Rust binding](https://crates.io/crates/libmpv2/6.0.0)
[Limusic and Raaga Rust binding](https://crates.io/crates/libmpv2/4.1.0)
[FFI binding](https://crates.io/crates/libmpv2-sys/4.0.1)
[Limusic third-party notices](https://github.com/SimoHypers/limusic/blob/5b9d1b3abd17a034dc4984ad956ff3a7a7059b7b/THIRD-PARTY-NOTICES.md)

Credit for the reviewed designs goes to the Refryt contributors
(`gridoe/refryt`), the Limusic contributors (`SimoHypers/limusic`), and the
Raaga contributors (`Mercyy00/Raaga`). This change uses independently
implemented behavior. It incorporates none of their source, media, model
weights, cookies or provider-specific resolver code. The moved CPAL adapter
originated in Swiftify and retains its prior copyright and license notice.
