#include "TailConvolver.h"

#include <cmath>
#include <thread>

namespace septima {

namespace {
int log2Exact(int n) {
    int o = 0;
    while ((1 << o) < n) ++o;
    return o;
}
} // namespace

// ---------------------------------------------------------------------------
std::unique_ptr<TailSpectra> TailSpectra::build(const std::array<const std::vector<float>*, kPaths>& irs,
                                                int offset, int P, int end) {
    auto s = std::make_unique<TailSpectra>();
    s->partitionSize = P;
    juce::dsp::FFT fft(log2Exact(2 * P));
    std::vector<float> buf(static_cast<std::size_t>(4 * P));
    for (int path = 0; path < kPaths; ++path) {
        const auto* ir = irs[static_cast<std::size_t>(path)];
        auto& parts = s->h[static_cast<std::size_t>(path)];
        parts.clear();
        if (ir == nullptr || static_cast<int>(ir->size()) <= offset) continue;
        const int irEnd = end > 0 ? std::min(end, static_cast<int>(ir->size())) : static_cast<int>(ir->size());
        if (irEnd <= offset) continue;
        const int len = irEnd - offset;
        // Ignore a silent end (faded captures): find the last meaningful sample.
        float peak = 0.0f;
        for (int i = 0; i < len; ++i) peak = std::max(peak, std::abs((*ir)[static_cast<std::size_t>(offset + i)]));
        if (peak <= 0.0f) continue;
        int last = len - 1;
        while (last > 0 && std::abs((*ir)[static_cast<std::size_t>(offset + last)]) < peak * 1.0e-7f) --last;
        const int K = last / P + 1;
        parts.resize(static_cast<std::size_t>(K));
        double binMaxMag = 0.0;
        std::vector<double> binPeak(static_cast<std::size_t>(P + 1), 0.0);
        for (int k = 0; k < K; ++k) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            const int start = k * P;
            const int count = std::min(P, len - start);
            for (int i = 0; i < count; ++i) buf[static_cast<std::size_t>(i)] = (*ir)[static_cast<std::size_t>(offset + start + i)];
            fft.performRealOnlyForwardTransform(buf.data(), true);
            auto& part = parts[static_cast<std::size_t>(k)];
            part.resize(static_cast<std::size_t>(P + 1));
            for (int b = 0; b <= P; ++b) {
                part[static_cast<std::size_t>(b)] = {buf[static_cast<std::size_t>(2 * b)], buf[static_cast<std::size_t>(2 * b + 1)]};
                const double m = std::abs(part[static_cast<std::size_t>(b)]);
                binPeak[static_cast<std::size_t>(b)] = std::max(binPeak[static_cast<std::size_t>(b)], m);
                binMaxMag = std::max(binMaxMag, m);
            }
        }
        // Bins whose response never comes within 140 dB of the strongest are
        // skipped (the VLF paths only carry the lowest few hundred bins).
        int maxBin = 0;
        for (int b = P; b >= 0; --b)
            if (binPeak[static_cast<std::size_t>(b)] > binMaxMag * 1.0e-7) { maxBin = b + 1; break; }
        s->maxBin[static_cast<std::size_t>(path)] = std::min(P + 1, (maxBin + 7) & ~7);
    }
    return s;
}

// ---------------------------------------------------------------------------
struct TailConvolver::Job {
    std::atomic<int> state{0};   // 0 free, 1 queued, 2 done
    long long index = -1;
    std::array<std::vector<float>, kInputs> frame;
    TailSpectra* cur = nullptr;
    TailSpectra* old = nullptr;  // being crossfaded out; freed by the worker
    std::array<TailSpectra*, 4> dispose{};
    bool clearHistory = false;
    std::array<float, 3> motionFrom{1.0f, 0.0f, 0.0f}, motionTo{1.0f, 0.0f, 0.0f};
    std::array<std::vector<float>, kOutputs> out;
};

class TailConvolver::Worker : public juce::Thread {
public:
    explicit Worker(TailConvolver& owner) : juce::Thread(owner.threadName_), owner_(owner) {}
    void run() override {
        while (!threadShouldExit()) {
            Job* next = nullptr;
            for (auto& j : owner_.jobs_)
                if (j->state.load(std::memory_order_acquire) == 1 && (next == nullptr || j->index < next->index))
                    next = j.get();
            if (next == nullptr) { wake_.wait(50); continue; }
            owner_.runJob(*next);
            next->state.store(2, std::memory_order_release);
        }
    }
    void wake() noexcept { wake_.signal(); }
private:
    TailConvolver& owner_;
    juce::WaitableEvent wake_;
};

