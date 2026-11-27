# Synchronized lyrics

The existing Qt playback panels render a user's `.lrc` file with the same stem
as the music file. A UTF-8 BOM, multiple cue tags per line and signed `[offset:]`
metadata are supported. Positive file offsets advance the LRC cues. The sidebar
has a saved manual delay in 100 ms increments. Positive manual delay displays
lyrics later. Highlighting, scrolling and seeking to a lyric use the same delay.
The setting also affects the immersive player. Untimed lyrics retain their normal
reading view. The implementation reads local files and the existing Spotify
lyrics source, subject to each track's existing access path.

`PlaybackController` currently uses the existing app playback position. It polls
in 250 ms intervals and updates immediately when a seek succeeds. Accurate
speaker-time synchronization will need the Rust clock and timeline revision from
`native-audio`, including pauses, underruns and compensation for DSP and device
latency. The manual delay can compensate for a repeatable timing difference.

The source comparison covers Refryt's timed-line lookup, Limusic's `audio-pts`
clock and Raaga's word interpolation. The existing Qt parser and model remain in
use. No lyrics, alignment models or parser code were imported from the reviewed
clients. Additional lyric sources need review of display, caching, attribution
and redistribution rights. Word-level display and model-assisted alignment are
future features.
[source comparison](youtube-music-client-comparison.md)

## Codex usage (JST)

- 2026-11-20 19:05. Official App Server returned HTTP 401 after the codec stage.
  No remaining-usage percentage was available.

## Track-specific manual correction

The existing timing controls save the displayed total correction for the
current track. Repeated entries for the same track use the same correction
after a restart. Track settings use the song URI as the identity, hashed
for storage. Moving a local file to a new URI creates a new identity.
Tracks without an override use the old global `lyricsDelayMs` setting.
Reset sets the current track to 0 ms. Buttons adjust in steps of 25 ms.

Line highlighting and line seek use the same correction. The Qt poll
interval and device latency still limit overall timing accuracy.
The [MusicBeatSP review](musicbeatsp-source-review.md) traces its manual
offset, timestamp editor, report-steered clock and cue formats.
