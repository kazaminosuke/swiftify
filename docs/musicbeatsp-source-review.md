# MusicBeatSP source review

This review reads the Android Gradle entry points, Kotlin call sites, C++ JNI
targets and both Linux source trees at MusicBeatSP commit
`abcac36660db6096d76db9ebed71d65637bd522a`. It records source wiring. The
review did not build the Android app or call its online providers.
[MusicBeatSP snapshot][beat] · [Android dependencies][gradle] · [JNI build][jni]

## Synchronized lyrics

| Feature | What the source does |
| --- | --- |
| Search and fallback | The registry wires built-in clients to a repository with parallel fetch, provider ordering, preferred-result handling and fallbacks. It also supports script-based extensions. The editor has a separate candidate-search route for several providers. [Registry][builtins] · [Fetch engine][fetch] · [Extension executor][extensions] |
| Matching | The playback fetch engine evaluates candidates and source priority. The editor's `LyricsMatchScorer` compares normalized title, artist, album and duration and shows candidates for manual selection. Maintain recording identity for repeats, remasters and alternate versions. [Scorer][scorer] · [Editor view model][editorvm] |
| Formats and display | Provider replies go through Enhanced LRC or plain LRC parsing, with TTML support in the relevant adapters. The line model keeps word and syllable boundaries and supports ruby readings and backing vocals. The Compose renderer consumes this data for its karaoke sweep. [Provider parsing][parse] · [TTML][ttml] · [Cue model][line] · [Renderer][nowplaying] |
| Playhead | The Android lyric clock uses dated monotonic samples and a discontinuity counter. The displayed playhead slews toward new samples and handles seeks separately. The Compose desktop host stores a global manual offset. In Swiftify, manual compensation is applied in both directions, to cue highlighting and line seeking. The per-track override is a Swiftify extension to that design. [Lyric clock][clock] · [Desktop offset][offset] |
| Editing | The editor can preview audio, capture line timestamps, shift LRC line and word timestamps and save lyrics through its local manager. The local manager supports a user copy and writing supported embedded tags. The display recognizes readings in forms such as `漢字((かんじ))`. [Editor][editor] · [Timestamp utilities][timestamp] · [Local persistence][lyricstore] · [Reading parser][ruby] |
| Forced alignment | The editor invokes an ONNX MMS/CTC model using decoded local audio and supplied lyrics. It resamples, romanizes/tokenizes text, aligns inferred frames to the text and produces timed output. It uses Android media decoding and an FFmpeg fallback. [Editor call site][editorvm] · [Aligner][ctc] · [Fallback][ffmpeg] · [Model card][model] |
| Heuristic synchronization | The providers labelled Heuristic Sync estimate time from text weights and duration. The character mode distributes estimates within each line. These routes run without PCM or model inference. The separate PLaMo route translates lyrics. [Estimator][heuristic] · [Built-in providers][builtins] · [Translation][translate] |
| Cache | The fetch cache is in memory with 24-hour success and five-minute negative entries. The editor's saved lyrics and tag writes have independent lifetimes. For Swiftify, include provider, stable recording ID, version and rights in cache keys and limits. [Fetch cache][lyriccache] · [Local persistence][lyricstore] |

### Provider access and lyric rights

- **User-supplied LRC, TTML and tags** are the starting point for the desktop
  implementation. Its offset store keeps a hash of the track URI and an
  integer, and it copies no lyric text. The user controls the source files.
- **LRCLIB and Unison** have repository adapters. LRCLIB documents public
  search and retrieval plus newer word timing. Client/API availability and
  publication rights for the returned lyrics need separate checks. Unison
  uses the Unison Community License for newer datasets and API use. Its
  published terms cover eligible public open-source projects when Unison
  lyrics are free to their users. They require linked Unison and submitter
  attribution for API display and document ETag revalidation.
  [Registry][builtins] · [LRCLIB API][lrclib] · [Unison terms][unison]
