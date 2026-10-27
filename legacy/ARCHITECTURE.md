# Architecture

The DSP core is intentionally independent of a plugin framework.

`Fft` implements a precomputed-radix-2 transform. `PartitionedConvolver`
splits an impulse response into uniform blocks, transforms each block once,
keeps a ring of input spectra, and performs frequency-domain accumulation for
each audio block. `ReverbEngine` owns stereo convolver instances and applies
pre-delay, width, wet/dry, and gain processing.

FFT scratch storage and overlap-add buffers are allocated during preparation,
not in `process`. The JUCE wrapper in `plugin/` maps the current controls to an
`AudioProcessorValueTreeState` and provides VST3/AU targets. It uses a
generated response for a self-contained default build. Replacing that response
with user material still belongs in a background preparation and crossfade
layer before shipping a library-oriented product.

The current WAV reader accepts PCM16 mono or stereo files and maps mono to both
channels. It now accepts PCM 8/16/24/32-bit and IEEE float32, validates a
bounded file size and duration, and uses a windowed-sinc resampler. The
prepared-response layer shapes the tail after a 50 ms early window with
frequency-dependent exponential envelopes. A multiplier of 1.0 is neutral;
values above or below 1.0 lengthen or shorten the measured tail
approximately. This is an offline acoustic model, not recovery of missing
recorded reverberation.

Prepared stereo responses are also decomposed into complementary early and
late components using a smooth 40-80 ms cosine crossover before convolution.
At neutral component levels the two components sum to the shaped response.
Early/late controls therefore have a defined effect in convolution mode, but
the split remains an approximation because an ordinary stereo IR does not
provide independently captured early-reflection and late-reverb signals.

The plugin worker creates complete response engines away from the audio thread.
The audio thread atomically accepts a completed pointer, crossfades for 100 ms,
and never deletes response objects. The worker retains up to eight response
objects and only reclaims an object that is neither active nor pending. Failed
loads are logged and leave the previous response running.

The algorithmic mode is an independent eight-line FDN. Its feedback signs are
the 8x8 Walsh-Hadamard matrix normalized by `1/sqrt(8)`, so the matrix itself
does not amplify energy. Prime-like fixed delay lengths reduce coincident
modes. Feedback is derived from mean delay and requested seconds using a 60 dB
reference; low/high multipliers and a one-pole high-frequency damper shape the
network response. Twelve deterministic early taps precede the network. This
is a diffuse approximation, not a measured RT60 or physical room simulation.

Metadata, library indexing, preset compatibility, five-band EQ, and early/late
capture decomposition remain future work. The editor currently combines a WAV
file chooser with custom rotary controls, a mode selector, factory preset
browser, and user preset save/open actions. The five-band output EQ uses a
low shelf, three peaking bands, and a high shelf with stereo-linked states.
Coefficients move toward parameter changes over 20 ms and are prepared from
bounded frequency, gain, and Q ranges.

The plugin processes a preallocated copy through the algorithmic engine and a
preallocated copy through the selected convolution engine, then smooths the
Convolution/Algorithmic mode blend over 100 ms. This costs both paths while
the mode is being evaluated and is intentionally conservative for click-free
switching. The algorithmic controls are room size, decay, pre-delay, early
level, late level, early/late balance, low/high decay, high damping, width,
wet/dry, and output gain.
