# Grand Hall listening validation

This is a repeatable listening checklist, not a claim of perceptual equivalence
to any other product. Use the same dry source, level-match the wet return, and
compare neutral decay against shorter and longer settings without changing the
source performance.

## Material

- Solo piano: sustained notes, pedal resonance, and repeated attacks.
- Exposed strings: solo violin and a quiet string section.
- Woodwind and brass: exposed entrances followed by rests.
- Full orchestral tutti: dense low-mid content and simultaneous transients.
- Quiet-to-loud transitions and long pauses between phrases.

## Checks

1. Confirm the initial attack remains clear with the default wet/dry balance.
2. Increase pre-delay and verify that depth increases without a detached echo.
3. Move low and high decay independently. Listen for warmth, brightness, and
   tail duration rather than only a level change.
4. Switch between two legitimate user WAVs during sustained audio. Listen for
   clicks, abrupt stereo collapse, or a discontinuity in the tail.
5. Repeat at 44.1, 48, and 96 kHz and with small and large host blocks.
6. Check that quiet tails do not become metallic, unstable, or excessively
   wide at high width settings.

Record sample rate, block size, IR metadata, parameter values, and observations
for any regression. A controlled blind comparison is required before making
claims about similarity to a reference product.

## Algorithmic mode

Repeat the material above with **Reverb Mode = Algorithmic**. Compare neutral,
shorter, and longer decay values, then listen for metallic periodicity, pitch
wobble, high-frequency harshness, and low-frequency build-up. Toggle early
reflections and vary room size separately from decay. Mode changes should
crossfade without an attack discontinuity. The algorithmic tail is an original
FDN approximation, not a captured venue response.

## EQ comparison

Level-match the complete output, then compare EQ bypass/neutral settings
against one boosted and one cut band. Use the same settings in both reverb
modes and listen for attack coloration, piano sustain brightness, string
bow-noise emphasis, brass harshness, and stereo image changes. The current EQ
is intentionally stereo-linked and affects the complete output, including
dry signal; record that fact in any listening notes.

Record the actual host, sample rate, block size, preset, parameter values, and
observations. No host listening observations are claimed by this repository
until the plugin has been opened and evaluated in a DAW.
