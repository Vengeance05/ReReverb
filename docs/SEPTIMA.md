# Septima manual

Septima is a true-stereo convolution reverb. It plays back impulse responses
sampled from a classic hardware reverb and lets you reshape them: change the
decay time, adjust bass and treble decay, swap the early reflections, add
modulation and EQ. At its default settings, a program sounds the same as the
original capture.

You need the free IR library first. See [INSTALL.md](INSTALL.md#septimas-impulse-responses-required).

## Programs and presets

- **Program browser:** lists every program found in your IR folder, grouped
  into the library's categories (Halls, Plates, Rooms, Chambers, Ambience,
  Spaces and so on). Choosing a program loads its IR and sets **Decay Time**
  to the length it was sampled at.
- **Factory presets:** one per program (134), each at the program's captured
  settings. They're the quickest way to audition the library.
- **User presets:** use **Save** to store your own. They're saved as files
  (see INSTALL.md for the folder) and can be copied between machines.

## Controls

### Reverb shape
| Control | Range | What it does |
| --- | --- | --- |
| Decay Time | 0.2–30 s | Overall reverb time. Each program opens at its sampled decay; moving this lengthens or shortens the tail by reshaping the IR rather than time-stretching it, so pitch and density are kept. **Sampled** returns to the captured value. |
| Low Decay Multiply / Low Crossover | 0.25–4× / 50–1000 Hz | Decay below the crossover relative to Decay Time. Above 1× the low end rings longer; below 1× it's tighter. |
| High Decay Multiply / High Crossover | 0.25–4× / 1–16 kHz | Decay above the crossover. Below 1× gives a darker, more absorbent room; above 1× gives brighter air. |
| VLF | 0–200 % | Level of the very-low-frequency part of the tail. 100 % is as sampled. |

### Early reflections
| Control | Range | What it does |
| --- | --- | --- |
| Early Reflections | Sampled, Set 0–31 | **Sampled** uses the program's own early part. Sets 0–31 replace it with synthetic reflection patterns, from small rooms to large halls. |
| Early / Late | 0–100 % | Balance between early reflections and the tail. 50 % is as sampled. |
| Early Roll-off | 1–20 kHz | Low-pass on the early reflections. |

### Time
| Control | Range | What it does |
| --- | --- | --- |
| Pre-delay (+ Sync) | 0–500 ms or note values | Delays the tail and VLF but not the early reflections, so the source stays close while the room opens up. |
| Delay Time / Delay Level (+ Sync) | 0–1000 ms, 0–100 % | A delayed copy of the input fed into the reverb, for slap-back into the room. |

### Tone and movement
| Control | Range | What it does |
| --- | --- | --- |
| Late Roll-off | 1–20 kHz | Low-pass on the tail. |
| Modulation | 0–100 % (default 5 %) | Gentle pitch movement in the tail. It softens metallic ringing on sustained sounds. |
| Tail Motion | 0–100 % (default 5 %) | Slowly evolves the tail by blending decorrelated versions of it. 0 % is the static capture. |

### EQ (on the wet signal)
Low band (Shelf or Bell), a Mid bell, a High band (Shelf or Bell) and a High
Cut. Each band has Freq, Gain (±18 dB) and Q. **EQ On** bypasses the section.

### Output
| Control | Range | What it does |
| --- | --- | --- |
| Mix | 0–100 % | Dry/wet. Use 100 % on an aux/send. |
| Gain | −24 to +12 dB | Output level. |

## Tips
- For the original sound of a program, load its factory preset and leave the
  shape controls alone.
- Shortening a long hall with Decay Time usually sounds more natural than
  choosing a smaller program, because it keeps the room's character.
- Septima has no added latency. Long IRs use more CPU than short ones; a
  typical hall uses about 10 % of one core.
- In offline bounces Septima waits for the IR to finish loading before it
  starts, so a bounce never begins dry.