TailConvolver::TailConvolver(const char* threadName) : threadName_(threadName) {}

TailConvolver::~TailConvolver() { release(); }

int TailConvolver::partitionSizeFor(double sampleRate) {
    // ~85 ms of worker slack at any rate.
    int p = 1024;
    while (p < 0.085 * sampleRate) p <<= 1;
    return std::min(p, 16384);
}

void TailConvolver::prepare(int partitionSize, double sampleRate) {
    sampleRateForMotion_ = sampleRate;
    // Keep the spectra we already have across a re-prepare with the same size.
    TailSpectra* keep = incoming_ != nullptr ? incoming_ : current_;
    if (keep == current_) current_ = nullptr; else incoming_ = nullptr;
    release();
    P_ = partitionSize;
    fft_ = std::make_unique<juce::dsp::FFT>(log2Exact(2 * P_));
    for (auto& h : history_) h.assign(static_cast<std::size_t>(2 * P_), 0.0f);
    for (auto& j : jobs_) {
        j = std::make_unique<Job>();
        for (auto& f : j->frame) f.assign(static_cast<std::size_t>(2 * P_), 0.0f);
        for (auto& o : j->out) o.assign(static_cast<std::size_t>(P_), 0.0f);
    }
    scratch_.assign(static_cast<std::size_t>(4 * P_), 0.0f);
    for (auto& a : acc_) a.assign(static_cast<std::size_t>(P_ + 1), {});
    for (auto& f : fdl_) f.clear();
    fdlHead_ = 0;
    fill_ = 0;
    block_ = 0;
    playing_ = nullptr;
    readPos_ = 0;
    if (keep != nullptr && keep->partitionSize == P_) incoming_ = keep;
    else delete keep;
    worker_ = std::make_unique<Worker>(*this);
    worker_->startThread(juce::Thread::Priority::highest);
}

void TailConvolver::release() {
    if (worker_ != nullptr) {
        worker_->signalThreadShouldExit();
        worker_->wake();
        worker_->stopThread(2000);
        worker_.reset();
    }
    for (auto& j : jobs_) {
        if (j == nullptr) continue;
        delete j->old; j->old = nullptr;
        for (auto*& d : j->dispose) { delete d; d = nullptr; }
    }
    for (auto*& d : dispose_) { delete d; d = nullptr; }
    delete current_; current_ = nullptr;
    delete incoming_; incoming_ = nullptr;
}

void TailConvolver::reset() noexcept {
    if (P_ == 0) return;
    // Let in-flight work finish, then drop all history (audio and spectral).
    for (auto& j : jobs_)
        while (j->state.load(std::memory_order_acquire) == 1) std::this_thread::yield();
    for (auto& j : jobs_) j->state.store(0);
    for (auto& h : history_) std::fill(h.begin(), h.end(), 0.0f);
    fill_ = 0;
    playing_ = nullptr;
    readPos_ = 0;
    clearPending_ = true;
}

void TailConvolver::setSpectra(TailSpectra* next) noexcept {
    if (incoming_ != nullptr) {
        bool parked = false;
        for (auto*& d : dispose_)
            if (d == nullptr) { d = incoming_; parked = true; break; }
        if (!parked) delete incoming_;   // practically unreachable (4 swaps within one partition)
    }
    incoming_ = next;
}

