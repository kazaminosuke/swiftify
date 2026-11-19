# Swiftify YouTube fork development

The fork is based on [kazaminosuke/swiftify](https://github.com/kazaminosuke/swiftify) at
`21e6d0e`. Swiftify is GPL-3.0-or-later. Preserve the upstream copyright
notices and the third-party notices shipped by CMake. YouTube brand assets
are separately governed by the YouTube API branding guidelines.

## Upstream code map

| Layer | Upstream implementation | Fork approach |
| --- | --- | --- |
| Desktop and presentation | `src/main.cpp`, `src/app`, `qml/Main.qml`, `qml/design`, `qml/components` | Share Qt Quick controls, Material 3 palette, typography and motion. |
| Search and library | `src/spotify/WebApi.cpp`, `SpotifyJson.cpp`, `src/library/LibraryStore.cpp`, Spotify IDs in models and page navigation | Use a dedicated YouTube module and keep video IDs, playlist IDs and account data separate. |
| Sign-in | `src/spotify/SpotifySession.cpp`, `OAuthFlow.cpp`, `CredentialStore.cpp` | Use Google desktop OAuth, the system browser, loopback and PKCE. Keep credentials separate from Spotify. |
| Playback | `src/playback/PlaybackController.cpp`, `src/core/RustCore.cpp`, `spotify-core/src/capi.rs` and `lib.rs` (librespot), `src/soloist` (Spotify Soloist) | Open validated media URLs in the official service. The existing core remains the native playback path for its existing inputs. |
| Audio and lyrics | `spotify-core/src/dsp.rs`, `output.rs`, `spectrum.rs`, `file_playback.rs`, `src/render`, `src/playback/Lyrics.cpp` | The DSP, equalizer and visualizer require a permitted PCM source. Lyrics require a separately authorized source and reliable track matching. |
| Builds | `CMakeLists.txt`, `.github/workflows/prebuilt.yml`, `tools/build-linux-tarball.sh`, `tools/build-windows.ps1` | Keep the Qt/CMake and Rust toolchains and verify each target OS. |

## API and playback decision

- [YouTube Data API](https://developers.google.com/youtube/v3) documents music
  video search using category 10, liked videos, owned YouTube playlists and
  playlist contents. Liked results can be filtered locally by category 10.
  These are YouTube datasets. The API does not document access to the full
  YouTube Music catalog or saved albums, subscriptions or library.
- [Google desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app)
  supports a registered Desktop app, dynamic loopback port and S256 PKCE
  with the external browser. The read-only YouTube scope covers the first
  account features. The scope can require app verification for public distribution.
- [ytmapi-rs / youtui](https://github.com/nick42d/youtui) and
  [ytmusicapi](https://github.com/sigma67/ytmusicapi) are MIT-licensed
  integrations for the internal YouTube Music API. Their code license
  does not authorize use of that service API. Browser-cookie extraction or
  emulated client sign-in would add a sensitive credential dependency.
- yt-dlp and stream parsers can resolve audio/video formats for a local
  player, but the [YouTube API developer policies](https://developers.google.com/youtube/terms/developer-policies)
  prohibit separating or modifying YouTube audiovisual components for
  API clients without applicable approval. FFmpeg, Qt Multimedia and the
  Rust PCM path would still need an authorized stream source.
- The [official YouTube IFrame player](https://developers.google.com/youtube/iframe_api_reference)
  supports visible embedded video, requires an embedded web runtime and
  keeps control of playback. The fork uses an explicit handoff to the
  system browser, with no WebView or Electron. Playback availability,
  ads and sign-in depend on the official service.

## Session log (JST)

| Time | Check | Result |
| --- | --- | --- |
| 2026-11-19 21:22 | Start, official Codex App Server `account/rateLimits/read` | HTTP 401. Remaining usage and all limit windows unavailable. |
| 2026-11-19 21:27 | Repository and API review, official App Server stdio and daemon proxy | HTTP 401 through stdio. The daemon control socket was absent. No percentage inferred. |
| 2026-11-19 21:34 | Data API tests and Google OAuth tests, official App Server | HTTP 401. No percentage available. |

Next priorities are the Qt Quick shell, Google OAuth and account library,
then cross-platform package and browser-handoff verification.
