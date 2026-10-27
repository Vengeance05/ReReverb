// Offline renderer: writes the Vista Rooms impulse response for a preset-like
// set of arguments, or reshapes a WAV IR as Septima would. Used for listening
// and measurement during development.
#include "rr/vista_engine.h"
#include "wav_io.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    const int fs = 48000;
    rr::VistaSettings s;
    s.late.rt60 = argc > 1 ? static_cast<float>(std::atof(argv[1])) : 2.5f;
    s.er.pattern = static_cast<rr::ErPattern>(argc > 2 ? std::atoi(argv[2]) : 0);
    const char* out = argc > 3 ? argv[3] : "vista_ir.wav";
    rr::VistaEngine e;
    e.setSettings(s);
    e.prepare(fs, 512);
    const int n = static_cast<int>(fs * (s.late.rt60 * 1.5f + 1.0f));
    std::vector<float> l(static_cast<std::size_t>(n), 0.0f), r(static_cast<std::size_t>(n), 0.0f);
    l[0] = r[0] = 1.0f;
    for (int i = 0; i < n; i += 512) e.process(&l[static_cast<std::size_t>(i)], &r[static_cast<std::size_t>(i)], std::min(512, n - i));
    rr::tools::writeWav(out, {l, r}, fs);
    std::printf("wrote %s\n", out);
}
