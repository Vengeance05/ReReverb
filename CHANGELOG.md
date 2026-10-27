# Changelog

This project uses [semantic versioning](https://semver.org): MAJOR.MINOR.PATCH.

- **Patch (1.0.x):** bug fixes only. Presets and sessions are unaffected.
- **Minor (1.x.0):** new features or presets. Old sessions still load and
  sound the same.
- **Major (x.0.0):** may change how existing sessions sound. Release notes
  will say so.

## [1.0.0] - unreleased

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