- **PetitLyrics, KuGou, Kugeci, QQ Music, NetEase and Followlyrics** have
  implemented catalog clients. **Uta-Net, UtaTen, J-Lyric, JOYSOUND and
  myTyping** use provider-specific lookup/scrape routes. **Genius** uses search
  and a page parser. Each route needs authorization for automated access,
  display, caching, derived timing and export before adoption. The upstream
  code demonstrates the routes. Its GPL covers its implementation.
  PetitLyrics and Uta-Net publish their own usage terms. Tencent requires
  the product-specific agreements for QQ Music and KuGou. For the other
  adapters in this group, the review did not establish conditions permitting
  a third-party client. [Registry][builtins] · [Web clients][webclients] ·
  [Genius adapter][genius] · [PetitLyrics terms][petit] · [Uta-Net terms][utanet]
  · [Tencent agreement][tencent]
- **lrc.red, BiniLyrics, BetterLyrics, BetterLyrics Portato, LyricsPlus and
  PaxSenix** include intermediary routes. **Musixmatch and Spotify** are
  accessed directly or through provider-specific adapters. Check both the
  operator's terms and the underlying lyric rights. A proxy API key controls
  access to that API. Apple MusicKit use is subject to Apple's agreement.
  [Registry][builtins] · [Apple developer agreement][apple] ·
  [Spotify developer policy][spotify]
- **YouTube captions and YouTube Music lyrics** use the playing video ID.
  Availability is separate from permission to fetch or republish track
  lyrics. The existing Swiftify YouTube Data API OAuth scope is for a
  read-only catalogue. [YouTube lyrics adapter][ytlyrics] · [YouTube API
  policies][ytpolicy]

## Audio, library and queues

| Feature | What the source does |
| --- | --- |
| Parametric EQ and AutoEQ | Media3's custom processor builds global and per-channel biquads for the input format and supports preamp and dither. The AutoEQ dialog reads a local index, fetches bounded GraphicEQ data and approximates it. Profile libraries have separate measurement and data terms. [EQ processor][eq] · [AutoEQ dialog][autoeq] · [AutoEq project][autoeqproject] |
| ReplayGain | TagLib values feed an audio processor. The optional scanner uses MediaCodec PCM, integrated loudness and sample peak, and persists measurements. Swiftify already has tag-based gain, EQ, pitch, tempo and output code. Extend those Rust paths before considering a scanner. [Gain reader][gain] · [Scanner][gainscan] · [Playback service][service] |
| Gapless, crossfade and Automix | Media3 provides queue transitions. The bit-perfect sink has a separate gapless boundary, and the crossfade controller runs two players. Automix analyzes PCM via native spectral and resampling code and ONNX beat/vocal models. Native tempo analysis uses onset autocorrelation and follows the beat grid. A planner selects a transition style. Existing sources contain the paths. Continuous output on a given device requires measurement. [Crossfade controller][crossfade] · [Planner][planner] · [Native analyzer][analyzer] · [Bit-perfect routing][routing] |
| Rate, pitch, device profiles and statistics | PlaybackService sets Media3 playback parameters and disables them on bit-perfect routes. App settings and audio effect manager bind profiles to routes. NerdStats gets the declared stream plus the decoder's active format rather than guessing from its URL. Oboe and libusb power distinct output routes. [Service][service] · [Effects][effects] · [NerdStats][stats] · [JNI build][jni] |
| Queues and positions | Named snapshots use Android `AtomicFile` and include ordered tracks, active index and position. Queue history and long-track resume are separate stores. The latter restores only sufficiently long tracks and clears positions near their endpoints. Swiftify's existing playback-state serialization is the starting point for bounded, atomic queue snapshots. [Snapshots][queues] · [History][qhistory] · [Resume call sites][service] |
| History, playlists and artwork | ListeningStats stores listening events and supports encrypted backup. The local playlist store has separate playlist ordering and track ordering. Artwork prefetch and caches sit between media IDs, tags and the UI. [Listening stats][listening] · [Playlists][playlists] · [Prefetch][art] · [Local artwork cache][artcache] |
| Metadata and other services | TagLib writes through Android descriptors and storage permissions. Jellyfin and Navidrome implement catalog, stream and artwork under a common remote-source API. Jellyfin uses token auth, Navidrome uses OpenSubsonic auth with a guarded fallback. Swiftify can use documented server APIs with its own credentials and bounded transport. [Tag writer][tags] · [Server adapters][remote] |
| Desktop projects | `linux-offline` is the maintained local BitChord Compose derivative. It preserves local playback, library and some lyric settings and contains shared networking code. Presence in the tree alone does not show that the offline build enables the network route. `linux-desktop` is a distinct JavaFX prototype. It launches an mpv child process over a Unix socket, reads bounded sidecar LRC files and writes local M3U8 playlists through a temporary file and rename. Neither desktop architecture should be adopted wholesale. [Compose desktop][compose] · [Java sources][java] |

