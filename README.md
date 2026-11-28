<div align="center">

<img src="packaging/icons/swiftify-256.png" width="128" alt="Swiftify icon">

# Swiftify

A Qt Quick desktop music player with a Rust audio engine and Material 3 Expressive UI.
This fork is developing fully native YouTube Music playback alongside a local music library.

</div>

## Development status

`integration` is the development and trial branch. It provides local playback and
an optional Spotify integration. Native YouTube Music playback and service
integration in the main window are pending. See the
[backend investigation](docs/youtube-music-backend.md) for playback evidence and
[AGENTS.md](AGENTS.md) for the requirements. `main` tracks
[upstream Swiftify](https://github.com/swiftify-org/swiftify).

The desktop application includes a local library and playlists, metadata
editing, visualizers, EQ, ReplayGain, independent tempo and pitch, and
synchronized lyrics. The Linux target uses Wayland, MPRIS and PipeWire through
the desktop's audio compatibility layers. CachyOS with Hyprland is the primary
trial environment.

<table>
  <tr>
    <td><img src="assets/eq.png" alt="Equalizer, tempo and pitch settings" width="400"></td>
    <td><img src="assets/immersive.png" alt="Immersive player with visualizer and lyrics" width="400"></td>
  </tr>
</table>

## Try on CachyOS

The Linux packaging workflow produces the `linux-disabled` Artifact for a
trial with Spotify excluded at build time. The [CachyOS trial guide](docs/cachyos-trial.md)
covers download, checksum, a separate profile and hardware checks.

Start Swiftify and add local audio through the library. To display local lyrics,
place a same-stem `.lrc` or `.ttml` sidecar next to the recording. Enhanced
LRC and TTML support word cues, and the lyrics controls save timing corrections
per track. See [synchronized lyrics](docs/synchronized-lyrics.md) for file
selection and timing behavior.

## Build from source

Use an `integration` checkout. The build needs CMake 3.24+, Ninja, a C++20
compiler, stable Rust, Qt 6.12.1+ (Base, Declarative and Shader Tools), and QCoro
Core and Network for Qt 6. Qt Test and QuickTest are required when testing.
On Linux, install the ALSA and PulseAudio development libraries and a Wayland
platform plugin for Qt. The first configure fetches Corrosion and fonts
with pinned revisions or hashes.

For an Arch-based system with the required Qt version available:

```sh
sudo pacman -S --needed base-devel git cmake ninja rust pkgconf qcoro \
  qt6-base qt6-declarative qt6-shadertools qt6-wayland libpulse alsa-lib
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_SPOTIFY=OFF
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

For a portable Linux x86_64 package, use a Qt kit with Wayland and Shader Tools.
The `--test` option runs the Qt tests with Xvfb.

```sh
./tools/build-linux-tarball.sh --qt "$QT_ROOT_DIR" --out dist --without-spotify --test
```

Other build paths are in the [Debian helper](tools/build-debian.sh),
[Windows helper](tools/build-windows.ps1) and [macOS packager](tools/build-macos-dmg.sh).
Use `CMAKE_PREFIX_PATH` with a compatible Qt kit when distribution packages are
older. Linux desktop integration is enabled by default and can be configured
with `SWIFTIFY_DESKTOP_INTEGRATION`.

### Optional Spotify

Build with `-DENABLE_SPOTIFY=ON` to include the Spotify integration.
It requires QtKeychain, OpenSSL on Linux, and Qt WebSockets when Soloist is
enabled. On Linux, account storage uses a Secret Service provider. The
application opens the local workspace by default, including in builds with
Spotify support. Quit the running window before switching modes.

Launch `swiftify --enable-spotify` to open the Spotify workspace. The
`SWIFTIFY_ENABLE_SPOTIFY=1` environment variable sets the same default, and
`--disable-spotify` overrides it. Spotify playback through librespot requires
Spotify Premium. The Spotify account screen accepts a Spotify client ID with
the redirect URI `http://127.0.0.1:8898/login`. See
[upstream Spotify setup](https://github.com/swiftify-org/swiftify#before-you-start)
for service-specific instructions.

Choose Japanese in Settings → Language or use a Japanese system locale. Qt
uses the system font fallback for Japanese glyphs.

## Playback controls

An existing Swiftify process handles these commands from a second invocation.

| Flag | Action |
| --- | --- |
| `--immersive` | Open the immersive player |
| `--play-pause` | Play or pause |
| `--next` | Play the next track |
| `--previous` | Play the previous track |
| `--stats` | Open playback statistics |

For example, these Hyprland bindings open the immersive player and control playback.

```ini
bind = SUPER SHIFT, M, exec, swiftify --immersive
bind = , XF86AudioPlay, exec, swiftify --play-pause
```

## Development

See [Contributing](CONTRIBUTING.md), the
[native playback guide](docs/native-playback.md) and the
[historical records](docs/history/README.md). Alignment Lab is an optional
tool built separately from Swiftify. [Alignment Lab](tools/alignment-lab/README.md)

## Authors and license

Swiftify was developed by [shred0s](https://github.com/shred0s) and the
[upstream contributors](https://github.com/swiftify-org/swiftify/graphs/contributors).
Swiftify is licensed under GPL-3.0-or-later. [License](LICENSE) · [Dependency notices](packaging/licenses/)
