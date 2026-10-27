#include "grand_hall/partitioned_convolver.h"

#include <algorithm>
#include <stdexcept>

namespace grand_hall {

void PartitionedConvolver::prepare(const std::size_t blockSize, const std::size_t irLength) {
    if (blockSize == 0 || irLength == 0)
        throw std::invalid_argument("Convolver dimensions must be non-zero");
    blockSize_ = blockSize;
    fftSize_ = blockSize * 2;
    partitions_ = (irLength + blockSize - 1) / blockSize;
    fft_ = std::make_unique<Fft>(fftSize_);
    timeBuffer_.assign(fftSize_, 0.0f);
    overlap_.assign(blockSize, 0.0f);
    inputSpectrum_.assign(fftSize_, {});
    outputSpectrum_.assign(fftSize_, {});
    impulseSpectra_.assign(partitions_ * fftSize_, {});
    history_.assign(partitions_ * fftSize_, {});
    pendingInput_.assign(blockSize_, 0.0f);
    pendingOutput_.assign(blockSize_, 0.0f);
    reset();
}

void PartitionedConvolver::setImpulseResponse(const std::span<const float> ir) {
    if (ir.empty() || partitions_ == 0) throw std::invalid_argument("Empty impulse response");
    std::fill(impulseSpectra_.begin(), impulseSpectra_.end(), std::complex<float>{});
    for (std::size_t partition = 0; partition < partitions_; ++partition) {
        std::fill(timeBuffer_.begin(), timeBuffer_.end(), 0.0f);
        const auto begin = partition * blockSize_;
        const auto count = std::min(blockSize_, ir.size() - std::min(begin, ir.size()));
        std::copy_n(ir.begin() + static_cast<std::ptrdiff_t>(begin), count, timeBuffer_.begin());
        fft_->forward(timeBuffer_, {impulseSpectra_.data() + partition * fftSize_, fftSize_});
    }
    reset();
}

void PartitionedConvolver::reset() noexcept {
    std::fill(overlap_.begin(), overlap_.end(), 0.0f);
    std::fill(history_.begin(), history_.end(), std::complex<float>{});
    std::fill(pendingInput_.begin(), pendingInput_.end(), 0.0f);
    std::fill(pendingOutput_.begin(), pendingOutput_.end(), 0.0f);
    pendingCount_ = 0;
    outputPosition_ = blockSize_;
    historyPosition_ = 0;
}

void PartitionedConvolver::process(const std::span<const float> input,
                                   const std::span<float> output) {
    if (input.size() != output.size()) throw std::invalid_argument("Convolver buffer mismatch");
    for (std::size_t sample = 0; sample < input.size(); ++sample) {
        if (outputPosition_ < blockSize_) output[sample] = pendingOutput_[outputPosition_++];
        else output[sample] = 0.0f;
        pendingInput_[pendingCount_++] = input[sample];
        if (pendingCount_ == blockSize_) {
            std::copy(pendingInput_.begin(), pendingInput_.end(), timeBuffer_.begin());
            std::fill(timeBuffer_.begin() + static_cast<std::ptrdiff_t>(blockSize_),
                      timeBuffer_.end(), 0.0f);
            fft_->forward(timeBuffer_, inputSpectrum_);
            auto* history = history_.data() + historyPosition_ * fftSize_;
            std::copy(inputSpectrum_.begin(), inputSpectrum_.end(), history);
            std::fill(outputSpectrum_.begin(), outputSpectrum_.end(), std::complex<float>{});
            for (std::size_t partition = 0; partition < partitions_; ++partition) {
                const auto position = (historyPosition_ + partitions_ - partition) % partitions_;
                const auto* x = history_.data() + position * fftSize_;
                const auto* h = impulseSpectra_.data() + partition * fftSize_;
                for (std::size_t bin = 0; bin < fftSize_; ++bin)
                    outputSpectrum_[bin] += x[bin] * h[bin];
            }
            fft_->inverse(outputSpectrum_, timeBuffer_);
            for (std::size_t i = 0; i < blockSize_; ++i) {
                pendingOutput_[i] = timeBuffer_[i] + overlap_[i];
                overlap_[i] = timeBuffer_[i + blockSize_];
            }
            historyPosition_ = (historyPosition_ + 1) % partitions_;
            pendingCount_ = 0;
            outputPosition_ = 0;
        }
    }
}

} // namespace grand_hall
