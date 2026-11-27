# Native YouTube catalogue

On a build with `SWIFTIFY_YOUTUBE_CLIENT=ON`, launch `swiftify --youtube`.
The window uses Qt Quick and Swiftify's Material components. A second launch
raises the existing catalogue window. The playback window uses its own
instance. Launch it with `--spotify` or use an existing playback shortcut such
as `--play-pause`.

For guest search,
set `SWIFTIFY_YOUTUBE_API_KEY` in the process environment using your own
configured project. For account collections provide the desktop application's
`SWIFTIFY_GOOGLE_CLIENT_ID` and, when required, `SWIFTIFY_GOOGLE_CLIENT_SECRET`.
The account flow runs only when a user starts it and completes Google sign-in.
Browser use is limited to authentication and attribution or policy links.

The catalogue searches music-category videos, YouTube channels and public
playlists. It shows descriptions and thumbnails where provided and opens
playlist entries within the native window. Read-only OAuth adds liked music,
owned playlists and their items. Search filters and IDs match the response
record type. Channels can include artists. YouTube Music artist and album
entities, history, recommendations and library mutation still need a service
adapter with suitable capabilities.

The desktop OAuth flow uses PKCE, loopback redirect, automatic refresh in a
running session, local sign-out and a server revocation request. The application
holds the tokens in memory. It does not yet persist a refresh token in the OS
keychain. API credentials are sent in request headers. OAuth diagnostics avoid
including secret-bearing request details. The mock tests cover redirects,
request shape and failures. A real Google project and manual account login are
required to validate the configured service end to end.

This catalogue supplies metadata. A native player still needs a separate media
provider that can return media with appropriate access and use permissions.
The existing comparison examines the source-level tradeoffs of guest clients,
range proxies, expiring URLs and browser sessions. No such provider and no
YouTube Music audio playback are enabled by this window.

This branch re-evaluates the Swiftify code from Drafts #1–#3. It reuses the
transport, authorization and selected QML and test code, under the same project
license and attribution. It removes browser search and playback handoff. The
YouTube identity assets and Qt NetworkAuth packaging retain their notices.
[Source comparison in #5](https://github.com/kazaminosuke/swiftify/blob/40e3cb1/docs/youtube-music-client-comparison.md)
[YouTube Terms](https://www.youtube.com/t/terms)
[YouTube API Developer Policies](https://developers.google.com/youtube/terms/developer-policies)

## Codex usage checks (JST)

- 2026-11-20 19:09. Official App Server returned HTTP 401 at catalogue integration.
  No remaining-usage percentage was available.
