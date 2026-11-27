# Native YouTube catalogue

On a build with `SWIFTIFY_YOUTUBE_CLIENT=ON`, launch `swiftify --youtube`.
The window uses Qt Quick and Swiftify's Material components. A second launch
raises the existing catalogue window. The playback window uses its own
instance. Launch it with `swiftify` or use a playback shortcut such as
`--play-pause`. To enable the optional Spotify integration, pass `--enable-spotify`.

For guest search,
set `SWIFTIFY_YOUTUBE_API_KEY` in the process environment using your own
configured project. For account collections provide the desktop application's
`SWIFTIFY_GOOGLE_CLIENT_ID` and `SWIFTIFY_GOOGLE_CLIENT_SECRET`.
The account flow runs only when a user starts it and completes Google sign-in.
Browser use is limited to authentication and attribution or policy links.

The catalogue searches music-category videos, YouTube channels and public
playlists. It shows descriptions and thumbnails where provided and opens
playlist entries within the native window. Read-only OAuth adds liked music,
owned playlists and their items. Search filters and IDs match the response
record type. Channels can include artists. YouTube Music artist and album
entities, history, recommendations and library mutation still need a service
adapter with suitable capabilities.

The desktop OAuth flow uses PKCE, loopback redirect, optional on-demand refresh,
local sign-out and a server revocation request. The application
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

## Google Cloud setup and request limits

Enable YouTube Data API v3 for the Google Cloud project. Restrict
`SWIFTIFY_YOUTUBE_API_KEY` to YouTube Data API and use it for public browse
and public playlist requests. For a personal library, register a
**Desktop app** OAuth client and pass its ID and client secret through the
documented environment variables. Swiftify opens the system browser, uses an
ephemeral IPv4 loopback port with PKCE S256, and requests only
`https://www.googleapis.com/auth/youtube.readonly`. Configure Branding,
Audience and Data Access in Google Auth Platform. Add the intended test users
when the app is in Testing, and confirm verification requirements and the
publishing status before releasing it. Desktop credentials can be inspected
by someone who controls the desktop process. Keep their values out of source,
logs, command-line arguments, shared configuration and packages.

Access is online by default and lasts for the granted session. In a deployment
that needs refresh within that process, set
`SWIFTIFY_GOOGLE_OFFLINE_ACCESS=1` before opening the workspace. Google can
omit a refresh token, and Swiftify lets the user authorize again when the
session expires. Refresh requires an explicit account request and the UI asks
the user to retry after it completes. Tokens live in memory. Disconnect cancels
catalogue and OAuth work, clears the local authorization, then makes a bounded
revocation request and reports its outcome.

HTTP 401 and permission 403 halt work for the affected credential. Account
401 requires a new sign-in. Correcting a blocked public key requires a fresh
process. Quota and rate responses suspend both paths for the workspace.
A daily quota pause lasts until midnight Pacific Time or the provider's
longer Retry-After. A throttle without Retry-After pauses for one minute.
The workspace waits for another user action before sending a request.
The catalogue allows one page request and one profile request in flight.
Each request has a 2 MiB body limit and a 25 s deadline. A collection loads up
to 40 pages, 25 results each. A retry keeps the page token and previous results.

The actual OAuth client's status, project ownership, quota, test audience,
verification, account access and API key restrictions require confirmation
by the project operator. The development tests use dummy credentials.

- [Google OAuth for Desktop Apps](https://developers.google.com/identity/protocols/oauth2/native-app)
- [Google OAuth App Verification](https://support.google.com/cloud/answer/13463073)
- [YouTube Data API setup](https://developers.google.com/youtube/data/getting-started)
- [YouTube Data API quotas](https://developers.google.com/youtube/data/determine_quota_cost)
