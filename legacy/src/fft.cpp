#include "grand_hall/fft.h"

#include <cmath>
#include <stdexcept>

namespace grand_hall {

Fft::Fft(const std::size_t size) : size_(size), bitReversal_(size), scratch_(size) {
    if (size < 2 || (size & (size - 1)) != 0)
        throw std::invalid_argument("FFT size must be a power of two");
    std::size_t bits = 0;
    for (auto n = size; n > 1; n >>= 1) ++bits;
    for (std::size_t i = 0; i < size; ++i) {
        auto value = i;
        std::size_t reversed = 0;
        for (std::size_t bit = 0; bit < bits; ++bit) {
            reversed = (reversed << 1) | (value & 1U);
            value >>= 1;
        }
        bitReversal_[i] = reversed;
    }
}

void Fft::forward(const std::span<const float> input,
                  const std::span<std::complex<float>> output) const {
    if (input.size() != size_ || output.size() != size_)
        throw std::invalid_argument("FFT buffer size mismatch");
    for (std::size_t i = 0; i < size_; ++i)
        output[bitReversal_[i]] = {input[i], 0.0f};

    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t length = 2; length <= size_; length <<= 1) {
        const auto angle = -2.0f * pi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size_; start += length) {
            std::complex<float> twiddle(1.0f, 0.0f);
            const auto half = length / 2;
            for (std::size_t i = 0; i < half; ++i) {
                const auto even = output[start + i];
                const auto odd = output[start + i + half] * twiddle;
                output[start + i] = even + odd;
                output[start + i + half] = even - odd;
                twiddle *= step;
            }
        }
    }
}

void Fft::inverse(const std::span<const std::complex<float>> input,
                  const std::span<float> output) const {
    if (input.size() != size_ || output.size() != size_)
        throw std::invalid_argument("FFT buffer size mismatch");
    for (std::size_t i = 0; i < size_; ++i)
        scratch_[bitReversal_[i]] = std::conj(input[i]);
    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t length = 2; length <= size_; length <<= 1) {
        const auto angle = -2.0f * pi / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size_; start += length) {
            std::complex<float> twiddle(1.0f, 0.0f);
            const auto half = length / 2;
            for (std::size_t i = 0; i < half; ++i) {
                const auto even = scratch_[start + i];
                const auto odd = scratch_[start + i + half] * twiddle;
                scratch_[start + i] = even + odd;
                scratch_[start + i + half] = even - odd;
                twiddle *= step;
            }
        }
    }
    const auto scale = 1.0f / static_cast<float>(size_);
    for (std::size_t i = 0; i < size_; ++i)
        output[i] = scratch_[i].real() * scale;
}

} // namespace grand_hall
