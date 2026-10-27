#include "grand_hall/fft.h"
#include "grand_hall/partitioned_convolver.h"
#include "grand_hall/reverb_engine.h"
#include "grand_hall/ir_processing.h"
#include "grand_hall/wav_reader.h"
#include "grand_hall/algorithmic_reverb.h"
#include "grand_hall/five_band_eq.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    void writePcm16Wav(const char* path, int channels, int sampleRate) {
        const std::vector<std::int16_t> samples = channels == 1
            ? std::vector<std::int16_t>{0, 16384, -8192}
            : std::vector<std::int16_t>{0, 0, 16384, -16384, -8192, 8192};
        const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
        std::ofstream file(path, std::ios::binary);
        const auto u16 = [&](std::uint16_t value) { file.put(static_cast<char>(value)); file.put(static_cast<char>(value >> 8)); };
        const auto u32 = [&](std::uint32_t value) {
            for (int i = 0; i < 4; ++i) file.put(static_cast<char>(value >> (i * 8)));
        };
        file.write("RIFF", 4); u32(36 + dataBytes); file.write("WAVE", 4);
        file.write("fmt ", 4); u32(16); u16(1); u16(static_cast<std::uint16_t>(channels));
        u32(static_cast<std::uint32_t>(sampleRate)); u32(sampleRate * channels * 2); u16(channels * 2); u16(16);
        file.write("data", 4); u32(dataBytes);
        for (const auto sample : samples) u16(static_cast<std::uint16_t>(sample));
    }

    void writeWav(const char* path, std::uint16_t format, std::uint16_t bits,
                  std::uint16_t channels, const std::vector<std::uint8_t>& data,
                  bool oddJunk = false, bool includeFormat = true,
                  bool includeData = true, std::uint32_t declaredDataSize = 0) {
        std::vector<std::uint8_t> body;
        const auto appendChunk = [&body](const char id[4],
                                         const std::vector<std::uint8_t>& payload) {
            body.insert(body.end(), id, id + 4);
            for (int byte = 0; byte < 4; ++byte)
                body.push_back(static_cast<std::uint8_t>(payload.size() >> (byte * 8)));
            body.insert(body.end(), payload.begin(), payload.end());
            if ((payload.size() & 1U) != 0) body.push_back(0);
        };
        if (includeFormat) {
            std::vector<std::uint8_t> formatChunk;
            const auto add16 = [&formatChunk](const std::uint16_t value) {
                formatChunk.push_back(static_cast<std::uint8_t>(value));
                formatChunk.push_back(static_cast<std::uint8_t>(value >> 8));
            };
            const auto add32 = [&formatChunk](const std::uint32_t value) {
                for (int byte = 0; byte < 4; ++byte)
                    formatChunk.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
            };
            add16(format);
            add16(channels);
            add32(24000);
            add32(24000 * channels * ((bits + 7) / 8));
            add16(static_cast<std::uint16_t>(channels * ((bits + 7) / 8)));
            add16(bits);
            appendChunk("fmt ", formatChunk);
        }
        if (oddJunk) appendChunk("JUNK", {1, 2, 3});
        if (includeData) {
            body.insert(body.end(), {'d', 'a', 't', 'a'});
            const auto size = declaredDataSize == 0 ? data.size() : declaredDataSize;
            for (int byte = 0; byte < 4; ++byte)
                body.push_back(static_cast<std::uint8_t>(size >> (byte * 8)));
            body.insert(body.end(), data.begin(), data.end());
            if ((data.size() & 1U) != 0) body.push_back(0);
        }
        std::ofstream file(path, std::ios::binary);
        file.write("RIFF", 4);
        const auto riffSize = static_cast<std::uint32_t>(body.size() + 4);
        for (int byte = 0; byte < 4; ++byte)
            file.put(static_cast<char>(riffSize >> (byte * 8)));
        file.write("WAVE", 4);
        file.write(reinterpret_cast<const char*>(body.data()),
                   static_cast<std::streamsize>(body.size()));
    }

    void expectWavFailure(const char* path, const char* message) {
        bool failed = false;
        try {
            (void)grand_hall::readWavImpulseResponse(path);
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed, message);
    }
    }

    int main() {
    grand_hall::PartitionedConvolver convolver;
    convolver.prepare(4, 5);
    const std::vector<float> ir{1.0f, 0.5f, -0.25f, 0.125f, 0.0625f};
    convolver.setImpulseResponse(ir);
    std::vector<float> input(32, 0.0f), output(32, 0.0f);
    input[0] = 1.0f;
    for (std::size_t offset = 0; offset < input.size(); offset += 4)
        convolver.process({input.data() + offset, 4}, {output.data() + offset, 4});
    require(std::abs(output[4] - 1.0f) < 1.0e-4f, "impulse alignment");
    require(std::abs(output[5] - 0.5f) < 1.0e-4f, "first tail sample");
    require(std::abs(output[6] + 0.25f) < 1.0e-4f, "second tail sample");
    require(std::abs(output[7] - 0.125f) < 1.0e-4f, "third tail sample");
    require(std::abs(output[8] - 0.0625f) < 1.0e-4f, "partition boundary");
    grand_hall::PartitionedConvolver signalConvolver;
    signalConvolver.prepare(4, ir.size());
    signalConvolver.setImpulseResponse(ir);
    std::vector<float> signalInput(32), signalOutput(32), expected(32);
    for (std::size_t i = 0; i < signalInput.size(); ++i)
        signalInput[i] = std::sin(static_cast<float>(i) * 0.37f) * 0.4f;
    for (std::size_t i = 0; i < expected.size(); ++i)
        for (std::size_t k = 0; k < ir.size() && k <= i; ++k)
            expected[i] += signalInput[i - k] * ir[k];
    for (std::size_t offset = 0; offset < signalInput.size(); offset += 4)
        signalConvolver.process({signalInput.data() + offset, 4},
                                {signalOutput.data() + offset, 4});
    for (std::size_t i = 0; i + 4 < signalOutput.size(); ++i)
        require(std::abs(signalOutput[i + 4] - expected[i]) < 2.0e-4f,
                "reference convolution");
    const std::vector<float> source{0.0f, 0.25f, 1.0f, 0.5f, 0.0f};
    const auto processed = grand_hall::processImpulseResponse(source);
    require(processed.size() == 3 && processed.front() == 0.25f, "IR trimming");
    const auto resampled = grand_hall::resampleLinear(source, 2.0, 4.0);
    require(resampled.size() == 10 && std::abs(resampled[2] - 0.25f) < 0.03f,
            "windowed-sinc resampling");
    writePcm16Wav("/tmp/grand_hall_mono.wav", 1, 24000);
    const auto mono = grand_hall::readWavImpulseResponse("/tmp/grand_hall_mono.wav");
    require(mono.channels == 1 && mono.left == mono.right, "mono WAV mapping");
    writePcm16Wav("/tmp/grand_hall_stereo.wav", 2, 24000);
    const auto stereo = grand_hall::readWavImpulseResponse("/tmp/grand_hall_stereo.wav");
    require(stereo.channels == 2 && stereo.left[1] != stereo.right[1], "stereo WAV preservation");
    std::remove("/tmp/grand_hall_mono.wav");
    std::remove("/tmp/grand_hall_stereo.wav");
    {
        const std::vector<float> response{
            1.0f, 0.8f, 0.6f, 0.4f, 0.25f, 0.2f, 0.15f, 0.1f,
            0.08f, 0.06f, 0.04f, 0.03f, 0.02f, 0.01f, 0.0f, 0.0f};
        const auto components = grand_hall::splitImpulseResponse(response, 100.0);
        require(components.early.size() == response.size()
                && components.late.size() == response.size(),
                "early/late component sizes");
        for (std::size_t i = 0; i < response.size(); ++i)
            require(std::abs(components.early[i] + components.late[i] - response[i])
                    < 1.0e-6f, "neutral early/late reconstruction");
        require(std::abs(components.early[0]) > std::abs(components.late[0]),
                "early component contains onset");
        require(std::abs(components.late[12]) > std::abs(components.early[12]),
                "late component contains tail");

        grand_hall::ReverbEngine engine;
        engine.prepare(100.0, 4, response.size());
        engine.setImpulseResponseComponents(components.early, components.late,
                                            components.early, components.late);
        grand_hall::ReverbParameters parameters;
        parameters.preDelayMs = 0.0f;
        parameters.wet = 1.0f;
        parameters.dry = 0.0f;
        parameters.earlyLevel = 1.0f;
        parameters.lateLevel = 1.0f;
        parameters.earlyLateBalance = 0.5f;
        engine.setParameters(parameters);
        std::vector<float> inputLeft(32, 0.0f), inputRight(32, 0.0f);
        inputLeft[0] = inputRight[0] = 1.0f;
        for (std::size_t offset = 0; offset < inputLeft.size(); offset += 4)
            engine.process({inputLeft.data() + offset, 4},
                           {inputRight.data() + offset, 4});
        require(std::abs(inputLeft[4] - response[0]) < 1.0e-4f,
                "neutral early/late engine output");
        parameters.earlyLevel = 0.0f;
        parameters.earlyLateBalance = 0.0f;
        engine.reset();
        engine.setParameters(parameters);
        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        inputLeft[0] = inputRight[0] = 1.0f;
        for (std::size_t offset = 0; offset < inputLeft.size(); offset += 4)
            engine.process({inputLeft.data() + offset, 4},
                           {inputRight.data() + offset, 4});
        require(std::abs(inputLeft[4]) < 1.0e-6f, "early level control");
        parameters.earlyLevel = 0.0f;
        parameters.lateLevel = 1.0f;
        parameters.earlyLateBalance = 1.0f;
        engine.reset();
        engine.setParameters(parameters);
        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        inputLeft[0] = inputRight[0] = 1.0f;
        for (std::size_t offset = 0; offset < inputLeft.size(); offset += 4)
            engine.process({inputLeft.data() + offset, 4},
                           {inputRight.data() + offset, 4});
        require(std::abs(inputLeft[4]) < 1.0e-6f,
                "late level excludes early component");
        require(std::abs(inputLeft[16] - 2.0f * response[12]) < 1.0e-4f,
                "late level controls tail");
        parameters.lateLevel = 0.0f;
        engine.reset();
        engine.setParameters(parameters);
        std::fill(inputLeft.begin(), inputLeft.end(), 0.0f);
        std::fill(inputRight.begin(), inputRight.end(), 0.0f);
        inputLeft[0] = inputRight[0] = 1.0f;
        for (std::size_t offset = 0; offset < inputLeft.size(); offset += 4)
            engine.process({inputLeft.data() + offset, 4},
                           {inputRight.data() + offset, 4});
        require(std::abs(inputLeft[16]) < 1.0e-6f,
                "early-late balance excludes late component");
    }
    for (const auto bits : {8U, 16U, 24U, 32U}) {
        std::vector<std::uint8_t> pcm(bits / 8);
        const auto value = bits == 8 ? 128U : 16384U;
        for (std::size_t byte = 0; byte < pcm.size(); ++byte)
            pcm[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
        const auto path = std::string("/tmp/grand_hall_pcm")
                        + std::to_string(bits) + ".wav";
        writeWav(path.c_str(), 1, static_cast<std::uint16_t>(bits), 1, pcm);
        const auto decoded = grand_hall::readWavImpulseResponse(path);
        require(decoded.format == 1 && decoded.bitsPerSample == bits
                && decoded.left.size() == 1 && std::isfinite(decoded.left[0]),
                "supported PCM WAV format");
        std::remove(path.c_str());
    }
    {
        const float sample = 0.25f;
        std::uint32_t raw = 0;
        std::memcpy(&raw, &sample, sizeof(raw));
        std::vector<std::uint8_t> data(4);
        for (int byte = 0; byte < 4; ++byte)
            data[static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>(raw >> (byte * 8));
        writeWav("/tmp/grand_hall_float.wav", 3, 32, 1, data);
        const auto decoded = grand_hall::readWavImpulseResponse("/tmp/grand_hall_float.wav");
        require(decoded.format == 3 && decoded.bitsPerSample == 32
                && std::abs(decoded.left[0] - sample) < 1.0e-6f,
                "IEEE float32 WAV");
        std::remove("/tmp/grand_hall_float.wav");
    }
    writeWav("/tmp/grand_hall_odd_chunk.wav", 1, 16, 1, {0, 64}, true);
    const auto oddChunk = grand_hall::readWavImpulseResponse("/tmp/grand_hall_odd_chunk.wav");
    require(oddChunk.left.size() == 1, "odd-sized WAV chunk padding");
    std::remove("/tmp/grand_hall_odd_chunk.wav");
    writeWav("/tmp/grand_hall_float16.wav", 3, 16, 1, {0, 0});
    expectWavFailure("/tmp/grand_hall_float16.wav", "reject float16 WAV");
    writeWav("/tmp/grand_hall_format6.wav", 6, 16, 1, {0, 0});
    expectWavFailure("/tmp/grand_hall_format6.wav", "reject unsupported WAV format");
    writeWav("/tmp/grand_hall_channels3.wav", 1, 16, 3, {0, 0, 0, 0, 0, 0});
    expectWavFailure("/tmp/grand_hall_channels3.wav", "reject invalid channel count");
    writeWav("/tmp/grand_hall_unaligned.wav", 1, 16, 1, {0});
    expectWavFailure("/tmp/grand_hall_unaligned.wav", "reject unaligned WAV data");
    writeWav("/tmp/grand_hall_missing_fmt.wav", 1, 16, 1, {0, 0}, false, false, true);
    expectWavFailure("/tmp/grand_hall_missing_fmt.wav", "reject missing fmt chunk");
    writeWav("/tmp/grand_hall_missing_data.wav", 1, 16, 1, {}, false, true, false);
    expectWavFailure("/tmp/grand_hall_missing_data.wav", "reject missing data chunk");
    writeWav("/tmp/grand_hall_truncated.wav", 1, 16, 1, {0}, false, true, true, 3);
    expectWavFailure("/tmp/grand_hall_truncated.wav", "reject truncated WAV data");
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::uint32_t raw = 0;
        std::memcpy(&raw, &nan, sizeof(raw));
        std::vector<std::uint8_t> data(4);
        for (int byte = 0; byte < 4; ++byte)
            data[static_cast<std::size_t>(byte)] = static_cast<std::uint8_t>(raw >> (byte * 8));
        writeWav("/tmp/grand_hall_nan.wav", 3, 32, 1, data);
        expectWavFailure("/tmp/grand_hall_nan.wav", "reject non-finite float sample");
    }
    for (const auto* path : {"/tmp/grand_hall_float16.wav", "/tmp/grand_hall_format6.wav",
                             "/tmp/grand_hall_channels3.wav", "/tmp/grand_hall_unaligned.wav",
                             "/tmp/grand_hall_missing_fmt.wav", "/tmp/grand_hall_missing_data.wav",
                             "/tmp/grand_hall_truncated.wav", "/tmp/grand_hall_nan.wav"})
        std::remove(path);
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0}) {
        for (const auto blockSize : {32U, 64U, 128U, 256U, 512U}) {
        grand_hall::AlgorithmicReverb algorithmic;
        algorithmic.prepare(sampleRate, blockSize);
        grand_hall::AlgorithmicParameters parameters;
        parameters.decaySeconds = 1.5f;
        parameters.wet = 1.0f;
        parameters.dry = 0.0f;
        algorithmic.setParameters(parameters);
        std::vector<float> algorithmicLeft(blockSize, 0.0f), algorithmicRight(blockSize, 0.0f);
        algorithmicLeft[0] = 1.0f;
        algorithmicRight[0] = 1.0f;
        double energy = 0.0;
        for (int block = 0; block < 300; ++block) {
            algorithmic.process(algorithmicLeft, algorithmicRight);
            for (std::size_t i = 0; i < algorithmicLeft.size(); ++i) {
                require(std::isfinite(algorithmicLeft[i]) && std::isfinite(algorithmicRight[i]),
                        "algorithmic finite output");
                energy += algorithmicLeft[i] * algorithmicLeft[i]
                        + algorithmicRight[i] * algorithmicRight[i];
                algorithmicLeft[i] = 0.0f;
                algorithmicRight[i] = 0.0f;
            }
            grand_hall::FiveBandEq eq;
            eq.prepare(48000.0);
            std::array<grand_hall::EqBand, 5> neutral{};
            neutral[0] = {100.0f, 0.0f, 0.707f};
            neutral[1] = {300.0f, 0.0f, 0.707f};
            neutral[2] = {1000.0f, 0.0f, 0.707f};
            neutral[3] = {4000.0f, 0.0f, 0.707f};
            neutral[4] = {12000.0f, 0.0f, 0.707f};
            eq.setBands(neutral);
            std::vector<float> eqLeft(256), eqRight(256);
            for (std::size_t i = 0; i < eqLeft.size(); ++i) eqLeft[i] = eqRight[i] = std::sin(i * 0.11f);
            const auto before = eqLeft;
            eq.process(eqLeft, eqRight);
            for (std::size_t i = 32; i < eqLeft.size(); ++i)
                require(std::abs(eqLeft[i] - before[i]) < 1.0e-4f, "neutral EQ");
            neutral[2].gainDb = 12.0f;
            eq.setBands(neutral);
            std::fill(eqLeft.begin(), eqLeft.end(), 0.0f);
            std::fill(eqRight.begin(), eqRight.end(), 0.0f);
            eqLeft[0] = eqRight[0] = 1.0f;
            eq.process(eqLeft, eqRight);
            for (const auto value : eqLeft)
                require(std::isfinite(value), "EQ finite output");
        }
        require(energy > 1.0e-6 && energy < 100.0, "algorithmic bounded energy");
        algorithmic.reset();
        algorithmic.setParameters(parameters);
        algorithmicLeft[0] = 1.0f;
        algorithmicRight[0] = 0.0f;
        float maximumDifference = 0.0f;
        for (int block = 0; block < 300; ++block) {
            algorithmic.process(algorithmicLeft, algorithmicRight);
            for (std::size_t i = 0; i < algorithmicLeft.size(); ++i)
                maximumDifference = std::max(maximumDifference,
                    std::abs(algorithmicLeft[i] - algorithmicRight[i]));
            std::fill(algorithmicLeft.begin(), algorithmicLeft.end(), 0.0f);
            std::fill(algorithmicRight.begin(), algorithmicRight.end(), 0.0f);
        }
        require(maximumDifference > 1.0e-7f,
                "algorithmic stereo decorrelation");
        grand_hall::AlgorithmicParameters shortDecay = parameters;
        shortDecay.decaySeconds = 0.4f;
        algorithmic.reset();
        algorithmic.setParameters(shortDecay);
        std::fill(algorithmicLeft.begin(), algorithmicLeft.end(), 0.0f);
        std::fill(algorithmicRight.begin(), algorithmicRight.end(), 0.0f);
        algorithmicLeft[0] = algorithmicRight[0] = 1.0f;
        double shortEnergy = 0.0;
        for (int block = 0; block < 100; ++block) {
            algorithmic.process(algorithmicLeft, algorithmicRight);
            for (std::size_t i = 0; i < algorithmicLeft.size(); ++i) {
                shortEnergy += algorithmicLeft[i] * algorithmicLeft[i]
                             + algorithmicRight[i] * algorithmicRight[i];
                algorithmicLeft[i] = algorithmicRight[i] = 0.0f;
            }
        }
        require(shortEnergy < energy, "shorter algorithmic decay");
        }
    }
    {
        constexpr std::size_t renderedSize = 2048;
        std::vector<float> renderedLeft(renderedSize);
        std::vector<float> renderedRight(renderedSize);
        for (std::size_t i = 0; i < renderedSize; ++i) {
            const auto sample = std::sin(static_cast<float>(i) * 0.17f)
                              + 0.25f * std::sin(static_cast<float>(i) * 0.031f);
            renderedLeft[i] = renderedRight[i] = sample;
        }
        const auto original = renderedLeft;
        std::array<grand_hall::EqBand, 5> neutral{{
            {100.0f, 0.0f, 0.707f},
            {300.0f, 0.0f, 0.707f},
            {1000.0f, 0.0f, 0.707f},
            {4000.0f, 0.0f, 0.707f},
            {12000.0f, 0.0f, 0.707f}}};
        grand_hall::FiveBandEq neutralEq;
        neutralEq.prepare(48000.0);
        neutralEq.setBands(neutral);
        neutralEq.process(renderedLeft, renderedRight);
        for (std::size_t i = 32; i < renderedSize; ++i) {
            require(std::abs(renderedLeft[i] - original[i]) < 1.0e-4f,
                    "neutral final-output EQ");
            require(std::abs(renderedLeft[i] - renderedRight[i]) < 1.0e-6f,
                    "neutral EQ stereo consistency");
        }

        auto boosted = neutral;
        boosted[2].gainDb = 12.0f;
        std::vector<float> boostedLeft = original;
        std::vector<float> boostedRight = original;
        grand_hall::FiveBandEq boostedEq;
        boostedEq.prepare(48000.0);
        boostedEq.setBands(boosted);
        boostedEq.process(boostedLeft, boostedRight);
        bool changed = false;
        for (std::size_t i = 32; i < renderedSize; ++i) {
            require(std::isfinite(boostedLeft[i]) && std::isfinite(boostedRight[i]),
                    "finite final-output EQ");
            require(std::abs(boostedLeft[i] - boostedRight[i]) < 1.0e-6f,
                    "boosted EQ stereo consistency");
            changed = changed || std::abs(boostedLeft[i] - original[i]) > 1.0e-3f;
        }
        require(changed, "non-neutral final-output EQ changes rendered signal");
    }
    std::cout << "grand_hall_tests: all checks passed\n";
}