## YouTube Music

The Android `YouTubeSource` delegates typed search to `YtMusicRepository`;
browse/album/playlist rendering flows through the Innertube endpoint models
and parser. Android obtains a web session in its account flow and stores
session material in the auth store. Its native Media3 playback service uses a
separate resolver. InnerTubeX and NewPipeExtractor participate in resolving
streams. The resolver coalesces requests, expires cached stream URLs on a
monotonic clock, puts temporary limits on repeat failures and drops refused
URLs. Queue state and lyrics are connected by the video ID. [Search adapter][yt]
· [Repository][ytcatalog] · [Innertube parser][ytparser] · [Auth store][auth]
· [Resolver][resolver] · [Service][service] · [YouTube lyrics][ytlyrics]

These are source-verified Android paths. The official YouTube Data API can
supply catalog metadata. Its developer policies limit audio/video separation,
modification and background playback. Web session extraction and internal
resolution do not provide an authorized Swiftify audio interface. Keep the
provider-independent engine useful for local files and authorized streams.
For a future service adapter, establish media rights and an approved interface
before implementing playback. [Policies][ytpolicy]

## Provenance and distribution

- The surveyed repositories publish GPL-3.0. Credit **Kdroidwin and the
  MusicBeatSP contributors**, **SamuelAdmand and the MusicBeat contributors**,
  and **Kushagra Singh and the BitChord contributors**. The music and lyric
  implementations also include changes from other forks. The native Automix
  source retains named copyright for SFG545's Orchard and Singh's BitChord
  adaptation. Some lyric search adapters credit Tinque. Kotlin, Java, JNI,
  model files, artwork, lyrics and Android binaries from these projects were
  referenced for design only. [MusicBeatSP license][beatlicense] ·
  [MusicBeat license][musicbeat] · [BitChord license][bitchord] ·
  [Analyzer notices][analyzer] · [Search adapter attribution][builtins]
- Dependency terms are independent. AndroidX Media3, Ktor and QuickJS KT are
  Apache-2.0, jsoup and AutoEq code are MIT, and the Kotlin TagLib wrapper is
  Apache-2.0. ONNX Runtime Android 1.28.0 declares MIT in its published POM.
  TagLib offers LGPL-2.1 and MPL-1.1 terms. A port would need a compatible
  licensing route. NewPipeExtractor and InnerTubeX publish GPL-3.0. The
  exact artifact and transitive NOTICEs require review before importing
  code, including the pinned nanojson fork and native runtime components.
  [Media3][media3] · [Ktor][ktor] · [QuickJS KT][quickjs] · [jsoup][jsoup] ·
  [AutoEq license][autoeqlicense] · [TagLib wrapper][tagwrap] · [TagLib][taglib] ·
  [ONNX Android POM][onnx] · [NewPipeExtractor][newpipe] · [InnerTubeX][inner]
