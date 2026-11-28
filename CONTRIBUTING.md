# Contributing

Swiftify uses C++ and Qt Quick for its Material 3 Expressive desktop UI, with
Rust for playback. See [AGENTS.md](AGENTS.md) for the product, technical and
verification requirements.

Use an up-to-date `integration` checkout for development. `main` tracks upstream.
Check existing issues for related work. Report substantial design or dependency
changes before implementation.

## Building

the requirements and the steps for each platform are in the
[readme](README.md#building-from-source). for development keep the tests on

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

The first configure downloads Corrosion and two fonts and verifies their
pinned revisions or hashes. See the [Windows helper](tools/build-windows.ps1)
for a Windows build.

## Where things are

| Path | Contents |
| --- | --- |
| `src/` | App, settings, library, playback controls, service clients, Rust bridge and system integration |
| `qml/` | Qt Quick UI, Material 3 design system, views, player, visualizers and settings |
| `shaders/` | GLSL compiled to `.qsb` by `qt_add_shaders` |
| `native-audio/` | Shared Rust output, media input, decoding and render queue |
| `spotify-core/` | Rust desktop playback and DSP plus the optional Spotify integration |
| `translations/` | Qt translation catalogs |
| `tools/shape_atlas/` | Material 3 shape atlas generator |
| `tools/alignment-lab/` | Optional standalone alignment tool |
| `third_party/` | Material Color Utilities and rounded-polygon-qmljs |
| `tests/` | Qt Test suites, rendering benchmark and static checks |
| `packaging/` | Packaging definitions, desktop entry, icons and license notices |

## Tests

every test has to pass and the build has to be free of warnings.

- `tst_views` draws every view on the gpu and fails on any qml warning, so a warning is a bug
- `tst_views` also checks that nothing animates at rest, add new views to it
- `icon_names` checks that every material symbol named in qml exists in the pinned font
- `qml_statements` catches script lines that open with `(` or `[`
- `bench_frames` measures what a frame costs, ctest skips it, run it by hand when you touch timers or frame requests

lint the qml you changed. leave nothing new besides "can be shadowed" or "could
not compile" notes

```sh
qmllint --compiler warning -I build/qml_modules <files>
```

the rust core can be tested on its own

```sh
cargo test --locked --manifest-path native-audio/Cargo.toml
cargo test --locked --manifest-path spotify-core/Cargo.toml --no-default-features --features capi
```

## Rules

### keep it light

Measure CPU and memory as well as rendering behavior when changing playback or
UI code. Keep audio work off the Qt UI thread and network operations off the
real-time audio callback.

- an idle or hidden window should cost close to nothing
- dont add timers, polling or per frame work when a signal or binding does the job
- free images, buffers and models when a view goes away instead of keeping them around
- if a change touches rendering, run `bench_frames` before and after and record the resulting frame counts and timing
- if a feature costs a lot, make it something people can turn off


### nothing animates at rest

when nothing plays and nobody touches the window, nothing on screen should move.
animations are fine in two cases

- on hover or interaction, and they stop when it ends
- during playback, for things like the spinning shapes and squiggles. drive these off `UiState.spin` instead of your own timer or `NumberAnimation`. it only turns while music plays and the window is visible, so they stop on pause or when the window is hidden. it also ticks at 30 hz so slow motion doesnt redraw the window at full rate

- never lower an existing frame rate, compare frame counts before and after changing timers or frame requests

### qml

- use `pragma ComponentBehavior: Bound` and typed properties and functions so the AOT compiler takes them
- animations respect `Appearance.reduceMotion` and `Appearance.duration()`
- new qml files are picked up by glob, you dont need to list them

### shapes and shaders

- draw shapes with `MaterialShape` or `ShapeIcon`, by name from `qml/design/Shapes.qml`, never as hand drawn paths
- shape names follow the atlas order in `tools/shape_atlas/ShapeAtlasLayout.h`, a test checks this
- a `ShaderEffect` has to declare a property for every uniform in its shader, or qt warns and the tests fail

### license

- a new source file starts with the gpl notice every other file has, copy it from one in the same language
- code in `third_party` and `spotify-core/vendor` keeps its own license, dont put the notice there

### no filler

a note or description in the ui has to tell the user something the label
doesnt. a cache setting doesnt need a note saying it caches songs. the same goes for code comments.

## Commits

Describe the change, how it was tested and the tested platform. Include
screenshots for UI work when they help verify layout or behavior. Keep the
commit history and AI co-author trailer consistent with
[the repository policy](AGENTS.md#変更と履歴).
