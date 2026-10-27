# Rereverb: Septima + Vista Rooms

Two free reverb plugins for macOS (AU + VST3), built with JUCE from original DSP.

- **Septima**: a convolution reverb built around a library of hardware-reverb
  impulse responses. It adds Decay time (lengthen or shorten), bass and high
  decay multipliers, 32 synthetic early-reflection sets, VLF level,
  Early/Late balance, pre-delay, delay feed, early/late roll-offs, tail
  modulation and a 4-band master EQ. It ships **without** impulse responses:
  you download the free IR library yourself (see below).
- **Vista Rooms**: an algorithmic room and hall reverb.
  - Reflections: Proximity, Reflectivity, Size, Pattern, Diffusion,
    roll-off, modulation/chorus and crossfeed.
  - Reverb: Time 0.2–45 s plus Infinite, Contour, Low Boost, Pre-delay,
    Bloom, Undulation, Size, roll-off, chorus modes, crossfeed and Echo.
  - A 5-band EQ, ducking/compression and a tail gate.
  - 471 factory presets (Halls, Chambers, Rooms, Studios, Ambiences,
    Spaces, Post, Nonlinear, Mix Tools), plus Init.

## Septima: getting the impulse responses

Septima needs Samplicity's free Bricasti M7 impulse response library. Its
licence does not allow anyone to redistribute it, so it isn't in this
repository or in the installer.

1. Download **"Samplicity – Bricasti IRs version 2023-10.zip"** (about 960 MB)
   from Samplicity's downloads page: <https://samplicity.com/downloads/>
2. Unzip it anywhere, for example in `~/Downloads`.
3. Point Septima at it in either of these ways:
   - **IR Library… button** (easiest): open Septima, click **IR Library…**
     and choose the unzipped folder. You can choose the top-level
     `Samplicity - Bricasti IRs version 2023-10` folder or any of its
     sub-folders. Septima scans sub-folders, removes the duplicate copies
     (44.1/48 kHz, quad versus left/right files) and keeps one 48 kHz version
     of each of the 134 programs. The folder is remembered for every
     instance and every DAW.
   - **Default folder**: copy the WAVs from the
     `…quad channel files, 48 Khz` sub-folder into
     `~/Library/Application Support/Rereverb/Septima/IRs/` (just for you) or
     `/Library/Application Support/Rereverb/Septima/IRs/` (all users).
     Septima looks there automatically when no folder has been chosen.

Septima's 134 factory presets correspond one-to-one with the library's
programs. Each opens its program at the settings it was captured at. Any
other folder of WAV/AIFF impulse responses also works: quad true-stereo,
L/R pairs, stereo and mono-to-stereo files are all recognised.

## Build

```sh
./build.sh
```

You need the Xcode command-line tools and CMake (`brew install cmake`). JUCE
8.0.4 is fetched into `dependencies/JUCE` the first time you build. The
plugins are installed to `~/Library/Audio/Plug-Ins/{VST3,Components}`; then
rescan in your DAW (in Logic, restart it).

## Installer package

```sh
./make_installer.sh
```

This builds universal (arm64 + x86_64) release plugins and creates
`dist/Rereverb-1.0.0.pkg`, which installs both plugins system-wide. It
contains no impulse responses.

For your own machine only, `./make_installer.sh --with-irs <IR folder>` also
installs your downloaded IRs into the default folder above. That produces
`dist/Rereverb-1.0.0-personal.pkg`. **Never distribute that package.**

## Repository layout

- `core/`: DSP that doesn't depend on JUCE. The tests are in
  `tests/rr_tests.cpp` and build to `rr_tests`.
- `plugins/common`, `plugins/septima`, `plugins/vista_rooms`: the JUCE plugins.
- `tools/plugin_render`, `tools/plugin_bench`, `tools/plugin_smoke`: an
  offline render host, a CPU benchmark and a smoke test for the built plugins.
- `legacy/`: an earlier prototype, kept for reference and not built.

User presets are saved to `~/Library/Audio/Presets/Rereverb/<plugin>/`.

Fonts: Inter, under the SIL Open Font License (`plugins/common/fonts/Inter-LICENSE.txt`).

Septima and Vista Rooms are independent projects. They aren't affiliated with
or endorsed by Samplicity, Bricasti Design or any other reverb maker, and the
product names above are used only to say which IR library to download.