- The forced-alignment ONNX model card declares **CC BY-NC 4.0**. Runtime
  licensing does not grant model redistribution rights. The FFmpeg fallback
  needs a build-specific codec and license review. Automix and Music2Emo
  weights, source samples and generated profiles each have their own
  provenance. Bundle none of these artifacts under the application's GPL.
  [MMS model card][model] · [FFmpeg legal][fflegal] · [Music2Emo assets][models]
- If a future GPL-compatible source port is made, record the exact revision,
  retain file copyright/license headers, identify changes and satisfy
  corresponding-source requirements. For Apache-2.0 dependencies retain
  applicable LICENSE and NOTICE text, and for MPL-2.0 verify whether the
  secondary-license exclusion applies and provide the covered source. Check
  the generated notices inside Windows, Linux and macOS packages before
  distributing them. These source references introduce no MusicBeatSP
  runtime dependency or bundled artifacts. [GNU GPL compatibility][gpl] ·
  [Apache compatibility][apache] · [MPL-2.0][mpl]

## Swiftify sequencing

1. Move the local path toward one render worker and PCM clock. Carry media
   time through rate, tempo and queue processing, and pass a tagged snapshot
   through the existing C ABI. Use presentation timestamps for lyrics and
   preserve explicit restart after device failure.
2. Add local Enhanced LRC and TTML word cues above a common cue model, then
   karaoke rendering and an editor. Validate paused/playing seek, signed
   offsets, tempo changes, underruns and output latency. Persist user edits
   independently of future provider caches.
3. Reuse the queue and DSP in small stages for named queues, local preloading,
   gapless transitions, configurable EQ and output diagnostics. Automix and
   alignment require separate model provenance and accuracy tests.
4. Consider a documented self-hosted source such as Jellyfin or OpenSubsonic
   after authorizing access. Implement cancellable reads with deadlines,
   bounded retries, stable recording identity and seek rollback. Keep future
   service licensing and account integration separate from the transport.

