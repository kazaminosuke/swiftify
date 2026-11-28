# Native media provider

`MediaProvider::open` supplies a fresh reader for the same recording. A
`MediaInput` accepts a `Read + Seek + Send + Sync` reader, a known byte length
when available, and an optional format hint. The decoder recognizes Ogg Opus
and WebM by their bytes. A disk path is one implementation. The tested
in-memory WebM provider uses the same decoder and CPAL audio path.

`Player::load_source` and `Player::seek` prepare a source before replacing the
current track. A denied reopen keeps the active track and its timeline revision.
On stop, the decoder worker releases the current reader. A terminal read error
and a detected Opus decoding error become `SourceFailed` after queued PCM drains.
The snapshot carries an `io::ErrorKind`, without the provider's error message.
Natural EOF remains `Ended`. Rodio does not expose every packet-decoder error
in its iterator. The same transport-error observer still applies to its reader.

The current contract accepts seekable media. Opening and seeks can block on
the control thread. A network adapter must time-bound each blocking operation,
validate range and content identity before resuming, and enforce its permissions and
authentication rules. The standalone contract is exercised by native-audio
tests. The production Qt routes are described in [Native playback](native-playback.md).

## Cancellation and seek failures

Providers can retain the per-reader `MediaCancellation` token passed to
`open_cancellable`. The player sets it before joining the previous decoder
after stop, successful replacement or drop. Readers wrapping remote I/O
must check the token during waits, wake on cancellation or enforce deadlines.
Provider open, format probing and seek preparation still run on the control
thread. Callers that need a responsive UI should use a worker for those
operations. A fresh candidate has its own token, so a rejected seek keeps
the active reader.

The player distinguishes provider open errors, input read errors and byte
seek errors. A codec seek failure without a recorded I/O error remains a
decoder seek error. Provider error messages stay out of these results.
