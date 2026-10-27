# Changelog

This project uses [semantic versioning](https://semver.org): MAJOR.MINOR.PATCH.

- **Patch (1.0.x):** bug fixes only. Presets and sessions are unaffected.
- **Minor (1.x.0):** new features or presets. Old sessions still load and
  sound the same.
- **Major (x.0.0):** may change how existing sessions sound. Release notes
  will say so.

## [1.0.1] - 2026-10-27

### Septima
- New first-run screen: until it finds the impulse responses, Septima shows
  setup steps with **Download IR library**, **Choose folder**, **Open default
  folder** and **Rescan** buttons, and says which folder it looked in.
- The default IR folder is preferred when it actually contains IRs.

### Vista Rooms
- Removed faint ticks when switching Dynamics Mode, the EQ low cut on or off,
  the roll-off slopes and the reflection pattern while audio is playing.
  Switches now crossfade with smooth S-curves, and quick back-and-forth
  changes no longer cut a fade short.

### Both
- Sessions now record an engine version, so future sound changes can keep
  old projects sounding as they were made.
- No change to how existing presets and sessions sound. This is checked
  automatically against renders of 1.0.0.

### Behind the scenes
- Every build now runs pluginval (strictness 10) on both platforms, auval
  on the Mac AUs, a parameter-compatibility check and golden-render checks.
- Bug report and feature request forms on GitHub Issues.
- Faster test builds. Releases are still universal and fully optimised.

## [1.0.0] - 2026-10-27

First public release.

### Septima
- True-stereo convolution reverb for the free Samplicity IR library (134
  programs). The library is downloaded separately.
- Decay reshaping (0.2–30 s), low/high decay multipliers, 32 synthetic
  early-reflection sets, VLF, Early/Late balance, pre-delay, delay feed,
  roll-offs, modulation, tail motion and a 4-band EQ.
- 134 factory presets, one per program.
- **IR Library…** accepts the whole unzipped download folder; a default IR
  folder is searched automatically.

### Vista Rooms
- Algorithmic room and hall reverb with separate Reflections and Reverb
  sections, EQ, ducking/compression and a gate.
- 471 factory presets in 9 categories.

### Platforms
- macOS 11 or later (universal: Apple Silicon and Intel), AU and VST3.
- Windows 10/11 64-bit, VST3.