void TailConvolver::process(const float* inL, const float* inR, float* const* outputs, int n) noexcept {
    if (P_ == 0) return;
    int pos = 0;
    while (pos < n) {
        const int chunk = std::min(n - pos, P_ - fill_);
        std::copy(inL + pos, inL + pos + chunk, history_[0].begin() + P_ + fill_);
        std::copy(inR + pos, inR + pos + chunk, history_[1].begin() + P_ + fill_);
        if (playing_ != nullptr)
            for (int o = 0; o < kOutputs; ++o) {
                const float* src = playing_->out[static_cast<std::size_t>(o)].data() + readPos_;
                float* dst = outputs[o] + pos;
                for (int i = 0; i < chunk; ++i) dst[i] += src[i];
            }
        fill_ += chunk;
        readPos_ += chunk;
        pos += chunk;
        if (fill_ < P_) continue;

        // Partition boundary: the slot that was playing (job m-2) becomes free
        // and takes job m; job m-1 becomes the output for the next period.
        Job& job = *jobs_[static_cast<std::size_t>(block_ & 1)];
        while (job.state.load(std::memory_order_acquire) == 1) std::this_thread::yield();
        job.index = block_;
        for (int i = 0; i < kInputs; ++i) std::copy(history_[static_cast<std::size_t>(i)].begin(),
                                                    history_[static_cast<std::size_t>(i)].end(),
                                                    job.frame[static_cast<std::size_t>(i)].begin());
        job.old = nullptr;
        if (incoming_ != nullptr) {
            job.old = current_;
            current_ = incoming_;
            incoming_ = nullptr;
        }
        job.cur = current_;
        job.dispose = dispose_;
        dispose_ = {};
        job.clearHistory = clearPending_;
        clearPending_ = false;
        {
            // Motion: equal-power blend of the captured tail and two decorrelated
            // variants, rotating slowly (~12 s cycle with a slower wobble).
            const double dt = static_cast<double>(P_) / sampleRateForMotion_;
            motion_ += (motionTarget_ - motion_) * 0.25f;
            if (std::abs(motion_ - motionTarget_) < 1.0e-4f) motion_ = motionTarget_;
            motionPhase_ += 2.0 * 3.141592653589793 * dt * (0.083 + 0.03 * std::sin(motionWobble_));
            motionWobble_ += 2.0 * 3.141592653589793 * dt * 0.037;
            const float a = motion_;
            const std::array<float, 3> w{std::sqrt(1.0f - a),
                                         std::sqrt(a) * static_cast<float>(std::cos(motionPhase_)),
                                         std::sqrt(a) * static_cast<float>(std::sin(motionPhase_))};
            job.motionFrom = motionWeights_;
            job.motionTo = w;
            motionWeights_ = w;
        }
        job.state.store(1, std::memory_order_release);
        worker_->wake();

        for (auto& h : history_) std::copy(h.begin() + P_, h.end(), h.begin());
        fill_ = 0;
        readPos_ = 0;
        playing_ = nullptr;
        if (block_ >= 1) {
            Job& prev = *jobs_[static_cast<std::size_t>((block_ - 1) & 1)];
            if (prev.index == block_ - 1) {
                while (prev.state.load(std::memory_order_acquire) != 2) std::this_thread::yield();
                playing_ = &prev;
            }
        }
        ++block_;
    }
}

