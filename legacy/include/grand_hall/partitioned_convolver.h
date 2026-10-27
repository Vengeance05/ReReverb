#pragma once

#include "grand_hall/fft.h"

#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace grand_hall {

class PartitionedConvolver {
public:
    void prepare(std::size_t blockSize, std::size_t irLength);
    void setImpulseResponse(std::span<const float> ir);
    void reset() noexcept;
    void process(std::span<const float> input, std::span<float> output);
    [[nodiscard]] std::size_t latency() const noexcept { return blockSize_; }

private:
    std::size_t blockSize_ = 0;
    std::size_t fftSize_ = 0;
    std::size_t partitions_ = 0;
    std::unique_ptr<Fft> fft_;
    std::vector<float> timeBuffer_;
    std::vector<float> overlap_;
    std::vector<std::complex<float>> inputSpectrum_;
    std::vector<std::complex<float>> outputSpectrum_;
    std::vector<std::complex<float>> impulseSpectra_;
    std::vector<std::complex<float>> history_;
    std::size_t historyPosition_ = 0;
    std::vector<float> pendingInput_;
    std::size_t pendingCount_ = 0;
    std::vector<float> pendingOutput_;
    std::size_t outputPosition_ = 0;
};

} // namespace grand_hall
