# Vista Rooms manual

Vista Rooms is an algorithmic reverb built for realistic rooms and halls. It
has two sections: **Reflections**, the early sound of the room's walls, and
**Reverb**, the diffuse tail. These are followed by EQ, dynamics and a gate.
It doesn't need any sample files.

## Presets

There are 471 factory presets in 9 categories, plus **Init**:

| Category | For |
| --- | --- |
| Halls | Concert and scoring halls, orchestral and cinematic tails |
| Chambers | Echo chambers and dense, bright small spaces |
| Rooms | Natural rooms of every size |
| Studios | Live rooms and stages for drums, vocals and ensembles |
| Ambiences | Short, mostly early sound that adds space without a tail |
| Spaces | Large and unusual spaces: churches, caves, outdoor |
| Post | Real-world locations for dialogue and sound design (offices, cars, next room…) |
| Nonlinear | Gated and reverse-shaped effects |
| Mix Tools | Utility settings for busses and mixing |

Use **Save** to store your own presets. They're kept as files (see
INSTALL.md for the folder).

## Reflections

| Control | Range | What it does |
| --- | --- | --- |
| Proximity | 0–100 % | How close the source sounds. Low settings put strong reflections straight after the dry sound. Higher settings push the source back by delaying the build-up of reflections. |
| Reflectivity | 0–100 % | How hard the walls are: the level and brightness of the reflections. |
| Size | 0–100 % | Size of the space the reflections come from. |
| Pattern | Room, Studio, Chamber, Hall, Stage, Cathedral, Scattered, Nonlin U | The geometry of the reflections. |
| Diffusion | 0–100 % | From discrete echoes to a smooth wash. |
| Roll-off / Slope | 1–20 kHz, 6–24 dB/oct | Low-pass on the reflections. |
| Modulation / Chorus | 0–100 %, Off/Fine/Light/Moderate | Movement in the reflections. |
| X-Feed / Delay / Roll-off | 0–100 %, 0–100 ms, 1–20 kHz | How much each input side reaches the opposite output. |

## Reverb

| Control | Range | What it does |
| --- | --- | --- |
| Time / Infinite | 0.2–45 s | Mid-frequency decay time. **Infinite** freezes the tail. |
| Bass Multiply / Crossover | 0.25–4× / 50–2000 Hz | Low-frequency decay relative to Time. |
| Treble Multiply / Crossover | 0.1–2× / 1–16 kHz | High-frequency decay relative to Time. Lower is darker and more natural for large halls. |
| Low Boost | 0–12 dB | Adds weight to the bottom of the tail. |
| Pre-delay (+ Sync) | 0–500 ms or note values | Gap before the tail starts. |
| Bloom | 0–100 % | How slowly the tail builds up. Higher swells in gradually. |
| Undulation | 0–100 % | Slow, natural level movement in the tail. |
| Size | 0–100 % | Density and scale of the tail. |
| Roll-off / Slope | 1–20 kHz, 6–24 dB/oct | Low-pass on the tail. |
| Modulation / Chorus | 0–100 %, Light/Moderate/Ease-in | Pitch movement in the tail. **Ease-in** brings it in gradually as the tail decays. |
| X-Feed / Delay / Roll-off | as Reflections | Stereo cross-feed for the tail. |
| Echo Time / Sync / Level / Roll-off | 100–2000 ms, note values, 0–100 %, 1–20 kHz | A delay feeding the tail, for rhythmic or slap effects. |

## Output, EQ and dynamics

- **Reflections / Reverb:** balance between the two sections.
- **Width:** stereo width of the wet signal.
- **Mix:** dry/wet. Use 100 % on an aux/send.
- **Gain:** output level, −24 to +12 dB.
- **EQ:** low cut, a low shelf, a mid bell, a high shelf and a roll-off, all
  on the wet signal. **EQ On** bypasses it.
- **Dynamics Mode:**
  - *Duck Reverb* lowers the tail while the input is loud.
  - *Duck Wet* lowers the whole wet signal while the input is loud.
  - *Compress Wet* evens out the wet level.
  - Threshold, Ratio, Knee, Attack, Release and Trim set how it behaves.
- **Gate:** cuts the tail when the input drops below Threshold, for gated
  drum sounds or to clean up between phrases. Hold, Range, Open, Close and
  Hysteresis shape it.

## Tips

- On a send, set **Mix** to 100 % and **Proximity** around 30–50 %, then
  choose a Hall or Room preset. Lower **Proximity** sounds closer; raise it
  for more distance.
- For drums, start with a Studios or Rooms preset. A short Time with high
  Reflectivity gives punch without wash.
- CPU use is about 10–17 % of one core, depending on the preset.
