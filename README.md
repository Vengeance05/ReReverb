# Rereverb: Septima + Vista Rooms

Two free reverb plugins for **macOS (AU, VST3)** and **Windows (VST3)**:

- **Septima:** a true-stereo convolution reverb built around a classic
  hardware-reverb IR library. It can reshape the decay, swap early
  reflections, and adds modulation and EQ. It needs the free IR library,
  which you download separately (below).
- **Vista Rooms:** an algorithmic reverb for realistic rooms and halls, with
  separate Reflections and Reverb sections, EQ, ducking and a gate. It comes
  with 471 factory presets.

## Download and install

1. Download the latest installer from the **[Releases](../../releases/latest)**
   page: the `.pkg` for macOS or the `-Windows-Setup.exe` for Windows.
2. Install it. The plugins aren't code-signed, so you'll approve them once:
   - macOS: **System Settings → Privacy & Security → Open Anyway**.
   - Windows: **More info → Run anyway**.
3. **For Septima**, download the free IR library from
   <https://samplicity.com/downloads/>
   (*Samplicity – Bricasti IRs version 2023-10.zip*) and unzip it. In
   Septima, click **IR Library…** and choose the unzipped folder.

The full step-by-step guide is in **[docs/INSTALL.md](docs/INSTALL.md)**.

## Manuals

- [Septima](docs/SEPTIMA.md)
- [Vista Rooms](docs/VISTA_ROOMS.md)
- [Changelog](CHANGELOG.md)

## Why the IRs aren't included

Septima's impulse responses come from Samplicity's free library. Its licence
doesn't allow redistribution, so they aren't in this repository or in the
installers. You can point Septima at your own copy anywhere on disk. Any
other folder of WAV/AIFF impulse responses works too: quad true-stereo, L/R
pairs, stereo and mono-to-stereo files are all recognised.

---

## Building from source

### macOS
Requirements: Xcode command-line tools and CMake (`brew install cmake`).

```sh
./build.sh              # builds, runs the tests, installs into ~/Library/Audio/Plug-Ins
./make_installer.sh     # universal release build -> dist/Rereverb-<version>.pkg
```

`./make_installer.sh --with-irs <IR folder>` also installs your downloaded
IRs, for your own machine only. **Never distribute that package.**

### Windows
Requirements: Visual Studio 2022 or later (with the *Desktop development with C++*
workload), CMake and Git.

```bat
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git dependencies\JUCE
cmake -S . -B build -A x64 -DRR_COPY_AFTER_BUILD=OFF
cmake --build build --config Release
build\Release\rr_tests.exe
```

The VST3s are built to `build\Septima_artefacts\Release\VST3\` and
`build\VistaRooms_artefacts\Release\VST3\`. To build the installer, install
[Inno Setup 6](https://jrsoftware.org/isinfo.php) and run
`iscc installer\windows\rereverb.iss`.

### Continuous integration and releases
GitHub Actions (`.github/workflows/build.yml`) builds and tests both
platforms on every push and pull request. To publish a release:

1. Set the version in `CMakeLists.txt` (`project(Rereverb VERSION x.y.z)`).
2. Add a `## [x.y.z]` section to `CHANGELOG.md`.
3. Commit, tag and push:
   ```sh
   git tag vx.y.z && git push origin main vx.y.z
   ```

The workflow builds everything and creates the GitHub Release with the
macOS `.pkg` and `.zip` and the Windows `Setup.exe` and `.zip` attached.

### Compatibility rules (keep old sessions sounding the same)
- Never remove or rename a parameter ID, and only add choice-list entries at
  the end. `tools/check_params.py` enforces this (run with `--update` after
  adding parameters).
- Changes must not alter how existing settings sound.
  `VistaRooms_Golden check tests/golden/vista_rooms.txt` and
  `rr_golden_core check tests/golden/septima_core.txt` compare renders against
  1.0.0. If a change to the sound is intended, bump `kEngineVersion` in the
  processor, keep the old behaviour for sessions with a lower
  `engineVersion()`, and only then regenerate the golden files with `update`.

### Repository layout
- `core/`: DSP that doesn't depend on JUCE (reverb engines, IR reshaping, IR
  library scanning). Tests are in `tests/rr_tests.cpp`.
- `plugins/common`, `plugins/septima`, `plugins/vista_rooms`: the JUCE plugins.
- `installer/windows`: the Inno Setup script.
- `tools/plugin_render`, `tools/plugin_bench`, `tools/plugin_smoke`: an
  offline render host, a CPU benchmark and a smoke test for built plugins.
- `tools/golden_render`, `tests/golden_core.cpp`, `tests/golden/`: the
  session-compatibility checks. `tools/editor_snapshot` saves PNGs of the
  editors (`-DRR_BUILD_SNAPSHOT=ON`).
- `legacy/`: an earlier prototype, kept for reference and not built.

## Licence

Copyright © 2026 Sam Hindmarch. Septima and Vista Rooms are free software,
released under the **GNU Affero General Public License v3.0 or later** (see
[LICENSE](LICENSE)). You can use the plugins in any music, commercial or not;
the licence only governs the code. If you distribute modified versions, you
must release their source under the same licence. JUCE is used under its
AGPLv3 open-source licence.

## Credits and notices

- Built with [JUCE](https://juce.com).
- Fonts: Inter, under the SIL Open Font License (`plugins/common/fonts/Inter-LICENSE.txt`).
- Septima and Vista Rooms are independent projects. They aren't affiliated
  with or endorsed by Samplicity, Bricasti Design or any other reverb maker.
  Product names are used only to say which IR library to download.
