# Synchronized lyrics

For a local recording, Swiftify looks for lyrics in the same directory.
It prefers the app's edited `.swiftify.ttml` sidecar, then a matching
`.lrc`, then a matching `.ttml`. For example, use `Track.lrc` alongside
`Track.flac`. The LRC parser accepts a UTF-8 BOM, multiple timestamps per
line and signed `[offset:]` metadata. Enhanced LRC and timed TTML can supply
word cues. Plain lyrics retain their untimed reading view.

The sidebar and immersive player highlight the current line and word against
the playback position. The local Qt path uses the native clock, including its
presentation estimate when the output backend can supply one. A lyric-line
seek uses the same timing correction as highlighting.

Timing corrections are saved per track in milliseconds. Positive values
display a lyric later. The controls adjust in 25 ms steps and reset the
current track to 0 ms. The track URI is hashed for storage. Moving a local
file to a different URI gives it a different correction. Tracks without a
per-track setting use the saved global delay.

Local timed-lyric edits are written to a separate `.swiftify.ttml` sidecar.
The original LRC or TTML file remains available alongside that sidecar.
When checking synchronization, cover pause, seek in both directions, tempo
changes and audio-device recovery. The [CachyOS trial guide](cachyos-trial.md)
lists the hardware checks.
