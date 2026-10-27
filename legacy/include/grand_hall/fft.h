#pragma once

#include <complex>
#include <span>
#include <vector>

namespace grand_hall {

class Fft {
public:
    explicit Fft(std::size_t size);
    void forward(std::span<const float> input, std::span<std::complex<float>> output) const;
    void inverse(std::span<const std::complex<float>> input, std::span<float> output) const;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::size_t size_;
    std::vector<std::size_t> bitReversal_;
    mutable std::vector<std::complex<float>> scratch_;
};

} // namespace grand_hall
