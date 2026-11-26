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

## Running the desktop client

Work is being reviewed in stacked draft branches. Build from
`youtube/desktop-client` or a descendant while the pull requests are open.

1. Build Swiftify using the [README instructions](../README.md#building-from-source).
   The Qt kit must be 6.10 or newer and include Qt Network Authorization.
   The Windows and Linux CMake builds enable the YouTube workspace by default.
2. Run `swiftify` on Linux or `swiftify.exe` on Windows. The search box opens
   an official YouTube Music search in your browser if the API is unconfigured.
3. For results inside Swiftify, enable YouTube Data API v3 in your Google
   Cloud project and provide a YouTube Data API key in the process environment
   as `SWIFTIFY_YOUTUBE_API_KEY`. Restrict the key to YouTube Data API v3.
4. For account access, create a Google OAuth **Desktop app** client with YouTube
   Data API v3 enabled. Configure the consent screen and test users if the app
   is in testing. Set `SWIFTIFY_GOOGLE_CLIENT_ID` and
   `SWIFTIFY_GOOGLE_CLIENT_SECRET` in the process environment before launching
   Swiftify. Sign in in your browser and return to the app. Only read-only
   YouTube access is requested. Accounts without a YouTube channel can keep
   using the browser search or a separately configured API key.
5. To remove account access from the current session, choose **Disconnect
   Google**. Swiftify clears account data and requests token revocation.
   The privacy dialog also links to Google's permissions page. Tokens and
   API results are held in memory, and a new launch requires a new sign-in.

The workspace can read liked videos in YouTube's Music category, owned
YouTube playlists and their contents. Use **Open on YouTube** to manage a
playlist on the official site. Availability of a particular result on YouTube
Music varies. Swiftify never receives PCM audio from browser playback.
Run `swiftify --spotify` for the upstream workspace and its local playback,
DSP, equalizer, lyrics and visualizers.

## Verification record

- Linux developer build with Qt 6.10.3. The desktop executable built and
  its X11 window opened. The six YouTube and repository CTest entries passed.
- The full local CTest run passed 21 of 23 suites. Five cases in
  `tst_views` and one playlist-order case in `tst_local` also failed on
  the original fork `main` checkout at `21e6d0e` with the same Qt 6.10.3 and
  Xvfb setup.
- Both Windows and Linux passed the six package CI checks and uploaded
  archives. The Linux archive opened its own bundled Qt window after
  extraction. The Windows archive includes the executable, its imported
  Qt Network Authorization DLL and the GPL and brand notices.
  [Package run](https://github.com/kazaminosuke/swiftify/actions/runs/44527107970)
- Both platform jobs succeeded again after the attribution rewrite.
  The final QML adjustment makes the official YouTube wordmark readable
  on both theme backgrounds. The six local checks and the dark-theme X11
  startup screenshot were verified again.
  [Post-rewrite CI](https://github.com/kazaminosuke/swiftify/actions/runs/44528859612)
- Final source commit `7e76ec4` passed the Windows and Linux package
  jobs. Both passed all six CI tests and uploaded archives. The final
  Linux archive started under X11 with its bundled Qt and displayed
  the corrected attribution wordmark.
  [Final source CI](https://github.com/kazaminosuke/swiftify/actions/runs/44531388404)
- The live Google consent flow, authenticated YouTube library reads, and
  audio playback in the browser still need a real Google Cloud Desktop
  client and an account. The automated tests use a local OAuth listener,
  fixture API replies and a browser URL handler.

## Git identity and draft reviews

Before creating commits in a fresh checkout, use the authenticated
`kazaminosuke` account and verify its GitHub-provided noreply address.
Set `user.name`, `user.email` and `user.useConfigOnly` with
`git config --local`. Check the effective Author and Committer with
`git var GIT_AUTHOR_IDENT` and `git var GIT_COMMITTER_IDENT`. Linked
worktrees use the same repository-local configuration. GitHub CLI
authentication and Git commit identity are configured independently.
See [GitHub's commit email reference](https://docs.github.com/en/account-and-profile/reference/email-addresses-reference).

The first ten development commits in the four draft PRs used the previous
global Git address. That address was absent from the authenticated GitHub
account's registered emails. GitHub returned no linked author or committer
for those commits. A history rewrite would change commit IDs across the
stack, whose bases are `main` → `youtube/data-api` → `youtube/google-oauth`
→ `youtube/desktop-client` → `youtube/package-ci`. With the fork owner's
authorization, the four feature branches were updated atomically with
`--force-with-lease` after comparing each old commit tree and its parent
topology. All 13 replacement commits use the verified noreply identity for
both Author and Committer and carry a `Generated-by: OpenAI Codex` trailer.
GitHub confirmed both associations for every replacement commit. The four
existing draft PRs and the fork's `main` were preserved. Local recovery refs
are under `refs/codex-backup/swiftify-attribution-20261119/`.

## Session log (JST)

| Time | Check | Result |
| --- | --- | --- |
| 2026-11-19 21:22 | Start, official Codex App Server `account/rateLimits/read` | HTTP 401. Remaining usage and all limit windows unavailable. |
| 2026-11-19 21:27 | Repository and API review, official App Server stdio and daemon proxy | HTTP 401 through stdio. The daemon control socket was absent. No percentage inferred. |
| 2026-11-19 21:34 | Data API tests and Google OAuth tests, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 21:37 | Desktop integration progress, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 21:43 | Packaging review, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 21:49 | Draft stack and Linux checks, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 21:56 | Attribution update, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:03 | Distribution verification, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:10 | Base test comparison, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:13 | Final QML and targeted tests, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:18 | Distribution CI monitoring, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:24 | Linux final package available, official App Server | HTTP 401. No percentage available. |
| 2026-11-19 22:26 | Final check after Windows and Linux CI, official App Server | HTTP 401. No percentage available. |

The new features use Qt Network and the existing Material 3 QML components.
The upstream Rust core and playback code remain available through the
upstream workspace.

Next is a live Google OAuth, library and browser playback test with a
configured Google Cloud desktop client and a test account. If the project obtains an
authorized PCM source, re-evaluate integration with the Rust DSP path.