[gradle]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/build.gradle.kts
[jni]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/cpp/CMakeLists.txt
[builtins]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/BuiltinLyricsSources.kt
[fetch]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/LyricsFetchEngine.kt
[extensions]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyrics/engine/LyricsExtensionExecutor.kt
[scorer]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyricseditor/domain/model/LyricsMatchScorer.kt
[editorvm]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyricseditor/ui/viewmodel/LyricsEditorViewModel.kt
[parse]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/ProviderLyrics.kt
[ttml]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/TtmlLyrics.kt
[line]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/LyricLine.kt
[nowplaying]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/ui/player/NowPlayingScreen.kt
[clock]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/ui/player/LyricClock.kt
[offset]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/linux-offline/desktopApp/src/main/kotlin/com/music/bitchord/desktop/DesktopPlayerHost.kt
[editor]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyricseditor/ui/LyricsEditorScreen.kt
[timestamp]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyricseditor/ui/LyricsTimestampEditor.kt
[lyricstore]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyricseditor/data/LocalLyricsManager.kt
[ruby]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/LyricsFurigana.kt
[ctc]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/CtcForcedLyricsAligner.kt
[ffmpeg]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/CtcFfmpegDecoder.kt
[heuristic]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/providers/CtcLrcGenerator.kt
[translate]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/lyrics/PlamoLyricsTranslation.kt
[lyriccache]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/LyricsFetchCache.kt
[webclients]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/providers/WebScrapeLyricsClients.kt
[genius]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/Genius.kt
[ytlyrics]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/lyrics/YouTubeLyrics.kt
[eq]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/ParametricEqAudioProcessor.kt
[autoeq]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/ui/screens/equalizer/AutoEqProfilesDialog.kt
[gain]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/ReplayGainReader.kt
[gainscan]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/ReplayGainScanner.kt
[service]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/PlaybackService.kt
[crossfade]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/CrossfadeController.kt
[planner]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/smart/TransitionPlanner.kt
[analyzer]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/native/analyzer/audio_analysis.cpp
[routing]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/audio/bitperfect/BitPerfectRoutingSink.kt
[effects]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/equalizer/AudioEffectManager.kt
[stats]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/NerdStats.kt
[queues]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/SavedQueueStore.kt
[qhistory]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/QueueHistory.kt
[listening]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/stats/ListeningStats.kt
[playlists]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/localmusic/data/LocalPlaylistStore.kt
[art]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/playback/PlaybackArtworkPrefetcher.kt
[artcache]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/localmusic/coil/LocalArtworkCache.kt
[tags]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/feature/tageditor/data/TagLibWriter.kt
[remote]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/sources/RemoteMusicServerSource.kt
[compose]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/linux-offline/README.md
[yt]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/sources/YouTubeSource.kt
[ytcatalog]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/YtMusicRepository.kt
[ytparser]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/innertube/InnertubeParser.kt
[auth]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/auth/AuthStore.kt
[resolver]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/app/src/main/java/com/music/bitchord/data/innertube/StreamResolver.kt
[beatlicense]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/LICENSE
[models]: https://github.com/Kdroidwin/MusicBeatSP/blob/abcac36660db6096d76db9ebed71d65637bd522a/model-assets/README.md
[beat]: https://github.com/Kdroidwin/MusicBeatSP/tree/abcac36660db6096d76db9ebed71d65637bd522a
[java]: https://github.com/Kdroidwin/MusicBeatSP/tree/abcac36660db6096d76db9ebed71d65637bd522a/linux-desktop/src/com/samuel/musicbeat/desktop
[model]: https://huggingface.co/romara-labs/mms-300m-1130-forced-aligner-ONNX/tree/f515b917cf1f4f14d12a4768940cd8b4eb3e2119
[lrclib]: https://lrclib.net/docs
[apple]: https://developer.apple.com/support/terms/apple-developer-program-license-agreement/
[ytpolicy]: https://developers.google.com/youtube/terms/developer-policies#iii-e-handling-youtube-data-and-content
[autoeqproject]: https://github.com/jaakkopasanen/AutoEq
[musicbeat]: https://github.com/SamuelAdmand/MusicBeat/blob/3a7743d4ad348fbe59b01e7bcbd7054b5e130901/LICENSE
[bitchord]: https://github.com/kushagrasinghx/BitChord/blob/21356206de5594ca0bdaf4680ca2db68a2287729/LICENSE
[media3]: https://github.com/androidx/media/blob/release/LICENSE
[ktor]: https://github.com/ktorio/ktor/blob/main/LICENSE
[quickjs]: https://github.com/dokar3/quickjs-kt/blob/main/LICENSE.txt
[jsoup]: https://github.com/jhy/jsoup/blob/master/LICENSE
[autoeqlicense]: https://github.com/jaakkopasanen/AutoEq/blob/master/LICENSE
[tagwrap]: https://github.com/Kyant0/taglib/blob/main/LICENSE
[taglib]: https://taglib.org/
[newpipe]: https://github.com/TeamNewPipe/NewPipeExtractor/blob/v0.26.3/LICENSE
[inner]: https://github.com/MetrolistGroup/innertubex/blob/v0.7.0/LICENSE
[fflegal]: https://ffmpeg.org/legal.html
[gpl]: https://www.gnu.org/licenses/license-compatibility.html
[apache]: https://www.apache.org/licenses/GPL-compatibility
[mpl]: https://www.mozilla.org/MPL/2.0/

[unison]: https://github.com/better-lyrics/unison/blob/master/LICENSE-DATA.md
[petit]: https://petitlyrics.com/contents/kiyaku
[utanet]: https://www.uta-net.com/info/kiyaku.html
[tencent]: https://www.tencentmusic.com/en-us/protocol.html
[spotify]: https://developer.spotify.com/policy
[onnx]: https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.28.0/onnxruntime-android-1.28.0.pom
