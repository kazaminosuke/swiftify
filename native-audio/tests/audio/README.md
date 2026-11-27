These short fixtures contain generated tones (440 Hz for 0.6 seconds, then
880 Hz for 0.6 seconds), encoded at 24 kbps with FFmpeg's `libopus` and remuxed
from Ogg into WebM without re-encoding. They contain no third-party recording.

`two-tones-vorbis.webm` contains the same synthetic tones encoded as Vorbis in WebM.

`short-default-track.webm` contains a 555 ms default Opus tone at 440 Hz
and a 1.12 s alternative Opus tone at 880 Hz. It checks that the longer WebM
segment does not override the selected track. The final packet carries
Matroska DiscardPadding, which Symphonia 0.6.1 does not expose. The fixture
was generated with FFmpeg.

```sh
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=0.555' \
  -f lavfi -i 'sine=frequency=880:sample_rate=48000:duration=1.12' \
  -map 0:a:0 -map 1:a:0 -c:a libopus -b:a 16k -disposition:a:0 default \
  -disposition:a:1 0 -fflags +bitexact -flags:a +bitexact -map_metadata -1 \
  short-default-track.webm
```
