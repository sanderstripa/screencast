#pragma once
#include "dsp.h"
#include <complex>

namespace dsp {
// 512-point Hann STFT, 75% overlap; non-causal lookahead is supplied by the
// recorder's existing packet latency. No delayed wet/dry comb filtering.
class SpectralDenoiser {
    static constexpr int N = 512, Hop = 128, Ring = 2048;
    using Complex = std::complex<float>;
    std::array<float, N> window_{};
    std::array<float, N/2+1> noise_{}, smoothed_{}, gains_{};
    std::array<float, Ring> sum_{}, weight_{};
    std::array<int64_t, Ring> tags_{};
    int64_t next_{-384};
    bool learned_{};
    static void fft(std::array<Complex, N>& data, bool inverse) {
        for (int i = 1, j = 0; i < N; ++i) {
            int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
            if (i < j) std::swap(data[i], data[j]);
        }
        for (int length = 2; length <= N; length *= 2) {
            float angle = (inverse ? 1.f : -1.f) * 6.28318530718f / length;
            Complex increment(std::cos(angle), std::sin(angle));
            for (int i = 0; i < N; i += length) {
                Complex w(1,0);
                for (int j = 0; j < length/2; ++j) {
                    auto a = data[i+j], b = data[i+j+length/2] * w;
                    data[i+j] = a+b; data[i+j+length/2] = a-b; w *= increment;
                }
            }
        }
        if (inverse) for (auto& v : data) v /= N;
    }
    void analyze(Timeline const& mic, int64_t start, float strength) {
        std::array<Complex, N> spectrum{};
        float power{};
        for (int i = 0; i < N; ++i) {
            auto sample = mic.at(start+i); float mono = (sample[0] + sample[1]) * .5f;
            spectrum[i] = mono * window_[i]; power += mono * mono;
        }
        bool quiet = power / N < .000324f;
        fft(spectrum, false);
        for (int k = 0; k <= N/2; ++k) {
            float p = std::norm(spectrum[k]);
            smoothed_[k] = learned_ ? .8f * smoothed_[k] + .2f * p : p;
            if (!learned_) noise_[k] = quiet ? p : .0001f;
            else {
                // Learn downward quickly; grow slowly during speech to avoid eating vowels.
                float adaptation = smoothed_[k] < noise_[k] ? .12f : quiet ? .035f : .00015f;
                noise_[k] += adaptation * (smoothed_[k] - noise_[k]);
            }
            float subtraction = 1.8f * strength * noise_[k] / (p + 1e-12f);
            float wanted = std::sqrt(std::max(.0025f, 1.f - subtraction));
            wanted = 1 - strength + strength * wanted;
            gains_[k] += (wanted > gains_[k] ? .65f : .25f) * (wanted - gains_[k]);
            float gain = strength == 0 ? 1.f : gains_[k];
            spectrum[k] *= gain;
            if (k > 0 && k < N/2) spectrum[N-k] *= gain;
        }
        learned_ = true; fft(spectrum, true);
        for (int i = 0; i < N; ++i) {
            int64_t frame = start+i; if (frame < 0) continue;
            int slot = static_cast<int>(frame % Ring);
            if (tags_[slot] != frame) { tags_[slot] = frame; sum_[slot] = weight_[slot] = 0; }
            sum_[slot] += spectrum[i].real() * window_[i]; weight_[slot] += window_[i] * window_[i];
        }
    }
public:
    SpectralDenoiser() {
        tags_.fill(-1); gains_.fill(1);
        for (int i = 0; i < N; ++i) window_[i] = .5f - .5f * std::cos(6.28318530718f * i / N);
    }
    void render(Timeline const& mic, int64_t start, int frames, float strength, std::vector<float>& result) {
        strength = std::clamp(strength, 0.f, 1.f);
        while (next_ < start+frames) { analyze(mic, next_, strength); next_ += Hop; }
        result.resize(frames);
        for (int i = 0; i < frames; ++i) {
            int64_t frame = start+i; int slot = static_cast<int>(frame % Ring);
            result[i] = tags_[slot] == frame && weight_[slot] > 1e-8f ? sum_[slot] / weight_[slot] : 0.f;
            if (strength == 0) { auto dry = mic.at(frame); result[i] = (dry[0]+dry[1])*.5f; }
        }
    }
};
}
