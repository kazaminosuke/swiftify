# YouTube Music fork handoff

## 2026-11-26 synchronization

Fork `main` fast-forwarded from `21e6d0e` to `51dd3ef` with the original
upstream commits. `49767c3` corrects texture sizing and filtering during
Qt Quick zoom and contains the related scene and artwork updates.
`8eb91fe` merges that change. `0a3f595` sets version 0.26.0. `51dd3ef`
adds an opt-in release for manual prebuilt workflow runs. There was no
dependency major upgrade or playback/DSP change in this upstream range.
[Upstream range](https://github.com/swiftify-org/swiftify/compare/21e6d0e...51dd3ef)

All ten PR heads incorporate the new upstream through normal merge
commits. #1 → #2 → #3 → #4 → #8 is the catalogue stack. #5 → #6 is the
audio base, with #7 as the lyrics branch and #9 → #10 as the media/device
branch. The catalogue PR is now based on #4. It keeps typed catalog
search and native playlist browsing, restores the #4 packaging checks
and shares startup infrastructure with the existing playback window.
[#11](https://github.com/kazaminosuke/swiftify/pull/11) follows #10 and
[#12](https://github.com/kazaminosuke/swiftify/pull/12) follows #7.
All twelve PRs remain open as drafts. [Open drafts](https://github.com/kazaminosuke/swiftify/pulls)

### Code review follow-up

The previous review text was not present in the PR review threads or fork
work records inspected. The items supplied for this session were checked
against the current code and tests.

| Finding | Status |
| --- | --- |
| Audio routes and clock | The Qt/Spotify core and standalone CPAL player share format and device adapters. The render lifecycles are separate. The Qt local counter advances as PCM is decoded into output blocks. The standalone clock tracks submitted and estimated presentation frames with a revision. These cannot yet be treated as the same time source. |
| Fixed sample rate | The Spotify and local DSP paths already supply normalized stereo 44.1 kHz blocks. Live input was using those EQ coefficients at other rates. #11 calibrates input rate and channel state and bypasses out-of-band bands. |
| Provider I/O and seek | #11 signals cancellation before joining a stopped or replaced decoder and distinguishes read/seek I/O failures from decoder seek failures. Verification covers cancellation of a blocked cooperative reader, rejected opens, successful seek and natural EOF. Opening and probing still need a control worker for slow inputs. |
| WebM | #6 already supplies single-track segment duration. #11 uses codec metadata to select Opus or Vorbis and tests decoding, duration and seek with generated clips. Exact multitrack audio duration and padding still need fixtures. |
| Output | #10 supports standalone device selection. #11 propagates CPAL loss to Qt's existing pause and explicit resume flow. Paused-seek callbacks cannot consume the replacement source before resume. Physical removal, backend buffer latency and device selection in Qt remain open. |
| Catalogue overlap and startup | #8 is stacked on #4. Instance routing, scrolling, settings, translation and QML startup wiring are shared. The playback shortcuts reach the player and the catalogue retains its own window. |
| Lyrics | The track correction is persisted independently from the fallback setting. QML uses the same correction for highlighting and seeks. The 25 ms control step is finer than the timing guarantees of the Qt playback poll. |
| CI | #8 carries forward Linux and Windows packaging and catalogue checks. #11 adds the full C ABI/core tests and native Clippy. GitHub rejected current manual workflow execution with HTTP 422 because Actions is disabled for the repository. |

### Current verification

- Linux Rust 1.100.0. The engine passed 10 unit tests, an Ogg/WebM codec
  test and three ALSA null-device tests. Formatting and native Clippy passed.
  The core C ABI library suite passed 76 tests with one opt-in benchmark ignored.
- Qt 6.10.3 on Linux. The combined catalogue and audio stacks passed the
  playback, local library, catalogue, OAuth, QML, translation and icon
  checks. Both app modes opened offscreen and repeat launches forwarded
  to their own instance. Linux desktop integration was disabled for this
  build. Staged installation contained the GPL, Qt and YouTube notices and
  the Rust notice for Symphonia Vorbis 0.6.1.
- A C ABI probe played generated Vorbis/WebM with EQ and 1.25× tempo.
  It confirmed duration, a paused seek that holds its position, resume
  and explicit output restart through ALSA. Actual Google consent,
  Windows/macOS, hardware output changes and acoustic lyric latency
  remain unverified.

### Next integration boundary

Retain one output lifecycle for local and future authorized streams.
Expose a coherent Rust presentation snapshot and a revision through the
current C ABI before interpolating in QML. Rate changes and DSP buffering
need frame-to-media-time mapping. Test cancellation and seek rollback with
a bounded authorized transport before integrating remote audio. The
[MusicBeatSP source review](musicbeatsp-source-review.md) maps the lyrics,
audio, queue and provider work and lists the relevant code and model terms.

### Codex usage

Official App Server `account/rateLimits/read` returned HTTP 401 at
startup and on repeat checks, including 20:34, 20:40, 20:45 and 20:53 JST.
No remaining percentage was available.

## Drafts and dependencies

Drafts [#1–#4](https://github.com/kazaminosuke/swiftify/pulls) remain available
for reference. [#5](https://github.com/kazaminosuke/swiftify/pull/5) supplies the
standalone CPAL engine and clock. The current stages are reviewable separately.

| Draft | Depends on | Shipped in the draft |
| --- | --- | --- |
| [#6](https://github.com/kazaminosuke/swiftify/pull/6) | #5 | Ogg and WebM Opus, shared decoder, Qt local library and existing DSP path |
| [#7](https://github.com/kazaminosuke/swiftify/pull/7) | #6 | LRC offsets, saved manual delay, synchronized display and adjusted seeks |
| [#8](https://github.com/kazaminosuke/swiftify/pull/8) | Fork `main` | Qt catalogue, typed video/channel/playlist search, read-only OAuth and browsing |
| [#9](https://github.com/kazaminosuke/swiftify/pull/9) | #6 | Reopenable media providers, typed read failure, seek rollback and EOF |
| [#10](https://github.com/kazaminosuke/swiftify/pull/10) | #9 | Output selection and reopen, preserved gain and playback intent, mute |

The existing Qt queue, repeat, shuffle, spectrum and playback-shaping controls
continue to use the local transport in #6/#7. #9/#10 use the independent CPAL
renderer. The two clocks and the Qt catalogue still need integration. New
network media recovery, gapless transitions, preloading, crossfade, account
history, recommendations and playlist writes are outstanding.

## Verification

- Linux and Windows engine CI passed for Opus and the seekable provider. The
  output selection stage passed the same matrix and the ALSA reopen test.
  The final Linux Spotify core release suite reported 74 passes and one
  opt-in benchmark ignored.
  [Opus CI](https://github.com/kazaminosuke/swiftify/actions/runs/44718711652)
  [Provider CI](https://github.com/kazaminosuke/swiftify/actions/runs/44718801085)
  [Output selection CI](https://github.com/kazaminosuke/swiftify/actions/runs/44718850140)
- Linux Qt Release, `tst_playback`, `tst_local`, icons, translations and QML checks
  passed. After fixing the local copy/scan race and the test's signal handling,
  `tst_local` passed four repeats. A C ABI probe with EQ and 1.25× playback used
  the same local route as Qt and confirmed WebM duration, paused seek and resume
  through an ALSA null device. The standalone player exercised WAV, FLAC, MP3,
  ADTS and MP4 AAC, Ogg Opus and WebM Opus with generated audio.
- The separate catalogue build passed Linux Qt tests and offscreen app startup,
  the transport and PKCE fixture suites and the Windows/Linux packaging jobs.
  The baseline library tests on this separate branch can still miss a fast
  tag-edit completion. Draft #7 carries the fix for that test and the library.
  [Catalogue packaging CI](https://github.com/kazaminosuke/swiftify/actions/runs/44717137221)

## Further source review

- `youtui` 0.0.40 and `ytmapi-rs` 0.3.4 are MIT-licensed. Their source separates
  typed album/search/playlist parsing from the downloader and Rodio sink. The
  player queues decoded bytes by item ID. The library's guest and account paths
  use service-internal requests, so the playback interface and parser structure
  are useful references independently of whether a given service route can be
  used. Swiftify has no dependency on either crate.
  [Typed search](https://github.com/nick42d/youtui/blob/c489567ef2d1a49a005018d1f9974d81165bb6d8/ytmapi-rs/src/query/search.rs)
  [Rodio player](https://github.com/nick42d/youtui/blob/c489567ef2d1a49a005018d1f9974d81165bb6d8/youtui/src/app/server/player.rs)
- `lrc` 0.2.0 (MIT) represents line time tags and supports signed shifts. Swiftify
  keeps the existing Qt parser, avoiding an extra binding and preserving its
  QML model. The lyrics source comparison is in #5. The new timing controls use
  local user-supplied LRC and the pre-existing Spotify lyric path.
  [LRC timing source](https://github.com/magiclen/lrc/blob/91b95be2810a74a5af77d554f4558c13abf75c5f/src/lyrics.rs)
  [Three-client comparison](youtube-music-client-comparison.md)

These references supplied design evidence. Code for the catalogue was reused
from the fork's earlier drafts. The Opus stage uses packaged MPL-2.0 crates
with their notices. No media, lyrics, account credentials or resolver code
were imported from the reviewed projects.

## Next integration work

1. Validate a permitted media source that reports content identity, seek
   capabilities and bounded failures. Connect it to the native media provider.
2. Expose a coherent clock and render controls through the Qt worker boundary.
   Reuse the existing DSP chain and cover output selection, clock latency and
   stale events. Add one-track lookahead before implementing transitions.
3. Test WASAPI and actual ALSA/PipeWire devices, unplug/reopen, Wayland/Hyprland
   and audio-to-lyrics alignment. Validate a real Google project and manual
   OAuth. The existing QtKeychain adapter is a reference for scoped credential
   storage and revocation recovery.
4. Add music-specific catalogue entities and account collections as an
   appropriate service adapter becomes available. Review audio/video separation,
   endpoint authorization, storage, background playback and lyric rights for
   the selected routes. No live media availability or service approval was
   established by the source-only reviews.

## Codex usage (JST)

| Time on 2026-11-20 | Stage | Official App Server result |
| --- | --- | --- |
| 18:55 | Start and draft review | HTTP 401 |
| 19:00 | Codec tests | HTTP 401 |
| 19:05 | Codec and lyrics milestones | HTTP 401 |
| 19:09 | Catalogue and OAuth fixtures | HTTP 401 |
| 19:14 | Media provider and catalogue build | HTTP 401 |
| 19:18 | Output device tests | HTTP 401 |
| 19:23 | Integrated verification | HTTP 401 |
| 19:27 | Closing verification and handoff | HTTP 401 |
| 19:30 | Final CI and core regression results | HTTP 401 |

The official `account/rateLimits/read` check did not return an available
percentage. Authentication failure was not treated as a quota reading.
