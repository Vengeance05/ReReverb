// Background-thread, frequency-domain convolver for the long part of Septima's
// late and VLF impulse responses.
//
// The first `offset()` samples of each IR are convolved on the audio thread
// (low-latency JUCE convolvers); everything after that is handled here with
// large partitions on a worker thread, so the audio thread does a small, even
// amount of work every block and the expensive part runs in parallel.
//
// Layout: 2 inputs (feed L/R) x 4 outputs (late L/R, VLF L/R) = 8 paths, all
// sharing one input spectrum per partition. The tail IR begins `2P` samples in,
// which gives the worker one full partition period (~85 ms) to deliver each
// block, with zero added latency.
#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <complex>
#include <memory>
#include <vector>

namespace septima {

struct TailSpectra {
    static constexpr int kInputs = 2, kOutputs = 4, kPaths = kInputs * kOutputs;
    int partitionSize = 0;                                    // P
    std::array<std::vector<std::vector<std::complex<float>>>, kPaths> h;   // [path][partition][bin]
    std::array<int, kPaths> maxBin{};                         // bins above this are negligible
    int numPartitions() const noexcept {
        std::size_t k = 0;
        for (const auto& p : h) k = std::max(k, p.size());
        return static_cast<int>(k);
    }
    bool empty() const noexcept { return numPartitions() == 0; }

    // Optional decorrelated versions of the late paths (same time/frequency
    // envelope, independent fine structure). Blending slowly between them makes
    // the tail evolve over time instead of repeating one frozen capture.
    std::array<std::unique_ptr<TailSpectra>, 2> variants;

    // irs[input * kOutputs + output]: full impulse responses; the tail starts at
    // sample `offset`. Runs off the audio thread.
    // `end` (exclusive) limits the section; <= 0 means to the end of the IR.
    static std::unique_ptr<TailSpectra> build(const std::array<const std::vector<float>*, kPaths>& irs,
                                              int offset, int partitionSize, int end = 0);
};

class TailConvolver {
public:
    static constexpr int kInputs = TailSpectra::kInputs, kOutputs = TailSpectra::kOutputs;

    explicit TailConvolver(const char* threadName = "Septima tail");
    ~TailConvolver();

    static int partitionSizeFor(double sampleRate);
    int partitionSize() const noexcept { return P_; }
    int offset() const noexcept { return 2 * P_; }

    // Message/setup thread. Stops the worker, (re)allocates, restarts it.
    void prepare(int partitionSize, double sampleRate = 48000.0);
    void release();
    // Audio thread: clears all history (only call while stopped or from prepare/reset paths).
    void reset() noexcept;

    // Audio thread: hand over new spectra (ownership transfers). Takes effect at
    // the next partition boundary with a one-partition crossfade.
    void setSpectra(TailSpectra* next) noexcept;

    // Audio thread: tail motion amount 0..1 (0 = the captured tail, exactly).
    void setMotion(float amount) noexcept { motionTarget_ = std::clamp(amount, 0.0f, 1.0f); }

    // Audio thread: consumes `n` samples of the two inputs and ADDS the tail
    // contribution to the four outputs.
    void process(const float* inL, const float* inR, float* const* outputs, int n) noexcept;

private:
    struct Job;
    class Worker;
    void runJob(Job& job);

    int P_ = 0;
    juce::String threadName_;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<Worker> worker_;

    // Audio-thread state
    std::array<std::vector<float>, kInputs> history_;        // last 2P input samples per input
    int fill_ = 0;                                            // samples into the current partition
    long long block_ = 0;                                     // index of the partition being filled
    std::array<std::unique_ptr<Job>, 2> jobs_;
    Job* playing_ = nullptr;                                  // job whose output is being read
    int readPos_ = 0;
    TailSpectra* current_ = nullptr;
    TailSpectra* incoming_ = nullptr;
    std::array<TailSpectra*, 4> dispose_{};                   // superseded before use; freed by the worker
    bool clearPending_ = false;
    float motionTarget_ = 0.0f, motion_ = 0.0f;
    double sampleRateForMotion_ = 48000.0;
    double motionPhase_ = 0.0, motionWobble_ = 0.0;
    std::array<float, 3> motionWeights_{1.0f, 0.0f, 0.0f};  // base, variant 1, variant 2 at the last boundary

    // Worker-thread state: frequency-domain input history ("FDL").
    std::array<std::vector<std::vector<std::complex<float>>>, kInputs> fdl_;
    int fdlHead_ = 0;
    std::vector<float> scratch_;
    std::array<std::vector<std::complex<float>>, kOutputs> acc_;
};

} // namespace septima