void TailConvolver::runJob(Job& job) {
    const int P = P_;
    const int bins = P + 1;
    auto* cur = job.cur;
    auto* old = job.old;
    const int needed = std::max({1, cur != nullptr ? cur->numPartitions() : 0, old != nullptr ? old->numPartitions() : 0});
    if (job.clearHistory)
        for (auto& f : fdl_) for (auto& s : f) std::fill(s.begin(), s.end(), std::complex<float>{});
    // Grow the spectral history if a longer IR arrived (worker thread may allocate).
    // Existing spectra keep their age: age a (1 = previous partition) moves to
    // index needed - a, and writing restarts at index 0.
    if (static_cast<int>(fdl_[0].size()) < needed) {
        for (auto& f : fdl_) {
            std::vector<std::vector<std::complex<float>>> grown(static_cast<std::size_t>(needed),
                                                                std::vector<std::complex<float>>(static_cast<std::size_t>(bins)));
            const int oldSize = static_cast<int>(f.size());
            for (int age = 1; age <= oldSize; ++age)
                grown[static_cast<std::size_t>(needed - age)] = std::move(f[static_cast<std::size_t>((fdlHead_ - age + oldSize) % oldSize)]);
            f = std::move(grown);
        }
        fdlHead_ = 0;
    }
    const int cap = static_cast<int>(fdl_[0].size());

    // Input spectra for this partition.
    for (int i = 0; i < kInputs; ++i) {
        std::fill(scratch_.begin(), scratch_.end(), 0.0f);
        std::copy(job.frame[static_cast<std::size_t>(i)].begin(), job.frame[static_cast<std::size_t>(i)].end(), scratch_.begin());
        fft_->performRealOnlyForwardTransform(scratch_.data(), true);
        auto& slot = fdl_[static_cast<std::size_t>(i)][static_cast<std::size_t>(fdlHead_)];
        for (int b = 0; b < bins; ++b)
            slot[static_cast<std::size_t>(b)] = {scratch_[static_cast<std::size_t>(2 * b)], scratch_[static_cast<std::size_t>(2 * b + 1)]};
    }

    // Convolve one output of one spectra set and write/add it with a gain ramp.
    auto renderOne = [&](const TailSpectra* s, int o, float g0, float g1, std::vector<float>& dst, bool add) {
        if (g0 == 0.0f && g1 == 0.0f) {
            if (!add) std::fill(dst.begin(), dst.end(), 0.0f);
            return;
        }
        auto& acc = acc_[static_cast<std::size_t>(o)];
        std::fill(acc.begin(), acc.end(), std::complex<float>{});
        int top = 0;
        if (s != nullptr) {
            for (int i = 0; i < kInputs; ++i) {
                const int path = i * kOutputs + o;
                const auto& parts = s->h[static_cast<std::size_t>(path)];
                const int nb = s->maxBin[static_cast<std::size_t>(path)];
                top = std::max(top, nb);
                const int K = std::min(static_cast<int>(parts.size()), cap);
                for (int k = 0; k < K; ++k) {
                    const auto& x = fdl_[static_cast<std::size_t>(i)][static_cast<std::size_t>((fdlHead_ - k + cap) % cap)];
                    const auto& h = parts[static_cast<std::size_t>(k)];
                    const std::complex<float>* xp = x.data();
                    const std::complex<float>* hp = h.data();
                    std::complex<float>* ap = acc.data();
                    for (int b = 0; b < nb; ++b) ap[b] += xp[b] * hp[b];
                }
            }
        }
        if (top == 0 || (g0 == 0.0f && g1 == 0.0f)) {
            if (!add) std::fill(dst.begin(), dst.end(), 0.0f);
            return;
        }
        std::fill(scratch_.begin(), scratch_.end(), 0.0f);
        for (int b = 0; b < top; ++b) {
            scratch_[static_cast<std::size_t>(2 * b)] = acc[static_cast<std::size_t>(b)].real();
            scratch_[static_cast<std::size_t>(2 * b + 1)] = acc[static_cast<std::size_t>(b)].imag();
        }
        fft_->performRealOnlyInverseTransform(scratch_.data());
        const float* y = scratch_.data() + P;   // overlap-save: the second half is valid
        if (!add && g0 == 1.0f && g1 == 1.0f) { std::copy(y, y + P, dst.begin()); return; }
        for (int t = 0; t < P; ++t) {
            const float g = g0 + (g1 - g0) * (static_cast<float>(t) + 0.5f) / static_cast<float>(P);
            if (add) dst[static_cast<std::size_t>(t)] += y[t] * g;
            else dst[static_cast<std::size_t>(t)] = y[t] * g;
        }
    };
    // A whole set (with motion variants on the outputs they cover), scaled by a0 -> a1.
    auto renderSet = [&](const TailSpectra* s, float a0, float a1, bool add) {
        for (int o = 0; o < kOutputs; ++o) {
            auto& dst = job.out[static_cast<std::size_t>(o)];
            bool hasVariants = s != nullptr && s->variants[0] != nullptr && s->variants[1] != nullptr
                && !s->variants[0]->h[static_cast<std::size_t>(o)].empty();
            const bool moving = hasVariants && (job.motionFrom[0] < 1.0f || job.motionTo[0] < 1.0f);
            if (!moving) { renderOne(s, o, a0, a1, dst, add); continue; }
            renderOne(s, o, a0 * job.motionFrom[0], a1 * job.motionTo[0], dst, add);
            for (int v = 0; v < 2; ++v)
                renderOne(s->variants[static_cast<std::size_t>(v)].get(), o,
                          a0 * job.motionFrom[static_cast<std::size_t>(v + 1)], a1 * job.motionTo[static_cast<std::size_t>(v + 1)], dst, true);
        }
    };

    if (old != nullptr) {
        // One-partition crossfade old -> new.
        renderSet(cur, 0.0f, 1.0f, false);
        renderSet(old, 1.0f, 0.0f, true);
    } else {
        renderSet(cur, 1.0f, 1.0f, false);
    }

    fdlHead_ = (fdlHead_ + 1) % cap;
    delete job.old; job.old = nullptr;
    for (auto*& d : job.dispose) { delete d; d = nullptr; }
}

} // namespace septima
